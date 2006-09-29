/*
 *  libzvbi test
 *
 *  Copyright (C) 2000-2006 Michael H. Schimek
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

/* $Id: capture.c,v 1.25 2006-09-29 09:29:17 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

#include "src/dvb_mux.h"
#include "src/decoder.h"
#include "src/io.h"
#include "src/io-sim.h"
#include "src/hamm.h"

vbi_capture *		cap;
vbi_raw_decoder *	par;
vbi_dvb_mux *		mx;
vbi_bool		quit;
int			src_w, src_h;

int			dump;
int			dump_ttx;
int			dump_xds;
int			dump_cc;
int			dump_wss;
int			dump_vps;
int			dump_sliced;
int			bin_sliced;
int			bin_pes;
int			bin_ts;
int			do_read = TRUE;
int			do_sim;
int			ignore_error;
int			desync;
int			strict;

extern void
vbi_capture_set_log_fp		(vbi_capture *		capture,
				 FILE *			fp);
extern vbi_bool vbi_capture_force_read_mode;

/*
 *  Dump
 */

#define PIL(day, mon, hour, min) \
	(((day) << 15) + ((mon) << 11) + ((hour) << 6) + ((min) << 0))

static void
dump_pil(int pil)
{
	int day, mon, hour, min;

	day = pil >> 15;
	mon = (pil >> 11) & 0xF;
	hour = (pil >> 6) & 0x1F;
	min = pil & 0x3F;

	if (pil == PIL(0, 15, 31, 63))
		printf(" PDC: Timer-control (no PDC)\n");
	else if (pil == PIL(0, 15, 30, 63))
		printf(" PDC: Recording inhibit/terminate\n");
	else if (pil == PIL(0, 15, 29, 63))
		printf(" PDC: Interruption\n");
	else if (pil == PIL(0, 15, 28, 63))
		printf(" PDC: Continue\n");
	else if (pil == PIL(31, 15, 31, 63))
		printf(" PDC: No time\n");
	else
		printf(" PDC: %05x, 200X-%02d-%02d %02d:%02d\n",
			pil, mon, day, hour, min);
}

static void
decode_vps(uint8_t *buf)
{
	static char pr_label[20];
	static char label[20];
	static int l = 0;
	int cni, pcs, pty, pil;
	int c;

	if (!dump_vps)
		return;

	printf("\nVPS:\n");

	c = vbi_rev8 (buf[1]);

	if ((int8_t) c < 0) {
		label[l] = 0;
		memcpy(pr_label, label, sizeof(pr_label));
		l = 0;
	}

	c &= 0x7F;

	label[l] = _vbi_to_ascii (c);

	l = (l + 1) % 16;

	printf(" 3-10: %02x %02x %02x %02x %02x %02x %02x %02x (\"%s\")\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], pr_label);

	pcs = buf[2] >> 6;

	cni = + ((buf[10] & 3) << 10)
	      + ((buf[11] & 0xC0) << 2)
	      + ((buf[8] & 0xC0) << 0)
	      + (buf[11] & 0x3F);

	pil = ((buf[8] & 0x3F) << 14) + (buf[9] << 6) + (buf[10] >> 2);

	pty = buf[12];

	printf(" CNI: %04x PCS: %d PTY: %d ", cni, pcs, pty);

	dump_pil(pil);
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
		"<invalid>"
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
		int i;
		unsigned int j;

		printf("Sliced time: %f\n", time);

		for (i = 0; i < lines; q++, i++) {
			printf("%04x %3d > ", q->id, q->line);

			for (j = 0; j < sizeof (q->data); ++j) {
				printf("%02x ", (uint8_t) q->data[j]);
			}

			putchar(' ');

			for (j = 0; j < sizeof (q->data); ++j) {
				char c = _vbi_to_ascii (q->data[j]);
				putchar(c);
			}

			putchar('\n');
		}
	}

	for (; lines > 0; s++, lines--) {
		if (s->id == 0) {
			continue;
		} else if (s->id & VBI_SLICED_VPS) {
			decode_vps(s->data);
		} else if (s->id & VBI_SLICED_TELETEXT_B) {
			/* Use ./decode instead. */
		} else if (s->id & VBI_SLICED_CAPTION_525) {
			/* Use ./decode instead. */
		} else if (s->id & VBI_SLICED_CAPTION_625) {
			/* Use ./decode instead. */
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
	{ VBI_SLICED_VPS, 13 },
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

	fflush (stdout);
}

static vbi_bool
binary_ts_pes			(vbi_dvb_mux *		mx,
				 void *			user_data,
				 const uint8_t *	packet,
				 unsigned int		packet_size)
{
	mx = mx;
	user_data = user_data;

	fwrite (packet, 1, packet_size, stdout);

	fflush (stdout);

	return TRUE;
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
	int64_t pts;

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	raw = malloc(src_w * src_h);
	sliced = malloc(sizeof(vbi_sliced) * src_h);

	assert(raw && sliced);

	for (quit = FALSE; !quit;) {
		int r;

		if (do_read) {
			r = vbi_capture_read(cap, raw, sliced,
					     &lines, &timestamp, &tv);
		} else {
			r = vbi_capture_pull(cap, &raw_buffer,
					     &sliced_buffer, &tv);
		}

		if (0) {
			fputc ('.', stdout);
			fflush (stdout);
		}
 
		switch (r) {
		case -1:
			fprintf(stderr, "VBI read error: %d, %s%s\n",
				errno, strerror(errno),
				ignore_error ? " (ignored)" : "");
			if (ignore_error)
				continue;
			else
				exit(EXIT_FAILURE);
		case 0: 
			fprintf(stderr, "VBI read timeout%s\n",
				ignore_error ? " (ignored)" : "");
			if (ignore_error)
				continue;
			else
				exit(EXIT_FAILURE);
		case 1:
			break;
		default:
			assert(!"reached");
		}

		if (!do_read) {
			sliced = sliced_buffer->data;
			lines = sliced_buffer->size / sizeof (vbi_sliced);
			timestamp = sliced_buffer->timestamp;
		}

		if (dump)
			decode_sliced(sliced, timestamp, lines);
		if (bin_sliced)
			binary_sliced(sliced, timestamp, lines);
		if (bin_pes || bin_ts) {
			/* XXX shouldn't use system time. */
			pts = (int64_t)(timestamp * 90000.0);
			_vbi_dvb_mux_feed (mx, pts, sliced, lines,
					   /* service_set: all */ -1);
		}
	}
}

static const char short_options[] = "123cd:elnpr:stvPT";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options[] = {
	{ "desync",	no_argument,		NULL,		'c' },
	{ "device",	required_argument,	NULL,		'd' },
	{ "ignore-error", no_argument,		NULL,		'e' },
	{ "pid",	required_argument,	NULL,		'i' },
	{ "dump-ttx",	no_argument,		NULL,		't' },
	{ "dump-xds",	no_argument,		&dump_xds,	TRUE },
	{ "dump-cc",	no_argument,		&dump_cc,	TRUE },
	{ "dump-wss",	no_argument,		&dump_wss,	TRUE },
	{ "dump-vps",	no_argument,		&dump_vps,	TRUE },
	{ "dump-sliced",no_argument,		&dump_sliced,	TRUE },
	{ "pes",	no_argument,		NULL,		'P' },
	{ "sliced",	no_argument,		NULL,		'l' },
	{ "ts",		no_argument,		NULL,		'T' },
	{ "read",	no_argument,		&do_read,	TRUE },
	{ "pull",	no_argument,		&do_read,	FALSE },
	{ "strict",	required_argument,	NULL,		'r' },
	{ "sim",	no_argument,		NULL,		's' },
	{ "ntsc",	no_argument,		NULL,		'n' },
	{ "pal",	no_argument,		NULL,		'p' },
	{ "v4l",	no_argument,		NULL,		'1' },
	{ "v4l2-read",	no_argument,		NULL,		'2' },
	{ "v4l2-mmap",	no_argument,		NULL,		'3' },
	{ "verbose",	no_argument,		NULL,		'v' },
	{ 0, 0, 0, 0 }
};
#else
#define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

int
main(int argc, char **argv)
{
	char *dev_name;
	int pid = -1;
	char *errstr;
	unsigned int services;
	int scanning = 625;
	int strict;
	int verbose = 0;
	int c, index;
	int interface = 0;

	dev_name = strdup ("/dev/vbi");
	assert (NULL != dev_name);

	while ((c = getopt_long(argc, argv, short_options,
				long_options, &index)) != -1)
		switch (c) {
		case 0: /* set flag */
			break;
		case '2':
			/* Preliminary hack for tests. */
			vbi_capture_force_read_mode = TRUE;
			/* fall through */
		case '1':
		case '3':
			interface = c - '0';
			break;
		case 'c':
			desync ^= TRUE;
			break;
		case 'd':
			free (dev_name);
			dev_name = strdup (optarg);
			assert (NULL != dev_name);
			break;
		case 'e':
			ignore_error ^= TRUE;
			break;
		case 'i':
			pid = atoi (optarg);
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
		case 'P':
			bin_pes ^= TRUE;
			break;
		case 'r':
			strict = strtol (optarg, NULL, 0);
			if (strict < -1)
				strict = -1;
			else if (strict > 2)
				strict = 2;
			break;
		case 's':
			do_sim ^= TRUE;
			break;
		case 't':
			dump_ttx ^= TRUE;
			break;
		case 'T':
			bin_ts ^= TRUE;
			break;
		case 'v':
			++verbose;
			break;
		default:
			fprintf(stderr, "Unknown option\n");
			exit(EXIT_FAILURE);
		}

	if (dump_ttx | dump_cc | dump_xds) {
		fprintf (stderr, "\
Teletext, CC and XDS decoding are no longer supported by this tool.\n\
Run  ./capture --sliced | ./decode --ttx --cc --xds  instead.\n");
		exit (EXIT_FAILURE);
	}

	dump = dump_wss | dump_vps | dump_sliced;

	services = VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625
		| VBI_SLICED_TELETEXT_B | VBI_SLICED_CAPTION_525
		| VBI_SLICED_CAPTION_625 | VBI_SLICED_VPS
		| VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;

	if (do_sim) {
		cap = vbi_capture_sim_new (scanning, &services,
					   /* interlaced */ FALSE, !desync);
		assert ((par = vbi_capture_parameters(cap)));
	} else {
		do {
			if (-1 != pid) {
				cap = vbi_capture_dvb_new (dev_name,
							   scanning,
							   &services,
							   strict,
							   &errstr,
							   !!verbose);
				if (cap) {
					vbi_capture_dvb_filter (cap, pid);
					break;
				}

				fprintf (stderr, "Cannot capture vbi data "
					 "with DVB interface:\n%s\n", errstr);

				free (errstr);

				exit(EXIT_FAILURE);
			}

			if (1 != interface) {
				cap = vbi_capture_v4l2_new (dev_name,
							    /* buffers */ 5,
							    &services,
							    strict,
							    &errstr,
							    /* trace */
							    !!verbose);
				if (cap)
					break;

				fprintf (stderr, "Cannot capture vbi data "
					 "with v4l2 interface:\n%s\n", errstr);

				free (errstr);
			}

			if (interface < 2) {
				cap = vbi_capture_v4l_new (dev_name,
							   scanning,
							   &services,
							   strict,
							   &errstr,
							   /* trace */
							   !!verbose);
				if (cap)
					break;

				fprintf (stderr, "Cannot capture vbi data "
					 "with v4l interface:\n%s\n", errstr);

				free (errstr);
			}

			/* BSD interface */
			if (1) {
				cap = vbi_capture_bktr_new (dev_name,
							    scanning,
							    &services,
							    strict,
							    &errstr,
							    /* trace */
							    !!verbose);
				if (cap)
					break;

				fprintf (stderr, "Cannot capture vbi data "
					 "with bktr interface:\n%s\n", errstr);

				free (errstr);
			}

			exit(EXIT_FAILURE);
		} while (0);

		assert ((par = vbi_capture_parameters(cap)));
	}

	if (verbose > 1) {
		vbi_capture_set_log_fp (cap, stderr);
	}

	if (-1 == pid)
		assert (par->sampling_format == VBI_PIXFMT_YUV420);

	src_w = par->bytes_per_line / 1;
	src_h = par->count[0] + par->count[1];

	if (bin_pes) {
		mx = _vbi_dvb_mux_pes_new (/* data_identifier */ 0x10,
					   /* packet_size */ 8 * 184,
					   VBI_VIDEOSTD_SET_625_50,
					   binary_ts_pes,
					   /* user_data */ NULL);
		assert (NULL != mx);
	} else if (bin_ts) {
		mx = _vbi_dvb_mux_ts_new (/* pid */ 999,
					  /* data_identifier */ 0x10,
					  /* packet_size */ 8 * 184,
					  VBI_VIDEOSTD_SET_625_50,
					  binary_ts_pes,
					  /* user_data */ NULL);
		assert (NULL != mx);
	}

	mainloop();

	if (!do_sim)
		vbi_capture_delete(cap);

	exit(EXIT_SUCCESS);	
}
