/*
 *  libzvbi WSS capture test / example
 *
 *  Copyright (C) 2005 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

/* $Id: wss.c,v 1.4 2005-05-25 02:27:06 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef ENABLE_V4L2

#include <fcntl.h>		/* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include "src/libzvbi.h"

#include <asm/types.h>		/* for videodev2.h */
#include "src/videodev2k.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))

struct buffer {
	void *			start;
	size_t			length;
};

static char *		my_name;
static char *		dev_name;
static unsigned int	verbose;
static int		fd;
struct buffer *		buffers;
static unsigned int	n_buffers;
static int		quit;
static vbi_raw_decoder	rd;

static void
errno_exit			(const char *		s)
{
	fprintf (stderr, "%s error %d, %s\n",
		 s, errno, strerror (errno));

	exit (EXIT_FAILURE);
}

static int
xioctl				(int			fd,
				 int			request,
				 void *			p)
{
	int r;

	do r = ioctl (fd, request, p);
	while (-1 == r && EINTR == errno);

	return r;
}

static void
decode_wss_625			(uint8_t *		buf)
{
	static const char *formats [] = {
		"Full format 4:3, 576 lines",
		"Letterbox 14:9 centre, 504 lines",
		"Letterbox 14:9 top, 504 lines",
		"Letterbox 16:9 centre, 430 lines",
		"Letterbox 16:9 top, 430 lines",
		"Letterbox > 16:9 centre",
		"Full format 14:9 centre, 576 lines",
		"Anamorphic 16:9, 576 lines"
	};
	static const char *subtitles [] = {
		"none",
		"in active image area",
		"out of active image area",
		"<invalid>"
	};
	int g1;
	int parity;

	g1 = buf[0] & 15;

	parity = g1;
	parity ^= parity >> 2;
	parity ^= parity >> 1;
	g1 &= 7;

	printf ("WSS PAL: ");
	if (!(parity & 1))
		printf ("<parity error> ");
	printf ("%s; %s mode; %s colour coding; %s helper; "
		"reserved b7=%d; %s Teletext subtitles; "
		"open subtitles: %s; %s surround sound; "
		"copyright %s; copying %s\n",
		formats[g1],
		(buf[0] & 0x10) ? "film" : "camera",
		(buf[0] & 0x20) ? "MA/CP" : "standard",
		(buf[0] & 0x40) ? "modulated" : "no",
		!!(buf[0] & 0x80),
		(buf[1] & 0x01) ? "have" : "no",
		subtitles[(buf[1] >> 1) & 3],
		(buf[1] & 0x08) ? "have" : "no",
		(buf[1] & 0x10) ? "asserted" : "unknown",
		(buf[1] & 0x20) ? "restricted" : "not restricted");
}

static void
process_image			(const void *		p)
{
	vbi_sliced sliced[1];
	unsigned int n_lines;

	n_lines = vbi_raw_decode (&rd, (uint8_t *) p, sliced);
	if (n_lines > 0) {
		assert (VBI_SLICED_WSS_625 == sliced[0].id);
		decode_wss_625 (sliced[0].data);
	} else {
		fputc ('.', stdout);
		fflush (stdout);
	}
}

static void
init_decoder			(void)
{
	unsigned int services;

	vbi_raw_decoder_init (&rd);

	rd.scanning = 625;
	rd.sampling_format = VBI_PIXFMT_YUYV;

	/* Should be calculated from VIDIOC_CROPCAP information.
	   Common sampling rates are 14.75 MHz to get 768 PAL/SECAM
	   square pixels per line, and 13.5 MHz according to ITU-R Rec.
           BT.601, 720 pixels/line. Note BT.601 overscans the line:
	   13.5e6 / 720 > 14.75e6 / 768. Don't be fooled by a driver
	   scaling 768 square pixels to 720. */
	rd.sampling_rate = 768 / 768 * 14750000;

	/* Misnamed, should be samples_per_line. */
	rd.bytes_per_line = 768;

	/* Should be calculated from VIDIOC_CROPCAP information. */
	rd.offset = 0; //6.8e-6 * rd.sampling_rate;

	rd.start[0] = 23;
	rd.count[0] = 1;

	rd.start[1] = 0;
	rd.count[1] = 0;

	rd.interlaced = FALSE; /* just one line */
	rd.synchronous = TRUE;

	services = vbi_raw_decoder_add_services (&rd,
						 VBI_SLICED_WSS_625,
						 /* strict */ 0);
	if (0 == services) {
		fprintf (stderr, "Cannot decode WSS\n");
		exit (EXIT_FAILURE);
	}
}

static void
mainloop			(void)
{
	quit = 0;

	while (!quit) {
		struct v4l2_buffer buf;

		for (;;) {
			fd_set fds;
			struct timeval tv;
			int r;

			FD_ZERO (&fds);
			FD_SET (fd, &fds);

			tv.tv_sec = 2;
			tv.tv_usec = 0;

			r = select (fd + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				if (EINTR == errno) {
					/* XXX should subtract the elapsed
					   time from timeout here. */
					continue;
				}

				errno_exit ("select");
			}

			if (0 == r) {
				fprintf (stderr, "select timeout\n");
				exit (EXIT_FAILURE);
			}

			break;
		}

		CLEAR (buf);

		buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory	= V4L2_MEMORY_MMAP;

		if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
			if (EAGAIN == errno)
				continue;

			errno_exit ("VIDIOC_DQBUF");
		}

		assert (buf.index < n_buffers);

		process_image (buffers[buf.index].start);

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
			errno_exit ("VIDIOC_QBUF");
	}
}

static void
start_capturing			(void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory	= V4L2_MEMORY_MMAP;
	        buf.index	= i;

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
			errno_exit ("VIDIOC_QBUF");
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
		errno_exit ("VIDIOC_STREAMON");
}

static void
init_device			(void)
{
	struct v4l2_capability cap;
	v4l2_std_id std_id;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;

	if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s is no V4L2 device\n",
				 dev_name);
			exit (EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf (stderr, "%s is no video capture device\n",
			 dev_name);
		exit (EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf (stderr, "%s does not support streaming I/O\n",
			 dev_name);
		exit (EXIT_FAILURE);
	}

	std_id = V4L2_STD_PAL;

	if (-1 == xioctl (fd, VIDIOC_S_STD, &std_id))
		errno_exit ("VIDIOC_S_STD");

	CLEAR (fmt);

	/* We need the top field without vertical scaling,
	   width must be at least 320 pixels. */

	fmt.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width	= 768; 
	fmt.fmt.pix.height	= 576;
	fmt.fmt.pix.pixelformat	= V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field	= V4L2_FIELD_INTERLACED;

	if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
		errno_exit ("VIDIOC_S_FMT");

	/* XXX the driver may adjust width and height, some
	   even change the pixelformat, that should be checked here. */

	CLEAR (req);

	req.count		= 4;
	req.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory		= V4L2_MEMORY_MMAP;

	if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s does not support "
				 "memory mapping\n", dev_name);
			exit (EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf (stderr, "Insufficient buffer memory on %s\n",
			 dev_name);
		exit (EXIT_FAILURE);
	}

	buffers = calloc (req.count, sizeof (*buffers));

	if (!buffers) {
		fprintf (stderr, "Out of memory\n");
		exit (EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory	= V4L2_MEMORY_MMAP;
	        buf.index	= n_buffers;

		if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
			errno_exit ("VIDIOC_QUERYBUF");

	        buffers[n_buffers].length = buf.length;
	        buffers[n_buffers].start =
			mmap (NULL /* start anywhere */,
			      buf.length,
			      PROT_READ | PROT_WRITE /* required */,
			      MAP_SHARED /* recommended */,
			      fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit ("mmap");
	}
}

static void
open_device			(void)
{
	struct stat st;	

	if (-1 == stat (dev_name, &st)) {
		fprintf (stderr, "Cannot identify '%s': %d, %s\n",
		         dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}

	if (!S_ISCHR (st.st_mode)) {
		fprintf (stderr, "%s is no device\n", dev_name);
		exit (EXIT_FAILURE);
	}

	fd = open (dev_name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf (stderr, "Cannot open '%s': %d, %s\n",
		         dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}
}

static void
usage				(FILE *			fp)
{
	fprintf (fp,
		 "Test/demo app extracting Wide Screen Signalling\n"
		 "(EN 300 294) from video images.  PAL/SECAM only.\n"
		 "May not work with all devices.\n\n"
		 "Usage: %s [options]\n\n"
		 "Options:\n"
		 "-d | --device name   Video device name (/dev/video)\n"
		 "-h | --help          Print this message\n"
		 "-v | --verbose       Increase verbosity\n"
		 "\n"
		 "(--long options only available on GNU & compatible.)\n",
		 my_name);
}

static const char short_options[] = "d:hv";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options [] = {
	{ "device",	required_argument,	NULL,		'd' },
	{ "help",	no_argument,		NULL,		'h' },
	{ "verbose",	no_argument,		NULL,		'v' },
	{ 0, 0, 0, 0 }
};
#else
#  define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

int
main				(int			argc,
				 char **		argv)
{
	int index;
	int c;

	my_name = argv[0];

	dev_name = strdup ("/dev/video");
	assert (NULL != dev_name);

	while (-1 != (c = getopt_long (argc, argv, short_options,
				       long_options, &index))) {
		switch (c) {
		case 0: /* set flag */
			break;

		case 'd':
			free (dev_name);
			dev_name = strdup (optarg);
			assert (NULL != dev_name);
			break;

		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case 'v':
			++verbose;
			break;

		default:
			usage (stderr);
			exit (EXIT_FAILURE);
		}
	}

	open_device ();
	init_device ();
	init_decoder ();
	start_capturing ();
	mainloop ();
	exit (EXIT_SUCCESS);

	return 0;
}

#else /* !ENABLE_V4L2 */

int
main				(int			argc,
				 char **		argv)
{
	fprintf (stderr, "Sorry, V4L2 only. Patches welcome.\n");
	exit (EXIT_FAILURE);
	
	return 0;
}

#endif /* !ENABLE_V4L2 */
