/*
 *  libzvbi test
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
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

/* $Id: capture.c,v 1.7.2.3 2004-02-25 17:27:36 mschimek Exp $ */

#undef NDEBUG

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

#include <libzvbi.h>

vbi_capture *		cap;
vbi_raw_decoder *	rd;
vbi_sampling_par	sp;
vbi_bool		quit;
int			src_w, src_h;

int			dump;
int			dump_xds;
int			dump_cc;
int			dump_wss;
int			dump_sliced;
int			bin_sliced;
int			do_read = TRUE;
int			do_sim;

#include "sim.c"

/*
 *  Dump
 */

static inline int
odd_parity(uint8_t c)
{
	c ^= (c >> 4);
	c ^= (c >> 2);
	c ^= (c >> 1);

	return c & 1;
}

static inline int
vbi_hamm16(uint8_t *p)
{
	return vbi_iham16p (p);
}

#define printable(c) ((((c) & 0x7F) < 0x20 || ((c) & 0x7F) > 0x7E) ? '.' : ((c) & 0x7F))

static void
decode_xds(uint8_t *buf)
{
	if (dump_xds) {
		char c;

		c = odd_parity(buf[0]) ? buf[0] & 0x7F : '?';
		c = printable(c);
		putchar(c);
		c = odd_parity(buf[1]) ? buf[1] & 0x7F : '?';
		c = printable(c);
		putchar(c);
		fflush(stdout);
	}
}

static void
decode_caption(uint8_t *buf, int line)
{
	static vbi_bool xds_transport = FALSE;
	char c = buf[0] & 0x7F;

	if (line >= 280) { /* field 2 */
		/* 0x01xx..0x0Exx ASCII_or_NUL[0..32] 0x0Fchks */
		if (odd_parity(buf[0]) && (c >= 0x01 && c <= 0x0F)) {
			decode_xds(buf);
			xds_transport = (c != 0x0F);
		} else if (xds_transport) {
			decode_xds(buf);
		}

		return;
	}

	if (dump_cc) {
		c = odd_parity(buf[0]) ? buf[0] & 0x7F : '?';
		c = printable(c);
		putchar(c);
		c = odd_parity(buf[1]) ? buf[1] & 0x7F : '?';
		c = printable(c);
		putchar(c);
		fflush(stdout);
	}
}

static void
decode_wss_625(uint8_t *buf)
{
	static const char *formats[] = {
		"Full format 4:3, 576 lines",
		"Letterbox 14:9 centre, 504 lines",
		"Letterbox 14:9 top, 504 lines",
		"Letterbox 16:9 centre, 430 lines",
		"Letterbox 16:9 top, 430 lines",
		"Letterbox > 16:9 centre",
		"Full format 14:9 centre, 576 lines",
		"Anamorphic 16:9, 576 lines"
	};
	static const char *subtitles[] = {
		"none",
		"in active image area",
		"out of active image area",
		"?"
	};
	int g1 = buf[0] & 15;
	int parity;

	if (dump_wss) {
		parity = g1;
		parity ^= parity >> 2;
		parity ^= parity >> 1;
		g1 &= 7;

		printf("WSS PAL: ");
		if (!(parity & 1))
			printf("<parity error> ");
		printf("%s; %s mode; %s colour coding;\n"
		       "      %s helper; reserved b7=%d; %s\n"
		       "      open subtitles: %s; %scopyright %s; copying %s\n",
		       formats[g1],
		       (buf[0] & 0x10) ? "film" : "camera",
		       (buf[0] & 0x20) ? "MA/CP" : "standard",
		       (buf[0] & 0x40) ? "modulated" : "no",
		       !!(buf[0] & 0x80),
		       (buf[1] & 0x01) ? "have TTX subtitles; " : "",
		       subtitles[(buf[1] >> 1) & 3],
		       (buf[1] & 0x08) ? "surround sound; " : "",
		       (buf[1] & 0x10) ? "asserted" : "unknown",
		       (buf[1] & 0x20) ? "restricted" : "not restricted");
	}
}

static void
decode_wss_cpr1204(uint8_t *buf)
{
	const int poly = (1 << 6) + (1 << 1) + 1;
	int g = (buf[0] << 12) + (buf[1] << 4) + buf[2];
	int j, crc;

	if (dump_wss) {
		crc = g | (((1 << 6) - 1) << (14 + 6));

		for (j = 14 + 6 - 1; j >= 0; j--) {
			if (crc & ((1 << 6) << j))
				crc ^= poly << j;
		}

		fprintf(stderr, "WSS CPR >> g=%08x crc=%08x\n", g, crc);
	}
}

static void
decode_sliced(vbi_sliced *s, double time, int lines)
{
	if (dump_sliced) {
		vbi_sliced *q = s;
		int i, j;

		printf("Sliced time: %f\n", time);

		for (i = 0; i < lines; q++, i++) {
			printf("%04x %3d > ", q->id, q->line);

			for (j = 0; j < sizeof(q->data); j++) {
				printf("%02x ", (uint8_t) q->data[j]);
			}

			putchar(' ');

			for (j = 0; j < sizeof(q->data); j++) {
				char c = printable(q->data[j]);
				putchar(c);
			}

			putchar('\n');
		}
	}

	for (; lines > 0; s++, lines--) {
		if (s->id == 0) {
			continue;
		} else if (s->id & VBI_SLICED_VPS) {
		} else if (s->id & VBI_SLICED_TELETEXT_B) {
		} else if (s->id & VBI_SLICED_CAPTION_525) {
			decode_caption(s->data, s->line);
		} else if (s->id & VBI_SLICED_CAPTION_625) {
			decode_caption(s->data, s->line);
		} else if (s->id & VBI_SLICED_WSS_625) {
			decode_wss_625(s->data);
		} else if (s->id & VBI_SLICED_WSS_CPR1204) {
			decode_wss_cpr1204(s->data);
		} else {
			fprintf(stderr, "Oops. Unhandled vbi service %08x\n",
				s->id);
		}
	}
}

/*
 *  Sliced, binary
 */

/* hysterical compatibility */
const int
services[][2] = {
	{ VBI_SLICED_TELETEXT_B, 42 },
	{ VBI_SLICED_CAPTION_625, 2 },
	{ VBI_SLICED_VPS | VBI_SLICED_VPS_F2, 13 },
	{ VBI_SLICED_WSS_625, 2 },
	{ VBI_SLICED_WSS_CPR1204, 3 },
	{ 0, 0 },
	{ 0, 0 },
	{ VBI_SLICED_CAPTION_525, 2 }
};

static void
binary_sliced(vbi_sliced *s, double time, int lines)
{
	static double last = 0.0;
	int i;

	if (last > 0.0)
		printf("%f\n%c", time - last, lines);
	else
		printf("%f\n%c", 0.04, lines);

	for (; lines > 0; s++, lines--) {
		for (i = 0; i < 8; i++) {
			if (s->id & services[i][0]) {
				printf("%c%c%c", i,
				       s->line & 0xFF, s->line >> 8);
				fwrite(s->data, 1, services[i][1], stdout);
				last = time;
				break;
			}
		}
	}
}

static void
mainloop(void)
{
	uint8_t	*raw;
	vbi_sliced *sliced;
	int lines;
	vbi_capture_buffer *raw_buffer;
	vbi_capture_buffer *sliced_buffer;
	double timestamp;
	struct timeval tv;

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	raw = malloc(src_w * src_h);
	sliced = malloc(sizeof(vbi_sliced) * src_h);

	assert(raw && sliced);

	for (quit = FALSE; !quit;) {
		int r;

		if (do_sim) {
			read_sim(raw, sliced, &lines, &timestamp);
			r = 1;
		} else if (do_read) {
			r = vbi_capture_read(cap, raw, sliced,
					     &lines, &timestamp, &tv);
		} else {
			r = vbi_capture_pull(cap, &raw_buffer,
					     &sliced_buffer, &tv);
		}

		switch (r) {
		case -1:
			fprintf(stderr, "VBI read error: %d, %s\n",
				errno, strerror(errno));
			exit(EXIT_FAILURE);
		case 0: 
			fprintf(stderr, "VBI read timeout\n");
			exit(EXIT_FAILURE);
		case 1:
			break;
		default:
			assert(!"reached");
		}

		if (do_read) {
			if (dump)
				decode_sliced(sliced, timestamp, lines);
			if (bin_sliced)
				binary_sliced(sliced, timestamp, lines);
		} else {
			if (dump)
				decode_sliced(sliced_buffer->data,
					      sliced_buffer->timestamp,
					      sliced_buffer->size
					      / sizeof(vbi_sliced));
			if (bin_sliced)
				binary_sliced(sliced_buffer->data,
					      sliced_buffer->timestamp,
					      sliced_buffer->size
					      / sizeof(vbi_sliced));
		}
	}
}

static void
log	       			(const char *		function,
				 const char *		message,
				 void *			user_data)
{
	fprintf (stderr, "%s: %s\n", function, message);
}


static const char short_options[] = "d:lnpstv";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options[] = {
	{ "device",	required_argument,	NULL,		'd' },
	{ "dump-xds",	no_argument,		&dump_xds,	TRUE },
	{ "dump-cc",	no_argument,		&dump_cc,	TRUE },
	{ "dump-wss",	no_argument,		&dump_wss,	TRUE },
	{ "dump-sliced",no_argument,		&dump_sliced,	TRUE },
	{ "sliced",	no_argument,		NULL,		'l' },
	{ "read",	no_argument,		&do_read,	TRUE },
	{ "pull",	no_argument,		&do_read,	FALSE },
	{ "sim",	no_argument,		NULL,		's' },
	{ "ntsc",	no_argument,		NULL,		'n' },
	{ "pal",	no_argument,		NULL,		'p' },
	{ "verbose",	no_argument,		NULL,		'v' },
	{ 0, 0, 0, 0 }
};
#else
#define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

int
main(int argc, char **argv)
{
	char *dev_name = "/dev/vbi";
	char *errstr;
	unsigned int services;
	int scanning = 625;
	vbi_bool verbose = FALSE;
	int c, index;

	while ((c = getopt_long(argc, argv, short_options,
				long_options, &index)) != -1) {
		switch (c) {
		case 0: /* set flag */
			break;
		case 'd':
			dev_name = optarg;
			break;
		case 'l':
			bin_sliced ^= TRUE;
			break;
		case 'n':
			scanning = 525;
			break;
		case 'p':
			scanning = 625;
			break;
		case 's':
			do_sim ^= TRUE;
			break;
		case 'v':
			verbose ^= TRUE;
			break;
		default:
			fprintf(stderr, "Unknown option\n");
			exit(EXIT_FAILURE);
		}
	}

	dump = dump_xds | dump_cc
		| dump_wss | dump_sliced;

	services = VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625
		| VBI_SLICED_TELETEXT_B | VBI_SLICED_CAPTION_525
		| VBI_SLICED_CAPTION_625
	  	| VBI_SLICED_VPS | VBI_SLICED_VPS_F2
		| VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;

	if (verbose)
		vbi_set_log_fn (log, NULL);

	if (do_sim) {
		rd = init_sim (scanning, services);
	} else {
		do {
			cap = vbi_capture_v4l2_new (dev_name,
						    /* buffers */ 5,
						    &services,
						    /* strict */ 0,
						    &errstr,
						    /* trace */ verbose);
			if (cap)
				break;

			fprintf (stderr, "Cannot capture vbi data "
				 "with v4l2 interface:\n%s\n", errstr);

			free (errstr);

			cap = vbi_capture_v4l_new (dev_name,
						   scanning,
						   &services,
						   /* strict */ 0,
						   &errstr,
						   /* trace */ verbose);
			if (cap)
				break;

			fprintf (stderr, "Cannot capture vbi data "
				 "with v4l interface:\n%s\n", errstr);

			free (errstr);

			cap = vbi_capture_bktr_new (dev_name,
						    scanning,
						    &services,
						    /* strict */ 0,
						    &errstr,
						    /* trace */ verbose);
			if (cap)
				break;

			fprintf (stderr, "Cannot capture vbi data "
				 "with bktr interface:\n%s\n", errstr);

			free (errstr);

			exit(EXIT_FAILURE);
		} while (0);

		assert ((rd = vbi_capture_parameters(cap)));
	}

	vbi_raw_decoder_get_sampling_par (rd, &sp);

	assert (sp.sampling_format == VBI_PIXFMT_YUV420);

	src_w = sp.bytes_per_line / 1;
	src_h = sp.count[0] + sp.count[1];

	mainloop();

	if (!do_sim)
		vbi_capture_delete(cap);

	exit(EXIT_SUCCESS);	
}
