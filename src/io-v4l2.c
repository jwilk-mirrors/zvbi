/*
 *  libzvbi - V4L2 interface
 *
 *  Copyright (C) 1999-2004 Michael H. Schimek
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

static const char rcsid [] =
"$Id: io-v4l2.c,v 1.28 2004-10-05 23:46:28 mschimek Exp $";

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#include "vbi.h"
#include "io.h"

#ifdef ENABLE_V4L2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
//#include <fcntl.h>
//#include <unistd.h>		
#include <assert.h>
#include <sys/time.h>		/* timeval */
#include <sys/types.h>		/* fd_set */
//#include <sys/ioctl.h>
#include <sys/ioctl.h>		/* for (_)videodev2.h */
//#include <sys/mman.h>
#include <asm/types.h>		/* for videodev2.h */
#include <pthread.h>

#include "videodev2.h"
#include "_videodev2.h"

/* This macro checks at compile time if the arg type is correct,
   device_ioctl() repeats the ioctl if interrupted (EINTR) and logs
   the args and result if sys_log_fp is non-zero. */
#define xioctl(v, cmd, arg)						\
(IOCTL_ARG_TYPE_CHECK_ ## cmd (arg),					\
 device_ioctl (v->capture.sys_log_fp, fprint_ioctl_arg, v->fd,		\
               cmd, (void *)(arg)))

#define printv(format, args...)						\
do {									\
	if (v->do_trace) {						\
		fprintf (stderr, format ,##args);			\
		fflush (stderr);					\
	}								\
} while (0)

typedef struct {
	vbi_capture		capture;

	int			fd;

	struct v4l2_capability  vcap;

	vbi_bool		do_trace;
} vbi_capture_v4l2;

static void
v4l2_delete			(vbi_capture *		vc)
{
	vbi_capture_v4l2 *v = PARENT (vc, vbi_capture_v4l2, capture);

	if (-1 != v->fd)
		device_close (v->capture.sys_log_fp, v->fd);

	CLEAR (*v);

	free(v);
}

/* document below */
vbi_capture *
vbi_capture_v4l2_new		(const char *		dev_name,
				 int			buffers,
				 unsigned int *		services,
				 int			strict,
				 char **		errorstr,
				 vbi_bool		trace)
{
	char *guess = "";
	vbi_capture_v4l2 *v;

	pthread_once (&vbi_init_once, vbi_init);

	if (!(v = calloc (1, sizeof (*v)))) {
		vbi_asprintf (errorstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		return NULL;
	}

	v->do_trace = trace;

	printv ("Try to open V4L2 0.20 VBI device, "
		"libzvbi interface rev.\n  %s\n", rcsid);

	v->fd = device_open (v->capture.sys_log_fp, dev_name, O_RDWR, 0);
	if (-1 == v->fd) {
		v->fd = device_open (v->capture.sys_log_fp,
				     dev_name, O_RDONLY, 0);
		if (-1 == v->fd) {
			vbi_asprintf (errorstr, _("Cannot open '%s': %d, %s."),
				      dev_name, errno, strerror (errno));
			goto io_error;
		}
	}

	printv ("Opened %s\n", dev_name);

	if (-1 == xioctl (v, VIDIOC_QUERYCAP, &v->vcap)) {
		vbi_asprintf (errorstr, _("Cannot identify '%s': %d, %s."),
			      dev_name, errno, strerror (errno));
		guess = _("Probably not a v4l2 device.");

		v4l2_delete (&v->capture);

		/* Try V4L2 2.6. */
		return vbi_capture_v4l2k_new (dev_name, -1, buffers,
					      services, strict,
					      errorstr, trace);
	}

	/* XXX localize. */
	vbi_asprintf (errorstr, "V4L2 0.20 API not supported.");

 io_error:
	v4l2_delete (&v->capture);

	return NULL;
}

#else

/**
 * @param dev_name Name of the device to open, usually one of
 *   @c /dev/vbi or @c /dev/vbi0 and up.
 * @param buffers Number of device buffers for raw vbi data, when
 *   the driver supports streaming. Otherwise one bounce buffer
 *   is allocated for vbi_capture_pull().
 * @param services This must point to a set of @ref VBI_SLICED_
 *   symbols describing the
 *   data services to be decoded. On return the services actually
 *   decodable will be stored here. See vbi_raw_decoder_add()
 *   for details. If you want to capture raw data only, set to
 *   @c VBI_SLICED_VBI_525, @c VBI_SLICED_VBI_625 or both.
 *   If this parameter is @c NULL, no services will be installed.
 *   You can do so later with vbi_capture_update_services(); note the
 *   reset parameter must be set to @c TRUE in this case.
 * @param strict Will be passed to vbi_raw_decoder_add().
 * @param errorstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress messages on stderr.
 *
 * Note: Starting with libzvbi 0.2.9 the V4L2 0.20 API is no longer
 * supported. The function still recognizes V4L2 0.20 drivers
 * for debugging purposes and supports Linux 2.6 V4L2 drivers.
 * 
 * @return
 * Initialized vbi_capture context, @c NULL on failure.
 */
vbi_capture *
vbi_capture_v4l2_new		(const char *		dev_name,
				 int			buffers,
				 unsigned int *		services,
				 int			strict,
				 char **		errorstr,
				 vbi_bool		trace)
{
	if (0) /* unused, no warning please */
		fputs (rcsid, stderr);

	pthread_once (&vbi_init_once, vbi_init);

	vbi_asprintf (errorstr, _("V4L2 driver interface not compiled."));

	return NULL;
}

#endif /* !ENABLE_V4L2 */
