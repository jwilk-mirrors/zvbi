/*
 *  libzvbi - FreeBSD/OpenBSD bktr driver interface
 *
 *  Copyright (C) 2002 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

static char rcsid[] = "$Id: io-bktr.c,v 1.2.2.1 2003-10-16 18:15:07 mschimek Exp $";

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#include "vbi.h"
#include "io.h"

#ifdef ENABLE_BKTR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>		/* timeval */
#include <sys/types.h>		/* fd_set */
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>

#define printv(format, args...)						\
do {									\
	if (trace) {							\
		fprintf(stderr, format ,##args);			\
		fflush(stderr);						\
	}								\
} while (0)

typedef struct vbi_capture_bktr {
	vbi_capture		capture;

	int			fd;
	vbi_bool		select;

	vbi_raw_decoder		dec;

	double			time_per_frame;

	vbi_capture_buffer	*raw_buffer;
	int			num_raw_buffers;

	vbi_capture_buffer	sliced_buffer;

} vbi_capture_bktr;

static int
bktr_read			(vbi_capture *		vc,
				 vbi_capture_buffer **	raw,
				 vbi_capture_buffer **	sliced,
				 struct timeval *	timeout)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);
	vbi_capture_buffer *my_raw = v->raw_buffer;
	struct timeval tv;
	int r;

	while (v->select) {
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(v->fd, &fds);

		tv = *timeout; /* kernel overwrites this? */

		r = select(v->fd + 1, &fds, NULL, NULL, &tv);

		if (r < 0 && errno == EINTR)
			continue;

		if (r <= 0)
			return r; /* timeout or error */

		break;
	}

	if (!raw)
		raw = &my_raw;
	if (!*raw)
		*raw = v->raw_buffer;
	else
		(*raw)->size = v->raw_buffer[0].size;

	for (;;) {
		/* from zapping/libvbi/v4lx.c */
		pthread_testcancel();

		r = read(v->fd, (*raw)->data, (*raw)->size);

		if (r == -1 && errno == EINTR)
			continue;

		if (r == (*raw)->size)
			break;
		else
			return -1;
	}

	gettimeofday(&tv, NULL);

	(*raw)->timestamp = tv.tv_sec + tv.tv_usec * (1 / 1e6);

	if (sliced) {
		int lines;

		if (*sliced) {
			lines = vbi_raw_decode(&v->dec, (*raw)->data,
					       (vbi_sliced *)(*sliced)->data);
		} else {
			*sliced = &v->sliced_buffer;
			lines = vbi_raw_decode(&v->dec, (*raw)->data,
					       (vbi_sliced *)(v->sliced_buffer.data));
		}

		(*sliced)->size = lines * sizeof(vbi_sliced);
		(*sliced)->timestamp = (*raw)->timestamp;
	}

	return 1;
}

static vbi_raw_decoder *
bktr_parameters(vbi_capture *vc)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);

	return &v->dec;
}

static void
bktr_delete(vbi_capture *vc)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);

	if (v->sliced_buffer.data)
		free(v->sliced_buffer.data);

	for (; v->num_raw_buffers > 0; v->num_raw_buffers--)
		free(v->raw_buffer[v->num_raw_buffers - 1].data);

	if (v->fd != -1)
		close(v->fd);

	free(v);
}

static int
bktr_fd(vbi_capture *vc)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);

	return v->fd;
}

/*
 *  FIXME: This seems to work only when video capturing is active
 *  (tested w/xawtv). Something I overlooked or a driver feature?
 */

vbi_capture *
vbi_capture_bktr_new		(const char *		dev_name,
				 int			scanning,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	char *driver_name = _("BKTR driver");
	vbi_capture_bktr *v;

	pthread_once (&vbi_init_once, vbi_init);

	assert(services && *services != 0);

	printv("Try to open bktr vbi device, libzvbi interface rev.\n"
	       "%s", rcsid);

	if (!(v = (vbi_capture_bktr *) calloc(1, sizeof(*v)))) {
		vbi_asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		return NULL;
	}

	v->capture.parameters = bktr_parameters;
	v->capture._delete = bktr_delete;
	v->capture.get_fd = bktr_fd;

	if ((v->fd = open(dev_name, O_RDONLY)) == -1) {
		vbi_asprintf(errstr, _("Cannot open '%s': %d, %s."),
			     dev_name, errno, strerror(errno));
		goto io_error;
	}

	printv("Opened %s\n", dev_name);

	/*
	 *  XXX
	 *  Can we somehow verify this really is /dev/vbiN (bktr) and not
	 *  /dev/hcfr (halt and catch fire on read) ?
	 */

	v->dec.bytes_per_line = 2048;
	v->dec.interlaced = FALSE;
	v->dec.synchronous = TRUE;

	v->dec.count[0]	= 16;
	v->dec.count[1] = 16;

	switch (v->dec.scanning) {
	default:
		v->dec.scanning = 625;

		/* fall through */

	case 625:
		/* Not confirmed */
		v->dec.sampling_rate = 35468950;
		v->dec.offset = (int)(10.2e-6 * 35468950);
		v->dec.start[0] = 22 + 1 - v->dec.count[0];
		v->dec.start[1] = 335 + 1 - v->dec.count[1];
		break;

	case 525:
		/* Not confirmed */
		v->dec.sampling_rate = 28636363;
		v->dec.offset = (int)(9.2e-6 * 28636363);
		v->dec.start[0] = 10;
		v->dec.start[1] = 273;
		break;
	}

	v->time_per_frame =
		(v->dec.scanning == 625) ? 1.0 / 25 : 1001.0 / 30000;

	v->select = FALSE; /* XXX ? */

	printv("Guessed videostandard %d\n", v->dec.scanning);

	v->dec.sampling_format = VBI_PIXFMT_YUV420;

	if (*services & ~(VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625)) {
		*services = vbi_raw_decoder_add_services (&v->dec, *services, strict);

		if (*services == 0) {
			vbi_asprintf(errstr, _("Sorry, %s (%s) cannot "
						 "capture any of "
						 "the requested data services."),
				     dev_name, driver_name);
			goto failure;
		}

		v->sliced_buffer.data =
			malloc((v->dec.count[0] + v->dec.count[1])
			       * sizeof(vbi_sliced));

		if (!v->sliced_buffer.data) {
			vbi_asprintf(errstr, _("Virtual memory exhausted."));
			errno = ENOMEM;
			goto failure;
		}
	}

	printv("Will decode services 0x%08x\n", *services);

	/* Read mode */

	if (!v->select)
		printv("Warning: no read select, reading will block\n");

	v->capture.read = bktr_read;

	v->raw_buffer = calloc(1, sizeof(v->raw_buffer[0]));

	if (!v->raw_buffer) {
		vbi_asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->raw_buffer[0].size = (v->dec.count[0] + v->dec.count[1])
		* v->dec.bytes_per_line;

	v->raw_buffer[0].data = malloc(v->raw_buffer[0].size);

	if (!v->raw_buffer[0].data) {
		vbi_asprintf(errstr, _("Not enough memory to allocate "
					 "vbi capture buffer (%d KB)."),
			     (v->raw_buffer[0].size + 1023) >> 10);
		goto failure;
	}

	v->num_raw_buffers = 1;

	printv("Capture buffer allocated\n");

	printv("Successful opened %s (%s)\n",
	       dev_name, driver_name);

	return &v->capture;

failure:
io_error:
	bktr_delete(&v->capture);

	return NULL;
}

#else

vbi_capture *
vbi_capture_bktr_new		(const char *		dev_name,
				 int			scanning,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	pthread_once (&vbi_init_once, vbi_init);

	vbi_asprintf(errstr, _("BKTR interface not compiled."));

	return NULL;
}

#endif /* !ENABLE_BKTR */
