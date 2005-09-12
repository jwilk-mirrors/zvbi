/*
 *  libzvbi - V4L2 (version 2002-10 and later) interface
 *
 *  Copyright (C) 2002-2005 Michael H. Schimek
 *  Copyright (C) 2003-2004 Tom Zoerner
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
"$Id: io-v4l2k.c,v 1.30 2005-09-12 19:42:02 mschimek Exp $";

/*
 *  Around Oct-Nov 2002 the V4L2 API was revised for inclusion into
 *  Linux 2.5/2.6. There are a few subtle differences, in order to
 *  keep the source clean this interface has been forked off from the
 *  old V4L2 interface. "v4l2k" is no official designation, there is
 *  none, take it as v4l2-kernel or v4l-2000.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "vbi.h"
#include "io.h"

#ifdef ENABLE_V4L2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>		/* read(), dup2() */
#include <assert.h>
#include <sys/time.h>		/* timeval */
#include <sys/types.h>		/* fd_set */
#include <sys/ioctl.h>		/* for (_)videodev2k.h */
#include <sys/mman.h>		/* PROT_READ, MAP_SHARED */
#include <asm/types.h>		/* for videodev2k.h */
#include <pthread.h>

#include "videodev2k.h"
#include "_videodev2k.h"

#include "raw_decoder.h"

/* This macro checks at compile time if the arg type is correct,
   device_ioctl() repeats the ioctl if interrupted (EINTR) and logs
   the args and result if sys_log_fp is non-zero. */
#define xioctl(v, cmd, arg)						\
(IOCTL_ARG_TYPE_CHECK_ ## cmd (arg),					\
 device_ioctl (v->capture.sys_log_fp, fprint_ioctl_arg, v->fd,		\
               cmd, (void *)(arg)))

#undef REQUIRE_SELECT
#undef REQUIRE_G_FMT		/* before S_FMT */
#undef REQUIRE_S_FMT		/* else accept current format */

#define printv(format, args...)						\
do {									\
	if (v->do_trace) {							\
		fprintf(stderr, format ,##args);			\
		fflush(stderr);						\
	}								\
} while (0)

#define ENQUEUE_SUSPENDED       -3
#define ENQUEUE_STREAM_OFF      -2
#define ENQUEUE_BUFS_QUEUED     -1
#define ENQUEUE_IS_UNQUEUED(X)  ((X) >= 0)

#define FLUSH_FRAME_COUNT       2

typedef struct vbi_capture_v4l2 {
	vbi_capture		capture;

	int			fd;
	vbi_bool		close_me;
	int			btype;			/* v4l2 stream type */
	vbi_bool		streaming;
	vbi_bool		read_active;
	vbi_bool		do_trace;
	int			has_try_fmt;
	int			enqueue;
	struct v4l2_buffer      vbuf;
	struct v4l2_capability  vcap;
	char		      * p_dev_name;

	vbi_raw_decoder		dec;
	struct v4l2_sliced_vbi_cap svcap;
	struct v4l2_format	svfmt;
        unsigned int            services;    /* all services, including raw */

	double			time_per_frame;

	vbi_capture_buffer *	capture_buffer;
	unsigned int		n_capture_buffers;
	int			buf_req_count;

	vbi_capture_buffer	sliced_buffer;
	int			flush_frame_count;

	vbi_bool		pal_start1_fix;
	vbi_bool		saa7134_ntsc_fix;
} vbi_capture_v4l2;

static const unsigned int	VBI_SLICED_SUPPORTED_BY_V4L2 =
	(VBI_SLICED_TELETEXT_B |
	 VBI_SLICED_VPS |
	 VBI_SLICED_CAPTION_525 |
	 VBI_SLICED_WSS_625);
static const unsigned int	SLICED_END =
	N_ELEMENTS (((struct v4l2_sliced_vbi_format *) 0)->service_lines[0]);

static vbi_service_set
vbi_sliced_from_v4l2_sliced	(unsigned int		id)
{
	switch (id) {
	case V4L2_SLICED_TELETEXT_B:	return VBI_SLICED_TELETEXT_B;
	case V4L2_SLICED_VPS:		return VBI_SLICED_VPS;
	case V4L2_SLICED_CAPTION_525:	return VBI_SLICED_CAPTION_525;
	case V4L2_SLICED_WSS_625:	return VBI_SLICED_WSS_625;
		/* No default. */
	}

	return 0;
}

static vbi_service_set
vbi_service_set_from_v4l2_sliced (unsigned int		set)
{
	vbi_service_set services = 0;

	if (set & V4L2_SLICED_TELETEXT_B)
		services |= VBI_SLICED_TELETEXT_B;
	if (set & V4L2_SLICED_VPS)
		services |= VBI_SLICED_VPS;
	if (set & V4L2_SLICED_CAPTION_525)
		services |= VBI_SLICED_CAPTION_525;
	if (set & V4L2_SLICED_WSS_625)
		services |= VBI_SLICED_WSS_625;

	return services;
}

static unsigned int
vbi_service_set_to_v4l2_sliced	(vbi_service_set	services)
{
	unsigned int set = 0;

	if (services & VBI_SLICED_TELETEXT_B)
		set |= V4L2_SLICED_TELETEXT_B;
	if (services & VBI_SLICED_VPS)
		set |= V4L2_SLICED_VPS;
	if (services & VBI_SLICED_CAPTION_525)
		set |= V4L2_SLICED_CAPTION_525;
	if (services & VBI_SLICED_WSS_625)
		set |= V4L2_SLICED_WSS_625;

	return set;
}

static void
v4l2_stream_stop		(vbi_capture_v4l2 *	v)
{
	if (v->enqueue >= ENQUEUE_BUFS_QUEUED) {
		printv ("Suspending stream...\n");

		/* Error ignored. */
		xioctl (v, VIDIOC_STREAMOFF, &v->btype);
	}

	while (v->n_capture_buffers > 0) {
		--v->n_capture_buffers;
		device_munmap (v->capture.sys_log_fp,
			       v->capture_buffer[v->n_capture_buffers].data,
			       v->capture_buffer[v->n_capture_buffers].size);
	}

	if (NULL != v->capture_buffer) {
		free (v->capture_buffer);
		v->capture_buffer = NULL;
	}

	v->enqueue = ENQUEUE_SUSPENDED;
}

static int
v4l2_stream_alloc		(vbi_capture_v4l2 *	v,
				 char **		errstr)
{
	struct v4l2_requestbuffers vrbuf;
	char *guess = NULL;

	assert (v->enqueue == ENQUEUE_SUSPENDED);
	assert (v->capture_buffer == NULL);

	printv ("Requesting %d streaming i/o buffers\n",
		v->buf_req_count);

	CLEAR (vrbuf);
	vrbuf.type = v->btype;
	vrbuf.count = v->buf_req_count;
	vrbuf.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl (v, VIDIOC_REQBUFS, &vrbuf)) {
		vbi_asprintf (errstr,
			      _("Cannot request streaming i/o buffers "
				"from %s (%s): %s."),
			      v->p_dev_name, v->vcap.card,
			      strerror (errno));
		guess = _("Possibly a driver bug.");
		goto failure;
	}

	if (0 == vrbuf.count) {
		vbi_asprintf (errstr, _("%s (%s) granted no streaming "
					"i/o buffers, perhaps the physical "
					"memory is exhausted."),
			      v->p_dev_name, v->vcap.card);
		goto failure;
	}

	printv ("Mapping %d streaming i/o buffers\n",
		vrbuf.count);

	v->capture_buffer = calloc (vrbuf.count,
				    sizeof (v->capture_buffer[0]));
	if (NULL == v->capture_buffer) {
		vbi_asprintf (errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->n_capture_buffers = 0;

	while (v->n_capture_buffers < vrbuf.count) {
		struct v4l2_buffer vbuf;
		unsigned int i, s;
		uint8_t *p;

		CLEAR (vbuf);
		vbuf.type = v->btype;
		vbuf.index = v->n_capture_buffers;

		if (-1 == xioctl (v, VIDIOC_QUERYBUF, &vbuf)) {
			vbi_asprintf (errstr,
				      _("Querying streaming i/o buffer #%d "
					"from %s (%s) failed: %s."),
				      v->n_capture_buffers, v->p_dev_name,
				      v->vcap.card, strerror(errno));
			goto mmap_failure;
		}

		p = device_mmap (v->capture.sys_log_fp,
				 /* start: any */ NULL,
				 vbuf.length,
				 PROT_READ | PROT_WRITE,
				 MAP_SHARED, /* MAP_PRIVATE ? */
				 v->fd,
				 vbuf.m.offset);

		/* V4L2 spec requires PROT_WRITE regardless if we write
		   buffers, but broken drivers might reject it. */
		if (MAP_FAILED == p)
			p = device_mmap (v->capture.sys_log_fp,
					 /* start: any */ NULL,
					 vbuf.length,
					 PROT_READ,
					 MAP_SHARED,
					 v->fd,
					 vbuf.m.offset);

		if (MAP_FAILED == p) {
			if (ENOMEM == errno
			    && v->n_capture_buffers >= 2) {
				printv ("Memory mapping buffer #%d failed: "
					"%d, %s (ignored).",
					v->n_capture_buffers,
					errno, strerror(errno));
				break;
			}

			vbi_asprintf (errstr, _("Memory mapping streaming "
						"i/o buffer #%d "
						"from %s (%s) failed: %s."),
				      v->n_capture_buffers, v->p_dev_name,
				      v->vcap.card, strerror(errno));
			goto mmap_failure;
		}

		v->capture_buffer[v->n_capture_buffers].data = p;
		v->capture_buffer[v->n_capture_buffers].size = vbuf.length;

		for (i = s = 0; i < vbuf.length; i++)
			s += p[i];

		if (s % vbuf.length) {
			fprintf (stderr,
				 "Security warning: driver %s (%s) seems "
				 "to mmap physical memory uncleared. Please "
				 "contact the driver author.\n",
				 v->p_dev_name, v->vcap.card);
			exit(EXIT_FAILURE);
		}

		if (-1 == xioctl (v, VIDIOC_QBUF, &vbuf)) {
			vbi_asprintf (errstr,
				      _("Cannot enqueue streaming i/o buffer "
					"#%d to %s (%s): %s."),
				      v->n_capture_buffers, v->p_dev_name,
				      v->vcap.card, strerror(errno));
			guess = _("Probably a driver bug.");
			goto mmap_failure;
		}

		++v->n_capture_buffers;
	}

	v->enqueue = ENQUEUE_STREAM_OFF;

	return 0;

mmap_failure:
	{
		int saved_errno = errno;

		v4l2_stream_stop(v);

		errno = saved_errno;
	}

failure:
        printv("v4l2-stream_alloc: "
	       "failed with errno=%d, msg='%s' guess='%s'\n",
	       errno, *errstr, guess ? guess : "");

	return -1;
}

static vbi_bool
restart_stream			(vbi_capture_v4l2 *	v)
{
	unsigned int i;

	if (-1 == xioctl (v, VIDIOC_STREAMOFF, &v->btype))
		return FALSE;

	for (i = 0; i < v->n_capture_buffers; ++i) {
		struct v4l2_buffer vbuf;

		CLEAR (vbuf);

		vbuf.index = i;
		vbuf.type = v->btype;

		if (-1 == xioctl (v, VIDIOC_QBUF, &vbuf))
			return FALSE;
	}

	if (-1 == xioctl (v, VIDIOC_STREAMON, &v->btype))
		return FALSE;

	return TRUE;
}

static void
store_sliced			(vbi_capture_v4l2 *	v,
				 vbi_capture_buffer **	sliced,
				 unsigned int		index,
				 double			time)
{
	vbi_sliced *d;
	const struct v4l2_sliced_vbi_data *s;
	unsigned int n_lines;

	if (NULL == *sliced)
		*sliced = &v->sliced_buffer;

	d = (vbi_sliced *)(*sliced)->data;
	s = (const struct v4l2_sliced_vbi_data *)
		v->capture_buffer[index].data;

	n_lines = MIN (v->capture_buffer[index].size / sizeof (*s),
		       SLICED_END * 2);

	while (n_lines > 0) {
		d->id = vbi_sliced_from_v4l2_sliced (s->id);
		if (0 != d->id) {
			d->line = s->line;
			if (0 != s->field) {
				if (s->id & V4L2_SLICED_VBI_525)
					d->line += 263;
				else
					d->line += 313;
			}

			memcpy (d->data, s->data,
				MIN (sizeof (d->data),
				     sizeof (s->data)));

			++d;
		}

		++s;
		--n_lines;
	}

	(*sliced)->size = (char *) d - (char *)(*sliced)->data;
	(*sliced)->timestamp = time;
}

static void
store_sliced_from_raw		(vbi_capture_v4l2 *	v,
				 vbi_capture_buffer **	sliced,
				 double			time)
{
	int lines;

	if (NULL == *sliced)
		*sliced = &v->sliced_buffer;

	lines = vbi_raw_decode (&v->dec,
				v->capture_buffer[v->vbuf.index].data,
				(vbi_sliced *)(*sliced)->data);

	(*sliced)->size = lines * sizeof (vbi_sliced);
	(*sliced)->timestamp = time;
}

static void
store_raw			(vbi_capture_v4l2 *	v,
				 vbi_capture_buffer **	raw,
				 double			time)
{
	if (*raw) {
		vbi_capture_buffer *b = *raw;

		b->size = v->capture_buffer[v->vbuf.index].size;
		memcpy (b->data, v->capture_buffer[v->vbuf.index].data,
			b->size);
	} else {
		*raw = v->capture_buffer + v->vbuf.index;
		/* keep this buffer out of the queue */
		v->enqueue = v->vbuf.index;
	}

	(*raw)->timestamp = time;
}

static int
v4l2_stream			(vbi_capture *		vc,
				 vbi_capture_buffer **	raw,
				 vbi_capture_buffer **	sliced,
				 const struct timeval *	timeout_orig)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct timeval timeout = *timeout_orig;
	double time;
	int r;

	if ((v->enqueue == ENQUEUE_SUSPENDED) || (v->services == 0)) {
		/* stream was suspended (add_services not committed) */
		printv("stream-read: no services set or not committed\n");
		errno = ESRCH;
		return -1;
	}

	if (v->enqueue == ENQUEUE_STREAM_OFF) {
		if (-1 == xioctl (v, VIDIOC_STREAMON, &v->btype)) {
		        printv ("stream-read: ERROR: "
				"failed to enable streaming\n");
			return -1;
                }
	} else if (ENQUEUE_IS_UNQUEUED(v->enqueue)) {
		v->vbuf.type = v->btype;
		v->vbuf.index = v->enqueue;

		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
		        printv ("stream-read: ERROR: "
				"failed to queue previous buffer\n");
			return -1;
                }
	}

	v->enqueue = ENQUEUE_BUFS_QUEUED;

	while (1)
	{
		/* wait for the next frame */
		r = vbi_capture_io_select(v->fd, &timeout);
		if (r <= 0) {
			if (r < 0)
				printv("stream-read: select: %d (%s)\n",
				       errno, strerror(errno));
			return r;
		}

		v->vbuf.type = v->btype;

		/* retrieve the captured frame from the queue */
		r = xioctl (v, VIDIOC_DQBUF, &v->vbuf);
		if (-1 == r) {
			int saved_errno;

			saved_errno = errno;

			printv ("stream-read: ioctl DQBUF: "
				"%d (%s)\n", errno, strerror(errno));

			/* On EIO bttv dequeues the buffer, or it does not,
			   or it resets the hardware (SCERR).  QBUF alone
			   is insufficient.
			   Actually the caller should restart on error. */

			/* Errors ignored. */
			restart_stream (v);

                        errno = saved_errno;

			return -1;
		}

		if (v->flush_frame_count > 0) {
			v->flush_frame_count -= 1;
			printv("Skipping frame (%d remaining)\n", v->flush_frame_count);

			if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
				printv ("stream-read: ioctl QBUF: "
					"%d (%s)\n", errno, strerror(errno));
				return -1;
			}
		} else {
			break;
		}
	}

	time = v->vbuf.timestamp.tv_sec
		+ v->vbuf.timestamp.tv_usec * (1 / 1e6);

	if (v->vcap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) {
		if (raw && *raw)
			(*raw)->size = 0;

		if (NULL != sliced)
			store_sliced (v, sliced, v->vbuf.index, time);
	} else {
		if (NULL != raw)
			store_raw (v, raw, time);

		if (NULL != sliced)
			store_sliced_from_raw (v, sliced, time);
	}

	/* if no raw pointer returned to the caller, re-queue
	   buffer immediately else the buffer is re-queued upon
	   the next call to read() */
	if (v->enqueue == ENQUEUE_BUFS_QUEUED) {
		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
			printv ("stream-read: ioctl QBUF: "
				"%d (%s)\n", errno, strerror(errno));
			return -1;
		}
	}

	return 1;
}

static void
v4l2_stream_flush		(vbi_capture *		vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct timeval tv;
	unsigned int max_loop;

	/* stream not enabled yet -> nothing to flush */
	if ( (v->enqueue == ENQUEUE_SUSPENDED) ||
	     (v->enqueue == ENQUEUE_STREAM_OFF) )
		return;

	if (ENQUEUE_IS_UNQUEUED(v->enqueue)) {
		v->vbuf.type = v->btype;
		v->vbuf.index = v->enqueue;

		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
			printv ("stream-flush: ioctl QBUF: "
				" %d (%s)\n", errno, strerror(errno));
			return;
		}
	}

	v->enqueue = ENQUEUE_BUFS_QUEUED;

	for (max_loop = 0; max_loop < v->n_capture_buffers; max_loop++) {
		/* check if there are any buffers pending for de-queueing */
		/* use zero timeout to prevent select() from blocking */

		CLEAR (tv);

		if (vbi_capture_io_select(v->fd, &tv) <= 0)
			return;

		if ((-1 == xioctl (v, VIDIOC_DQBUF, &v->vbuf))
		    && (errno != EIO))
			return;

		/* immediately queue the buffer again,
		   thereby discarding it's content */
		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf))
			return;
	}
}

static void
v4l2_read_stop			(vbi_capture_v4l2 *	v)
{
	while (v->n_capture_buffers > 0) {
		--v->n_capture_buffers;
		free (v->capture_buffer[v->n_capture_buffers].data);
		v->capture_buffer[v->n_capture_buffers].data = NULL;
	}

	free (v->capture_buffer);
	v->capture_buffer = NULL;
}

static int
v4l2_suspend			(vbi_capture_v4l2 *	v)
{
	int fd;

	if (v->streaming) {
		v4l2_stream_stop (v);
	} else {
		v4l2_read_stop (v);

		if (v->read_active) {
			printv ("Suspending read: re-open device...\n");

			/* hack: cannot suspend read to allow
			   S_FMT, need to close device */
			fd = device_open (v->capture.sys_log_fp,
					  v->p_dev_name, O_RDWR, 0);
			if (-1 == fd) {
				printv ("v4l2-suspend: failed to re-open "
					"VBI device: %d: %s\n",
					errno, strerror(errno));
				return -1;
			}

			/* use dup2() to keep the same fd,
			   which may be used by our client */
			device_close (v->capture.sys_log_fp, v->fd);
			dup2 (fd, v->fd);
			device_close (v->capture.sys_log_fp, fd);

			v->read_active = FALSE;
		}
	}

	return 0;
}

static int
v4l2_read_alloc			(vbi_capture_v4l2 *	v,
				 char **		errstr)
{
	assert (NULL == v->capture_buffer);

	v->capture_buffer = calloc (1, sizeof (v->capture_buffer[0]));
	if (!v->capture_buffer) {
		vbi_asprintf (errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	if (v->vcap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) {
		v->capture_buffer[0].size = v->svfmt.fmt.sliced.io_size;
	} else {
		v->capture_buffer[0].size = (v->dec.count[0] + v->dec.count[1])
			* v->dec.bytes_per_line;
	}

	v->capture_buffer[0].data = malloc(v->capture_buffer[0].size);
	if (NULL != v->capture_buffer[0].data) {
		vbi_asprintf(errstr, _("Not enough memory to allocate "
				       "vbi capture buffer (%d KiB)."),
			     (v->capture_buffer[0].size + 1023) >> 10);
		goto failure;
	}

	v->n_capture_buffers = 1;

	printv ("Capture buffer allocated\n");

	return 0;

failure:
        printv ("v4l2-read_alloc: failed with errno=%d, msg='%s'\n",
		errno, *errstr);

	return -1;
}

static int
v4l2_read_frame			(vbi_capture_v4l2 *	v,
				 vbi_capture_buffer *	raw,
				 struct timeval *	timeout)
{
	int r;

	/* wait until data is available or timeout expires */
	r = vbi_capture_io_select(v->fd, timeout);
	if (r <= 0) {
		if (r < 0)
			printv("read-frame: select: %d (%s)\n",
			       errno, strerror(errno));
		return r;
	}

	v->read_active = TRUE;

	for (;;) {
		/* from zapping/libvbi/v4lx.c */
		pthread_testcancel();

		r = read(v->fd, raw->data, raw->size);

		if (r == -1  && (errno == EINTR || errno == ETIME))
			continue;
		if (r == -1)
                        return -1;
		if (r != raw->size) {
			errno = EIO;
			return -1;
		}
		else
			break;
	}
	return 1;
}

static int
v4l2_read			(vbi_capture *		vc,
				 vbi_capture_buffer **	raw,
				 vbi_capture_buffer **	sliced,
				 const struct timeval *	timeout)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	vbi_capture_buffer *my_raw = v->capture_buffer;
	struct timeval tv;
	int r;

	if ((my_raw == NULL) || (v->services == 0)) {
		printv("cannot read: no services set or not committed\n");
		errno = EINVAL;
		return -1;
	}

	if (v->vcap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) {
		if (raw && *raw)
			(*raw)->size = 0;

		tv = *timeout;
		while (1) {
			r = v4l2_read_frame(v, my_raw, &tv);
			if (r <= 0)
				return r;

			if (v->flush_frame_count > 0) {
				v->flush_frame_count -= 1;
				printv("Skipping frame (%d remaining)\n",
				       v->flush_frame_count);
			} else {
				break;
			}
		}

		if (sliced) {
			double time;

			gettimeofday(&tv, NULL);
			time = tv.tv_sec + tv.tv_usec * (1 / 1e6);

			store_sliced (v, sliced, /* buffer index */ 0, time);
		}
	} else {
		if (raw == NULL)
			raw = &my_raw;
		if (*raw == NULL)
			*raw = v->capture_buffer;
		else
			(*raw)->size = v->capture_buffer[0].size;

		tv = *timeout;
		while (1) {
			r = v4l2_read_frame(v, *raw, &tv);
			if (r <= 0)
				return r;

			if (v->flush_frame_count > 0) {
				v->flush_frame_count -= 1;
				printv("Skipping frame (%d remaining)\n",
				       v->flush_frame_count);
			} else {
				break;
			}
		}

		gettimeofday(&tv, NULL);

		(*raw)->timestamp = tv.tv_sec + tv.tv_usec * (1 / 1e6);

		if (sliced) {
			int lines;

			if (*sliced) {
				lines = vbi_raw_decode
					(&v->dec, (*raw)->data,
					 (vbi_sliced *)(*sliced)->data);
			} else {
				*sliced = &v->sliced_buffer;
				lines = vbi_raw_decode
					(&v->dec, (*raw)->data,
					 (vbi_sliced *)
					   (v->sliced_buffer.data));
			}

			(*sliced)->size = lines * sizeof(vbi_sliced);
			(*sliced)->timestamp = (*raw)->timestamp;
		}
	}

	return 1;
}

static void
v4l2_read_flush			(vbi_capture *		vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct timeval tv;
	int r;

	if ( (v->capture_buffer == NULL) || (v->read_active == FALSE) )
		return;

	CLEAR (tv);

	r = vbi_capture_io_select(v->fd, &tv);
	if (r <= 0)
		return;

	do {
		r = read (v->fd, v->capture_buffer->data,
			  v->capture_buffer->size);
	} while ((r < 0) && (errno == EINTR));
}

static vbi_bool
v4l2_get_videostd(vbi_capture_v4l2 *v, char ** errstr)
{
	struct v4l2_standard vstd;
	v4l2_std_id stdid;
	int r;
	char * guess = NULL;

	if (-1 == xioctl (v, VIDIOC_G_STD, &stdid)) {
		vbi_asprintf (errstr,
			      _("Cannot query current videostandard "
				"of %s (%s): %s."),
			      v->p_dev_name, v->vcap.card,
			      strerror(errno));
		guess = _("Probably a driver bug.");
		goto failure;
	}

	vstd.index = 0;

	while (0 == (r = xioctl (v, VIDIOC_ENUMSTD, &vstd))) {
		if (vstd.id & stdid)
			break;
		vstd.index++;
	}

	if (-1 == r) {
		vbi_asprintf(errstr, _("Cannot query current "
				       "videostandard of %s (%s): %s."),
			     v->p_dev_name, v->vcap.card, strerror(errno));
		guess = _("Probably a driver bug.");
		goto failure;
	}

	printv ("Current scanning system is %d\n", vstd.framelines);

	/* add_vbi_services() eliminates non 525/625 */
	v->dec.scanning = vstd.framelines;

	return TRUE;

failure:
        printv("v4l2-get_videostd: "
	       "failed with errno=%d, msg='%s' guess='%s'\n",
               errno, *errstr, guess ? guess : "");

	return FALSE;
}

static int
v4l2_get_scanning(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
        int old_scanning = v->dec.scanning;
        int new_scanning = -1;

        if ( v4l2_get_videostd(v, NULL) ) {

                new_scanning = v->dec.scanning;
        }
        v->dec.scanning = old_scanning;

        return new_scanning;
}

static void
print_vfmt			(const char *		s,
				 struct v4l2_format *	vfmt)
{
	unsigned int i;

	switch (vfmt->type) {
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		fprintf (stderr, "%sformat %08x [%c%c%c%c], %d Hz, "
			 "%d bpl, offs %d, F1 %d...%d, F2 %d...%d, "
			 "flags %08x\n", s,
			 vfmt->fmt.vbi.sample_format,
			 (char)((vfmt->fmt.vbi.sample_format      ) & 0xff),
			 (char)((vfmt->fmt.vbi.sample_format >>  8) & 0xff),
			 (char)((vfmt->fmt.vbi.sample_format >> 16) & 0xff),
			 (char)((vfmt->fmt.vbi.sample_format >> 24) & 0xff),
			 vfmt->fmt.vbi.sampling_rate,
			 vfmt->fmt.vbi.samples_per_line,
			 vfmt->fmt.vbi.offset,
			 vfmt->fmt.vbi.start[0],
			 vfmt->fmt.vbi.start[0] + vfmt->fmt.vbi.count[0] - 1,
			 vfmt->fmt.vbi.start[1],
			 vfmt->fmt.vbi.start[1] + vfmt->fmt.vbi.count[1] - 1,
			 vfmt->fmt.vbi.flags);
		break;

	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		fprintf (stderr, "%sset=0x%x io_size=%u ",
			 s, vfmt->fmt.sliced.service_set,
			 vfmt->fmt.sliced.io_size);

		for (i = 0; i < 2 * SLICED_END; ++i) {
			fprintf (stderr, "0x%x ",
				 vfmt->fmt.sliced.service_lines[0][i]);
		}

		fputc ('\n', stderr);

		break;

	default:
		break;
	}
}

static vbi_bool
sliced_capable			(vbi_capture_v4l2 *	v,
				 const _vbi_service_par *sp,
				 vbi_videostd_set	videostd_set,
				 unsigned int		f2)
{
	unsigned int id;
	unsigned int i;

	if (0 == (videostd_set & sp->videostd_set))
		return FALSE;

	if (sp->last[0] >= SLICED_END
	    || (0 != sp->last[1] && (sp->last[1] - f2) >= SLICED_END))
		return FALSE;

	id = vbi_service_set_to_v4l2_sliced (sp->id);

	if (0 != sp->first[0]) {
		for (i = sp->first[0]; i <= sp->last[0]; ++i)
			id &= v->svcap.service_lines[0][i];
	}

	if (0 != sp->first[1]) {
		for (i = sp->first[1]; i <= sp->last[1]; ++i)
			id &= v->svcap.service_lines[1][i - f2];
	}

	return (0 != id);
}

static void
sliced_add			(struct v4l2_sliced_vbi_format *f,
				 const _vbi_service_par *sp,
				 unsigned int		f2)
{
	unsigned int id;
	unsigned int i;

	id = vbi_service_set_to_v4l2_sliced (sp->id);

	f->service_set |= id;

	if (0 != sp->first[0]) {
		for (i = sp->first[0]; i <= sp->last[0]; ++i)
			f->service_lines[0][i] |= id;
	}

	if (0 != sp->first[1]) {
		for (i = sp->first[1]; i <= sp->last[1]; ++i)
			f->service_lines[1][i - f2] |= id;
	}
}

static unsigned int
sliced_min_io_size		(const struct v4l2_sliced_vbi_format *f)
{
	unsigned int n_lines;
	unsigned int i;

	n_lines = 0;

	for (i = 0; i < 2 * SLICED_END; ++i) {
		n_lines += (0 != f->service_lines[0][i]);
	}

	return n_lines * sizeof (struct v4l2_sliced_vbi_data);
}

static unsigned int
v4l2_update_services_sliced	(vbi_capture *		vc,
				 vbi_bool		reset,
				 vbi_bool		commit,
				 unsigned int		services,
				 int			strict,
				 char **		errstr)
{
	vbi_capture_v4l2 *v = PARENT (vc, vbi_capture_v4l2, capture);
	const _vbi_service_par *sp;
	vbi_videostd_set videostd_set;
	unsigned int field2_offset;
	unsigned int min_io_size;

	strict = strict; /* unused */

	if (NULL == v->sliced_buffer.data) {
		v->sliced_buffer.data =
			malloc (SLICED_END * 2 * sizeof (vbi_sliced));

		if (NULL == v->sliced_buffer.data) {
			vbi_asprintf (errstr, _("Virtual memory exhausted."));
			errno = ENOMEM;
			goto io_error;
		}
	}

	/* Suspend capturing, or driver may return EBUSY. */
	/* XXX should try G_FMT first, maybe when can avoid an interruption. */
	v4l2_suspend (v);

	if (reset) {
	        if (!v4l2_get_videostd (v, errstr))
		        goto io_error;

		CLEAR (v->svcap);
		if (-1 == xioctl (v, VIDIOC_G_SLICED_VBI_CAP, &v->svcap)) {
			goto io_error;
		}

                v->services = 0;
        }

	switch (v->dec.scanning) {
	case 525:
		videostd_set = VBI_VIDEOSTD_SET_525_60;
		field2_offset = 262;
		break;

	case 625:
		videostd_set = VBI_VIDEOSTD_SET_625_50;
		field2_offset = 313;
		break;

	default:
		goto error;
	}

	services &= VBI_SLICED_SUPPORTED_BY_V4L2;

	CLEAR (v->svfmt);

	v->btype = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
	v->svfmt.type = v->btype;

	for (sp = _vbi_service_table; 0 != sp->id; ++sp) {
		if (0 == (services & sp->id)) {
			continue;
		}

		if (!sliced_capable (v, sp, videostd_set, field2_offset)) {
			services &= ~sp->id;
			continue;
		}

		sliced_add (&v->svfmt.fmt.sliced, sp, field2_offset);
	}

	if (0 == services) {
		goto no_services;
	}

	v->svfmt.fmt.sliced.io_size = sliced_min_io_size (&v->svfmt.fmt.sliced);

	if (v->do_trace)
		print_vfmt ("VBI capture parameters: ", &v->svfmt);

	printv ("Requesting sliced vbi data... ");

	if (-1 == xioctl (v, VIDIOC_S_FMT, &v->svfmt)) {
		printv ("failed\n");

		vbi_asprintf (errstr, _("Cannot set sliced vbi "
					"parameters of %s (%s): %s."),
			      v->p_dev_name, v->vcap.card, strerror(errno));
		goto io_error;
	}

	printv ("success\n");

	min_io_size = sliced_min_io_size (&v->svfmt.fmt.sliced);

	if (0 == v->svfmt.fmt.sliced.service_set
	    || 0 == v->svfmt.fmt.sliced.io_size
	    || 0 == min_io_size) {
		goto no_services;
	}

	if (v->svfmt.fmt.sliced.io_size < min_io_size) {
		printv ("Driver returns incorrect i/o size %u, expected %u.",
			v->svfmt.fmt.sliced.io_size, min_io_size);
	}

	services = vbi_service_set_from_v4l2_sliced
		(v->svfmt.fmt.sliced.service_set);

	/* XXX something fancy is needed here to handle cases
	   where the driver looks for only one (or a select few)
	   services on each line. */

        v->services |= services;
	printv ("Will capture services 0x%08x, "
		"added 0x%0x commit:%d\n",
		v->services, services, commit);

	if (commit && (v->services != 0)) {
		if (v->streaming) {
			if (v4l2_stream_alloc(v, errstr) != 0)
				goto io_error;
		} else {
			if (v4l2_read_alloc(v, errstr) != 0)
				goto io_error;
		}
	}

	return services;

 no_services:
	vbi_asprintf (errstr, _("Sorry, %s (%s) cannot capture any "
				"of the requested data services "
				"with scanning %d."),
		      v->p_dev_name, v->vcap.card, v->dec.scanning);
 io_error:
        printv ("v4l2-update_services_sliced: "
		"failed with errno=%d, msg='%s'\n",
		errno, *errstr);
 error:
	return 0;
}

static unsigned int
v4l2_update_services_raw	(vbi_capture *		vc,
				 vbi_bool		reset,
				 vbi_bool		commit,
				 unsigned int		services,
				 int			strict,
				 char **		errstr)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct v4l2_format vfmt;
	int    max_rate;
	int    g_fmt;
	int    s_fmt;
	char * guess = NULL;

	/* suspend capturing, or driver will return EBUSY */
	v4l2_suspend(v);

	if (reset) {
                /* query current norm */
	        if (v4l2_get_videostd(v, errstr) == FALSE)
		        goto io_error;

		vbi_raw_decoder_reset(&v->dec);
                v->services = 0;
        }

	memset(&vfmt, 0, sizeof(vfmt));

	vfmt.type = v->btype = V4L2_BUF_TYPE_VBI_CAPTURE;

	max_rate = 0;

	printv("Querying current vbi parameters... ");

	g_fmt = xioctl (v, VIDIOC_G_FMT, &vfmt);

	if (-1 == g_fmt) {
		printv("failed\n");
#ifdef REQUIRE_G_FMT
		vbi_asprintf(errstr, _("Cannot query current "
				       "vbi parameters of %s (%s): %s."),
			     v->p_dev_name, v->vcap.card, strerror(errno));
		goto io_error;
#else
		strict = MAX(0, strict);
#endif
	} else {
		printv("success\n");

		if (v->do_trace)
			print_vfmt("VBI capture parameters supported: ", &vfmt);

		if (v->has_try_fmt == -1) {
			struct v4l2_format vfmt_temp = vfmt;

			/* test if TRY_FMT is available by feeding it the current
			** parameters, which should always succeed */
			v->has_try_fmt =
				(0 == xioctl (v, VIDIOC_TRY_FMT, &vfmt_temp));
		}
	}

	if (strict >= 0) {
		struct v4l2_format vfmt_temp = vfmt;
		vbi_raw_decoder dec_temp;
	        unsigned int sup_services;
		int r;

		printv("Attempt to set vbi capture parameters\n");

		memset(&dec_temp, 0, sizeof(dec_temp));
		sup_services = vbi_raw_decoder_parameters(&dec_temp, services | v->services,
						          v->dec.scanning, &max_rate);

	        services &= sup_services;

		if (0 == services) {
			vbi_asprintf(errstr, _("Sorry, %s (%s) cannot capture any of the "
					       "requested data services with scanning %d."),
	                                       v->p_dev_name, v->vcap.card, v->dec.scanning);
			goto failure;
		}

		vfmt.fmt.vbi.sample_format	= V4L2_PIX_FMT_GREY;
		vfmt.fmt.vbi.sampling_rate	= dec_temp.sampling_rate;
		vfmt.fmt.vbi.samples_per_line	= dec_temp.bytes_per_line;
		vfmt.fmt.vbi.offset		= dec_temp.offset;
		vfmt.fmt.vbi.start[0]		= dec_temp.start[0];
		vfmt.fmt.vbi.count[0]		= dec_temp.count[0];
		vfmt.fmt.vbi.start[1]		= dec_temp.start[1];
		vfmt.fmt.vbi.count[1]		= dec_temp.count[1];

		if (v->pal_start1_fix
		    && 625 == v->dec.scanning)
			vfmt.fmt.vbi.start[1] -= 1;

		if (v->saa7134_ntsc_fix
		    && 525 == v->dec.scanning) {
			vfmt.fmt.vbi.start[0] = 10 + 6;
			vfmt.fmt.vbi.start[1] = 273 + 6;
		}

		if (v->do_trace)
			print_vfmt("VBI capture parameters requested: ", &vfmt);
		if ((v->has_try_fmt != 1) || commit) {
			s_fmt = VIDIOC_S_FMT;
			/* Arg type check requires constant cmd number. */
			r = xioctl (v, VIDIOC_S_FMT, &vfmt);
		} else {
			s_fmt = VIDIOC_TRY_FMT;
			r = xioctl (v, VIDIOC_TRY_FMT, &vfmt);
		}

		if (-1 == r) {
			switch (errno) {
			case EBUSY:
#ifndef REQUIRE_S_FMT
				if (g_fmt != -1) {
					printv("VIDIOC_S_FMT returned EBUSY, "
					       "will try the current parameters\n");

					vfmt = vfmt_temp;
					break;
				}
#endif
				vbi_asprintf(errstr, _("Cannot initialize %s (%s), "
						       "the device is already in use."),
					     v->p_dev_name, v->vcap.card);
				goto io_error;

			default:
				vbi_asprintf(errstr, _("Could not set the vbi capture parameters "
						       "for %s (%s): %d, %s."),
					     v->p_dev_name, v->vcap.card, errno, strerror(errno));
				guess = _("Possibly a driver bug.");
				goto io_error;
			}

			if (commit && (v->has_try_fmt == 1) && (v->dec.services != 0)) {
				/* FIXME strictness of services is not considered */
				unsigned int tmp_services =
					vbi_raw_decoder_check_services(&v->dec, v->dec.services, 0);
				if (v->dec.services != tmp_services)
					vbi_raw_decoder_remove_services(&v->dec, v->dec.services & ~ tmp_services);
			}

		} else {
			printv("Successfully %s vbi capture parameters\n",
	                       ((s_fmt == (int)VIDIOC_S_FMT) ? "set" : "tried"));
		}
	}

	if (v->do_trace)
		print_vfmt("VBI capture parameters granted: ", &vfmt);

	/* grow pattern array if necessary
	** note: must do this even if service add fails later, to stay in sync with driver */
	vbi_raw_decoder_resize(&v->dec, vfmt.fmt.vbi.start, vfmt.fmt.vbi.count);

	v->dec.sampling_rate		= vfmt.fmt.vbi.sampling_rate;
	v->dec.bytes_per_line		= vfmt.fmt.vbi.samples_per_line;
	v->dec.offset			= vfmt.fmt.vbi.offset;
	v->dec.start[0] 		= vfmt.fmt.vbi.start[0];
	v->dec.count[0] 		= vfmt.fmt.vbi.count[0];
	v->dec.start[1] 		= vfmt.fmt.vbi.start[1];

	if (v->pal_start1_fix
	    && 625 == v->dec.scanning
	    && 319 == v->dec.start[1])
		v->dec.start[1] += 1;

	v->dec.count[1] 		= vfmt.fmt.vbi.count[1];
	v->dec.interlaced		= !!(vfmt.fmt.vbi.flags
					     & V4L2_VBI_INTERLACED);
	v->dec.synchronous		= !(vfmt.fmt.vbi.flags
					    & V4L2_VBI_UNSYNC);
	v->time_per_frame 		= ((v->dec.scanning == 625)
					   ? 1.0 / 25 : 1001.0 / 30000);
	v->dec.sampling_format          = VBI_PIXFMT_YUV420;

 	if (vfmt.fmt.vbi.sample_format != V4L2_PIX_FMT_GREY) {
		vbi_asprintf(errstr, _("%s (%s) offers unknown vbi sampling format #%d. "
				       "This may be a driver bug or libzvbi is too old."),
			     v->p_dev_name, v->vcap.card, vfmt.fmt.vbi.sample_format);
		goto io_error;
	}

	if (services & ~(VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625)) {
		/* Nyquist (we're generous at 1.5) */

		if (v->dec.sampling_rate < max_rate * 3 / 2) {
			vbi_asprintf(errstr, _("Cannot capture the requested "
						 "data services with "
						 "%s (%s), the sampling frequency "
						 "%.2f MHz is too low."),
				     v->p_dev_name, v->vcap.card,
				     v->dec.sampling_rate / 1e6);
                        services = 0;
			goto failure;
		}

		printv("Nyquist check passed\n");

		printv("Request decoding of services 0x%08x, strict level %d\n", services, strict);

		/* those services which are already set must be checked for strictness */
		if ( (strict > 0) && ((services & v->dec.services) != 0) ) {
			unsigned int tmp_services;
			tmp_services = vbi_raw_decoder_check_services(&v->dec, services & v->dec.services, strict);
			/* mask out unsupported services */
			services &= tmp_services | ~(services & v->dec.services);
		}

		if ( (services & ~v->dec.services) != 0 )
	                services &= vbi_raw_decoder_add_services(&v->dec,
	                                                         services & ~ v->dec.services,
	                                                         strict);

		if (services == 0) {
			vbi_asprintf(errstr, _("Sorry, %s (%s) cannot capture any of "
					       "the requested data services."),
				     v->p_dev_name, v->vcap.card);
			goto failure;
		}

		if (v->sliced_buffer.data != NULL)
			free(v->sliced_buffer.data);

		v->sliced_buffer.data =
			malloc((v->dec.count[0] + v->dec.count[1]) * sizeof(vbi_sliced));

		if (!v->sliced_buffer.data) {
			vbi_asprintf(errstr, _("Virtual memory exhausted."));
			errno = ENOMEM;
			goto io_error;
		}
	}

failure:
        v->services |= services;
	printv ("Will capture services 0x%08x, "
		"added 0x%0x commit:%d\n",
		v->services, services, commit);

	if (commit && (v->services != 0)) {
		if (v->streaming) {
			if (v4l2_stream_alloc(v, errstr) != 0)
				goto io_error;
		} else {
			if (v4l2_read_alloc(v, errstr) != 0)
				goto io_error;
		}
	}

	return services;

io_error:
        printv ("v4l2-update_services: "
		"failed with errno=%d, msg='%s' guess='%s'\n",
		errno, *errstr, guess ? guess : "");

	return 0;
}

static vbi_raw_decoder *
v4l2_parameters(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	return &v->dec;
}

static void
v4l2_delete(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	if (v->streaming)
		v4l2_stream_stop(v);
	else
		v4l2_read_stop(v);

	vbi_raw_decoder_destroy(&v->dec);

	if (v->sliced_buffer.data)
		free(v->sliced_buffer.data);

	if (v->p_dev_name != NULL)
		free(v->p_dev_name);

	if (v->close_me && v->fd != -1)
		device_close (v->capture.sys_log_fp, v->fd);

	free(v);
}

static VBI_CAPTURE_FD_FLAGS
v4l2_get_fd_flags(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	VBI_CAPTURE_FD_FLAGS result;

        result = VBI_FD_IS_DEVICE | VBI_FD_HAS_SELECT;
        if (v->streaming)
                result |= VBI_FD_HAS_MMAP;

        return result;
}

static int
v4l2_get_fd(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	return v->fd;
}

static void
v4l2_flush(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	v->flush_frame_count = FLUSH_FRAME_COUNT;

	if (v->streaming)
		v4l2_stream_flush(vc);
	else
		v4l2_read_flush(vc);
}

/* document below */
vbi_capture *
vbi_capture_v4l2k_new		(const char *		dev_name,
				 int			fd,
				 int			buffers,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	char *guess = NULL;
	char *error = NULL;
	vbi_capture_v4l2 *v;

	pthread_once (&vbi_init_once, vbi_init);

	/* Needed to reopen device. */
	assert(dev_name != NULL);

	assert(buffers > 0);

	if (!errstr)
		errstr = &error;
	*errstr = NULL;

	if (!(v = calloc(1, sizeof(*v)))) {
		vbi_asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	vbi_raw_decoder_init (&v->dec);

	v->do_trace = trace;

	if (0)
		v->capture.sys_log_fp = stderr;

	printv ("Try to open V4L2 2.6 VBI device, "
		"libzvbi interface rev.\n  %s\n", rcsid);

	v->p_dev_name = strdup(dev_name);

	if (v->p_dev_name == NULL) {
		vbi_asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->capture.parameters = v4l2_parameters;
	v->capture._delete = v4l2_delete;
	v->capture.get_fd = v4l2_get_fd;
	v->capture.get_fd_flags = v4l2_get_fd_flags;
	v->capture.update_services = v4l2_update_services_raw;
	v->capture.get_scanning = v4l2_get_scanning;
	v->capture.flush = v4l2_flush;

	if (-1 == fd) {
		v->fd = device_open (v->capture.sys_log_fp,
				     v->p_dev_name, O_RDWR, 0);
		if (-1 == v->fd) {
			vbi_asprintf(errstr, _("Cannot open '%s': %d, %s."),
				     v->p_dev_name, errno, strerror(errno));
			goto io_error;
		}

		v->close_me = TRUE;

		printv("Opened %s\n", v->p_dev_name);
	} else {
		v->fd = fd;
		v->close_me = FALSE;

		printv("Using v4l2k device fd %d\n", fd);
	}

	if (-1 == xioctl (v, VIDIOC_QUERYCAP, &v->vcap)) {
		vbi_asprintf(errstr, _("Cannot identify '%s': %d, %s."),
			     v->p_dev_name, errno, strerror(errno));
		guess = _("Probably not a v4l2 device.");
		goto io_error;
	}

	if (v->vcap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) {
		printv ("%s (%s) is a v4l2 sliced vbi device,\n"
			"  driver %s, version 0x%08x\n",
			v->p_dev_name, v->vcap.card,
			v->vcap.driver, v->vcap.version);

		v->capture.update_services = v4l2_update_services_sliced;
	} else if (v->vcap.capabilities & V4L2_CAP_VBI_CAPTURE) {
		printv ("%s (%s) is a v4l2 raw vbi device,\n"
			"  driver %s, version 0x%08x\n",
			v->p_dev_name, v->vcap.card,
			v->vcap.driver, v->vcap.version);

		if (0 == strcmp ((char *) v->vcap.driver, "bttv")) {
			if (v->vcap.version <= 0x00090F)
				v->pal_start1_fix = TRUE;
		} else if (0 == strcmp ((char *) v->vcap.driver, "saa7134")) {
			if (v->vcap.version <= 0x00020C)
				v->saa7134_ntsc_fix = TRUE;

			v->pal_start1_fix = TRUE;
		}
	} else {
		vbi_asprintf(errstr, _("%s (%s) is not a vbi device."),
			     v->p_dev_name, v->vcap.card);
		goto failure;
	}

	v->has_try_fmt = -1;
	v->buf_req_count = buffers;

	if (v->vcap.capabilities & V4L2_CAP_STREAMING
	    && !vbi_capture_force_read_mode) {
		printv("Using streaming interface\n");

		fcntl(v->fd, F_SETFL, O_NONBLOCK);

		v->streaming = TRUE;
		v->enqueue = ENQUEUE_SUSPENDED;

		v->capture.read = v4l2_stream;

	} else if (v->vcap.capabilities & V4L2_CAP_READWRITE) {
		printv("Using read interface\n");

		v->capture.read = v4l2_read;

		v->read_active = FALSE;

	} else {
		vbi_asprintf(errstr,
			     _("%s (%s) lacks a vbi read interface, "
			       "possibly a driver bug."),
			     v->p_dev_name, v->vcap.card);
		goto failure;
	}

        v->services = 0;

	if (services != NULL) {
                assert(*services != 0);
                v->services =
			v->capture.update_services (&v->capture,
						    /* reset */ TRUE,
						    /* commit */ TRUE,
						    *services,
						    strict,
						    errstr);
                if (v->services == 0)
                        goto failure;

                *services = v->services;
        }

	printv("Successful opened %s (%s)\n",
	       v->p_dev_name, v->vcap.card);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return &v->capture;

 io_error:
 failure:
	if (v)
		v4l2_delete (&v->capture);

        printv ("v4l2k_new: failed with errno=%d, msg='%s' guess='%s'\n",
		errno, *errstr, guess ? guess : "");

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return NULL;
}

#else

/**
 * @param dev_name Name of the device to open, usually one of
 *   @c /dev/vbi or @c /dev/vbi0 and up.
 * @param fd File handle of VBI device if already opened by caller,
 *   else value -1.
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
 * @param errstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress messages on stderr.
 * 
 * @return
 * Initialized vbi_capture context, @c NULL on failure.
 */
vbi_capture *
vbi_capture_v4l2k_new(const char *dev_name, int fd, int buffers,
		      unsigned int *services, int strict,
		      char **errstr, vbi_bool trace)
{
	if (0) /* unused, no warning please */
		fputs (rcsid, stderr);

	pthread_once (&vbi_init_once, vbi_init);

	if (errstr)
		vbi_asprintf (errstr,
			      _("V4L2 driver interface not compiled."));

	return NULL;
}

#endif /* !ENABLE_V4L2 */
