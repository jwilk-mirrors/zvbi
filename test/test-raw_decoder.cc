/*
 *  libzvbi test
 *  Copyright (C) 2004 Michael H. Schimek
 */

/* $Id: test-raw_decoder.cc,v 1.1.2.6 2006-05-14 14:14:12 mschimek Exp $ */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/zvbi.h"

#define N_ELEMENTS(array) (sizeof (array) / sizeof (*(array)))

bool verbose;

typedef struct {
	unsigned int		service;
	unsigned int		first;
	unsigned int		last;
} block;

#define BLOCK_END { 0, 0, 0 }

static void *
memset_rand			(void *			d1,
				 size_t			n)
{
	uint8_t *d = (uint8_t *) d1;

	while (n-- > 0)
		*d++ = rand ();

	return d1;
}

static void
dump_hex			(const uint8_t *	p,
				 unsigned int 		n)
{
	while (n-- > 0)
		fprintf (stderr, "%02x ", *p++);
}

static void
sliced_rand_lines		(const vbi3_sliced *	s_start,
				 const vbi3_sliced *	s_end,
				 vbi3_sliced *		s,
				 unsigned int		service,
				 unsigned int		first_line,
				 unsigned int		last_line)
{
	unsigned int line;

	for (line = first_line; line <= last_line; ++line) {
		const vbi3_sliced *t;

		assert (s < s_end);

		for (t = s_start; t < s; ++t)
			assert (t->line != line);

		s->id = service;
		s->line = line;

		memset_rand (s->data, sizeof (s->data));

		++s;
	}
}

static unsigned int
sliced_rand			(vbi3_sliced *		s,
				 unsigned int		s_lines,
				 const block *		b)
{
	const vbi3_sliced *s_start;
	unsigned int services;

	s_start = s;
	services = 0;

	while (b->service) {
		services |= b->service;

		if (b->first > 0) {
			sliced_rand_lines (s_start, s_start + s_lines, s,
					   b->service, b->first, b->last);

			s += b->last - b->first + 1;
		}

		++b;
	}

	if (0)
		fprintf (stderr, "services 0x%08x\n", services);

	return s - s_start;
}

static unsigned int
create_raw			(uint8_t **		raw,
				 vbi3_sliced **		sliced,
				 const vbi3_sampling_par *sp,
				 const block *		b,
				 unsigned int		pixel_mask)
{
	unsigned int scan_lines;
	unsigned int sliced_lines;

	scan_lines = sp->count[0] + sp->count[1];

	*raw = (uint8_t *) malloc (sp->bytes_per_line * scan_lines);
	assert (NULL != *raw);

	*sliced = (vbi3_sliced *) malloc (sizeof (**sliced) * 50);
	assert (NULL != *sliced);

	sliced_lines = sliced_rand (*sliced, 50, b);

	if (pixel_mask) {
		memset_rand (*raw, sp->bytes_per_line * scan_lines);

		assert (_vbi3_test_image_video (*raw, 0, sp, pixel_mask,
					      *sliced, sliced_lines));
	} else {
		assert (_vbi3_test_image_vbi (*raw, 0, sp,
					     *sliced, sliced_lines));
	}

	return sliced_lines;
}

static vbi3_raw_decoder *
create_decoder			(const vbi3_sampling_par *sp,
				 const block *		b,
				 unsigned int		strict)
{
	vbi3_raw_decoder *rd;
	unsigned int services;

	services = 0;

	while (b->service) {
		services |= b->service;
		++b;
	}

	assert (NULL != (rd = vbi3_raw_decoder_new (sp)));

	assert (services == vbi3_raw_decoder_add_services
		(rd, services, strict));

	return rd;
}

static void
compare_sliced			(const vbi3_sliced *	in,
				 const vbi3_sliced *	out,
				 const vbi3_sliced *	old,
				 unsigned int		in_lines,
				 unsigned int		out_lines,
				 unsigned int		old_lines)
{
	unsigned int i;
	unsigned int min;
	const vbi3_sliced *s;

	min = 0;

	for (i = 0; i < out_lines; ++i) {
		unsigned int payload;

		/* Ascending line numbers */
		assert (out[i].line > min);
		min = out[i].line;

		/* Valid service id */
		assert (0 != out[i].id);
		payload = (vbi3_sliced_payload_bits (out[i].id) + 7) >> 3;
		assert (payload > 0);

		/* vbi3_sliced big enough */
		assert (payload <= sizeof (out[i].data));

		/* Writes more than payload */
		assert (0 == memcmp (out[i].data + payload,
				     old[i].data + payload,
				     sizeof (out[i].data) - payload));
	}

	/* Respects limits */
	assert (0 == memcmp (out + out_lines,
			     old + out_lines,
			     sizeof (*old) * (old_lines - out_lines)));

	while (out_lines-- > 0) {
		unsigned int payload;

		for (s = in; s < in + in_lines; ++s)
			if (s->line == out->line)
				break;

		/* Found something we didn't send */
		assert (s < in + in_lines);

		/* Identified as something else */
		// fprintf (stderr, "%3u id %08x %08x\n", s->line, s->id, out->id);
		assert (s->id == out->id);

		/* Same data as sent */
		payload = vbi3_sliced_payload_bits (out->id);
		assert (0 == memcmp (s->data, out->data, payload >> 3));

		if (payload & 7) {
			unsigned int mask = (1 << (payload & 7)) - 1;

			payload = (payload >> 3);

			/* MSBs zero, rest as sent */
			assert (0 == ((s->data[payload] & mask)
				      ^ out->data[payload]));
		}

		++out;
	}
}

static void
test_cycle			(const vbi3_sampling_par *sp,
				 const block *		b,
				 unsigned int		pixel_mask,
				 unsigned int		strict)
{
	vbi3_sliced *in;
	vbi3_sliced out[50];
	vbi3_sliced old[50];
	uint8_t *raw;
	vbi3_raw_decoder *rd;
	unsigned int in_lines;
	unsigned int out_lines;

	in_lines = create_raw (&raw, &in, sp, b, pixel_mask);

	if (verbose)
		dump_hex (raw + 120, 12);

	rd = create_decoder (sp, b, strict);

	memset_rand (out, sizeof (out));
	memcpy (old, out, sizeof (old));

	out_lines = vbi3_raw_decoder_decode (rd, out, 40, raw);

	if (verbose)
		fprintf (stderr, "%s %s in=%u out=%u\n",
			 __FUNCTION__,
			 vbi3_pixfmt_name (sp->sampling_format),
			 in_lines, out_lines);

	assert (in_lines == out_lines);

	compare_sliced (in, out, old, in_lines, out_lines, 50);

	free (in);
	free (raw);
}

static void
test_vbi			(const vbi3_sampling_par *sp,
				 const block *		b,
				 unsigned int		strict)
{
	test_cycle (sp, b, 0, strict);
}

static void
test_video			(const vbi3_sampling_par *sp,
				 const block *		b,
				 unsigned int		strict)
{
	vbi3_sampling_par sp2;
	unsigned int pixel_mask;
	vbi3_pixfmt pixfmt;

	sp2 = *sp;

	for (pixfmt = (vbi3_pixfmt) 0; pixfmt < VBI3_MAX_PIXFMTS;
	     pixfmt = (vbi3_pixfmt)(pixfmt + 1)) {
		if (0 == (VBI3_PIXFMT_SET_ALL & VBI3_PIXFMT_SET (pixfmt)))
			continue;

		sp2.sampling_format = pixfmt;
		sp2.bytes_per_line = sp2.samples_per_line
			* vbi3_pixfmt_bytes_per_pixel (pixfmt);

		/* Check bit slicer looks at Y/G */
		if (VBI3_PIXFMT_IS_YUV (pixfmt))
			pixel_mask = 0xFF;
		else
			pixel_mask = 0xFF00;

		test_cycle (&sp2, b, pixel_mask, strict);
	}
}

static const block ttx_a [] = {
	{ VBI3_SLICED_TELETEXT_A,	6, 22 },
	{ VBI3_SLICED_TELETEXT_A,	318, 335 },
	BLOCK_END,
};

static const block ttx_c_625 [] = {
	{ VBI3_SLICED_TELETEXT_C_625,	6, 22 },
	{ VBI3_SLICED_TELETEXT_C_625,	318, 335 },
	BLOCK_END,
};

static const block ttx_d_625 [] = {
	{ VBI3_SLICED_TELETEXT_D_625,	6, 22 },
	{ VBI3_SLICED_TELETEXT_D_625,	318, 335 },
	BLOCK_END,
};

static const block hi_625 [] = {
	{ VBI3_SLICED_TELETEXT_B_625,	6, 21 },
	{ VBI3_SLICED_TELETEXT_B_625,	318, 334 },
	{ VBI3_SLICED_CAPTION_625,	22, 22 },
	{ VBI3_SLICED_CAPTION_625,	335, 335 },
	{ VBI3_SLICED_WSS_625,		23, 23 },
	BLOCK_END,
};

static const block low_625 [] = {
/* No simulation yet.
	{ VBI3_SLICED_VPS,		16, 16 },
*/
	{ VBI3_SLICED_CAPTION_625,	22, 22 },
	{ VBI3_SLICED_CAPTION_625,	335, 335 },
	{ VBI3_SLICED_WSS_625,		23, 23 },
	BLOCK_END,
};

static const block cc_625 [] = {
	{ VBI3_SLICED_CAPTION_625,	22, 22 },
	{ VBI3_SLICED_CAPTION_625,	335, 335 },
	BLOCK_END,
};

static const block ttx_c_525 [] = {
	{ VBI3_SLICED_TELETEXT_C_525,	10, 21 },
	{ VBI3_SLICED_TELETEXT_C_525,	272, 284 },
	BLOCK_END,
};

static const block ttx_d_525 [] = {
	{ VBI3_SLICED_TELETEXT_D_525,	10, 21 },
	{ VBI3_SLICED_TELETEXT_D_525,	272, 284 },
	BLOCK_END,
};

static const block hi_525 [] = {
	{ VBI3_SLICED_TELETEXT_B_525,	10, 20 },
	{ VBI3_SLICED_TELETEXT_B_525,	272, 283 },
	{ VBI3_SLICED_CAPTION_525,	21, 21 },
	{ VBI3_SLICED_CAPTION_525,	284, 284 },
	BLOCK_END,
};

static const block low_525 [] = {
	{ VBI3_SLICED_CAPTION_525,	21, 21 },
	{ VBI3_SLICED_CAPTION_525,	284, 284 },
	BLOCK_END,
};

static void
test2				(const vbi3_sampling_par *sp)
{
	if (sp->videostd_set & VBI3_VIDEOSTD_SET_625_50) {
		if (sp->sampling_rate >= 13500000) {
			/* We cannot mix Teletext standards; bit rate and
			   FRC are too similar to reliable distinguish. */
			test_vbi (sp, ttx_a, 1);
			test_vbi (sp, ttx_c_625, 1);

			/* Needs sampling beyond 0H + 63 us (?) */
			if (2048 == sp->samples_per_line)
				test_vbi (sp, ttx_d_625, 1);

			test_vbi (sp, hi_625, 1);
			test_video (sp, hi_625, 1);
		} else if (sp->sampling_rate >= 5000000) {
			test_vbi (sp, low_625, 1);
			test_video (sp, low_625, 1);
		} else {
			/* WSS not possible below 5 MHz due to a cri_rate
			   check in bit_slicer_init(), but much less won't
			   work anyway. */
			test_vbi (sp, cc_625, 1);
			test_video (sp, cc_625, 1);
		}
	} else {
		if (sp->sampling_rate >= 13500000) {
			test_vbi (sp, ttx_c_525, 1);
			test_vbi (sp, ttx_d_525, 1);

			test_vbi (sp, hi_525, 1);
			test_video (sp, hi_525, 1);
		} else {
			test_vbi (sp, low_525, 1);
			test_video (sp, low_525, 1);
		}
	}
}

static void
test1				(const vbi3_sampling_par *sp)
{
	static const struct {
		unsigned int	sampling_rate;
		unsigned int	samples_per_line;
	} res [] = {
		/* bt8x8 PAL		~35.5 MHz / 2048
		   bt8x8 NTSC		~28.6 MHz / 2048
		   PAL 1:1		~14.7 MHz / 768
		   ITU-R BT.601		 13.5 MHz / 720
		   NTSC 1:1		~12.3 MHz / 640 */
		{ 35468950,	2048 },
		{ 27000000,	1440 },
		{ 13500000,	 720 },
		{  3000000,	 176 },
	};
	vbi3_sampling_par sp2;
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (res); ++i) {
		if (verbose)
			fprintf (stderr, "%.2f MHz %u spl\n",
				 res[i].sampling_rate / 1e6,
				 res[i].samples_per_line);

		sp2 = *sp;
		sp2.sampling_rate	= res[i].sampling_rate;
		sp2.samples_per_line	= res[i].samples_per_line;
		sp2.bytes_per_line	= res[i].samples_per_line;
		sp2.offset		= (int)(9.7e-6 * sp2.sampling_rate);

		test2 (&sp2);
	}
}

#if 0
static void
log	       			(vbi3_log_level		level,
				 const char *		function,
				 const char *		message,
				 void *			user_data)
{
	fprintf (stderr, "%s: %s\n", function, message);
}
#endif

int
main				(int			argc,
				 char **		argv)
{
	vbi3_sampling_par sp;
	vbi3_service_set set;

	verbose = (argc > 1);

	/*	if (verbose)
		vbi3_set_log_fn (log, NULL);
	*/

	memset (&sp, 0x55, sizeof (sp));

	set = vbi3_sampling_par_from_services (&sp, NULL,
					      VBI3_VIDEOSTD_SET_625_50,
					      ~0 & ~VBI3_SLICED_VBI3_625,
					       /* log_fn */ NULL,
					       /* log_user_data */ NULL);
	assert (set == (VBI3_SLICED_TELETEXT_A |
			VBI3_SLICED_TELETEXT_B_625 |
			VBI3_SLICED_TELETEXT_C_625 |
			VBI3_SLICED_TELETEXT_D_625 |
			VBI3_SLICED_VPS |
			VBI3_SLICED_VPS_F2 |
			VBI3_SLICED_CAPTION_625 |
			VBI3_SLICED_WSS_625));
	test2 (&sp);

	set = vbi3_sampling_par_from_services (&sp, NULL,
					      VBI3_VIDEOSTD_SET_525_60,
					       ~0 & ~VBI3_SLICED_VBI3_525,
					       /* log_fn */ NULL,
					       /* log_user_data */ NULL);
	assert (set == (VBI3_SLICED_TELETEXT_B_525 |
			VBI3_SLICED_TELETEXT_C_525 |
			VBI3_SLICED_TELETEXT_D_525 |
			VBI3_SLICED_CAPTION_525 |
			VBI3_SLICED_2xCAPTION_525
			/* Needs fix */
			/* | VBI3_SLICED_WSS_CPR1204 */ ));
	test2 (&sp);

	memset (&sp, 0x55, sizeof (sp));

	sp.videostd_set		= VBI3_VIDEOSTD_SET_PAL_BG;
	sp.sampling_format	= VBI3_PIXFMT_YUV420;
	sp.start[0]		= 6;
	sp.count[0]		= 23 - 6 + 1;
	sp.start[1]		= 318;
	sp.count[1]		= 335 - 318 + 1;
	sp.interlaced		= FALSE;
	sp.synchronous		= TRUE;

	test1 (&sp);

	sp.interlaced		= TRUE;

	test1 (&sp);

	sp.videostd_set		= VBI3_VIDEOSTD_SET_NTSC;
	sp.sampling_format	= VBI3_PIXFMT_YUV420;
	sp.start[0]		= 10;
	sp.count[0]		= 21 - 10 + 1;
	sp.start[1]		= 272;
	sp.count[1]		= 284 - 272 + 1;
	sp.interlaced		= FALSE;
	sp.synchronous		= TRUE;

	test1 (&sp);

	/* More... */

	return 0;
}
