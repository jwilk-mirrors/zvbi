/*
 *  libzvbi - Raw vbi decoder
 *
 *  Copyright (C) 2000-2003 Michael H. Schimek
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

/* $Id: decoder.c,v 1.7.2.2 2003-04-29 05:49:32 mschimek Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "decoder.h"

/**
 * @addtogroup Rawdec Raw VBI decoder
 * @ingroup Raw
 * @brief Converting raw VBI samples to bits and bytes.
 *
 * The libzvbi already offers hardware interfaces to obtain sliced
 * VBI data for further processing. However if you want to write your own
 * interface or decode data services not covered by libzvbi you can use
 * these lower level functions.
 */

/*
 *  Bit Slicer
 *
 *  This is time critical, tinker with care.
 */

#define OVERSAMPLING 4		/* 1, 2, 4, 8 */
#define THRESH_FRAC 9

static __inline__ unsigned int
bytes_per_pixel			(vbi_pixfmt		fmt)
{
	switch (fmt) {
	case VBI_PIXFMT_RGBA32_LE:
	case VBI_PIXFMT_BGRA32_LE:
	case VBI_PIXFMT_RGBA32_BE:
	case VBI_PIXFMT_BGRA32_BE:
		return 4;

	case VBI_PIXFMT_RGB24:
	case VBI_PIXFMT_BGR24:
		return 3;

	case VBI_PIXFMT_RGB16_LE:
	case VBI_PIXFMT_BGR16_LE:
	case VBI_PIXFMT_RGBA15_LE:
	case VBI_PIXFMT_BGRA15_LE:
	case VBI_PIXFMT_ARGB15_LE:
	case VBI_PIXFMT_ABGR15_LE:
	case VBI_PIXFMT_RGB16_BE:
	case VBI_PIXFMT_BGR16_BE:
	case VBI_PIXFMT_RGBA15_BE:
	case VBI_PIXFMT_BGRA15_BE:
	case VBI_PIXFMT_ARGB15_BE:
	case VBI_PIXFMT_ABGR15_BE:
	case VBI_PIXFMT_YUYV:
	case VBI_PIXFMT_YVYU:
	case VBI_PIXFMT_UYVY:
	case VBI_PIXFMT_VYUY:
		return 2;

	case VBI_PIXFMT_YUV420:
		return 1;
	}
}

static __inline__ unsigned int
green_shift			(vbi_pixfmt		fmt)
{
	switch (fmt) {
	case VBI_PIXFMT_RGBA32_LE:
	case VBI_PIXFMT_BGRA32_LE:
	case VBI_PIXFMT_RGB24:
	case VBI_PIXFMT_BGR24:
		return 8;

	case VBI_PIXFMT_RGBA32_BE:
	case VBI_PIXFMT_BGRA32_BE:
		return 16;

	case VBI_PIXFMT_RGB16_LE:
	case VBI_PIXFMT_BGR16_LE:
	case VBI_PIXFMT_RGB16_BE:
	case VBI_PIXFMT_BGR16_BE:
	case VBI_PIXFMT_ARGB15_LE:
	case VBI_PIXFMT_ABGR15_LE:
	case VBI_PIXFMT_ARGB15_BE:
	case VBI_PIXFMT_ABGR15_BE:
		return 5;

	case VBI_PIXFMT_RGBA15_LE:
	case VBI_PIXFMT_BGRA15_LE:
	case VBI_PIXFMT_RGBA15_BE:
	case VBI_PIXFMT_BGRA15_BE:
		return 6;

	case VBI_PIXFMT_UYVY:
	case VBI_PIXFMT_VYUY:
		return 8;

	case VBI_PIXFMT_YUYV:
	case VBI_PIXFMT_YVYU:
	case VBI_PIXFMT_YUV420:
		return 0;
	}
}

static __inline__ int
green				(uint8_t *		raw,
				 vbi_pixfmt		fmt)
{
	switch (fmt) {
	case VBI_PIXFMT_RGBA15_LE:
	case VBI_PIXFMT_BGRA15_LE:
	case VBI_PIXFMT_ARGB15_LE:
	case VBI_PIXFMT_ABGR15_LE:
#ifdef __i686__
		return (*(short *) raw) & (0x1F << green_shift (fmt));
#else
		return (raw[0] + raw[1] * 256) & (0x1F << green_shift (fmt));
#endif

	case VBI_PIXFMT_RGBA15_BE:
	case VBI_PIXFMT_BGRA15_BE:
	case VBI_PIXFMT_ARGB15_BE:
	case VBI_PIXFMT_ABGR15_BE:
		return (raw[0] * 256 + raw[1]) & (0x1F << green_shift (fmt));

	case VBI_PIXFMT_RGB16_LE:
	case VBI_PIXFMT_BGR16_LE:
#ifdef __i686__
		return (*(short *) raw) & (0x3F << 5);
#else
		return (raw[0] + raw[1] * 256) & (0x3F << 5); 
#endif

	case VBI_PIXFMT_RGB16_BE:
	case VBI_PIXFMT_BGR16_BE:
		return (raw[0] * 256 + raw[1]) & (0x3F << 5); 

	default:
		assert (0);
		return 0;
	}
}

/*
 * Subfunction of bit_slicer_tmpl(). Note this is just
 * a template. The code is inlined, with fmt being const.
 *
 * This function translates from the image format to
 * plain bytes, with linear interpolation of samples.
 * Could be further improved with a lowpass filter.
 */
static __inline__ unsigned int
sample				(uint8_t *		raw,
				 int			offs,
				 vbi_pixfmt		fmt)
{
	unsigned char frac = offs; /* offs & 0xFF */
	int raw0, raw1;

	frac = offs; /* offs & 0xFF */
	raw += (offs >> 8) * bytes_per_pixel (fmt);

	switch (fmt) {
	case VBI_PIXFMT_ARGB15_BE:
	case VBI_PIXFMT_ABGR15_BE:
	case VBI_PIXFMT_RGBA15_BE:
	case VBI_PIXFMT_BGRA15_BE:
	case VBI_PIXFMT_RGB16_BE:
	case VBI_PIXFMT_BGR16_BE:
	case VBI_PIXFMT_ARGB15_LE:
	case VBI_PIXFMT_ABGR15_LE:
	case VBI_PIXFMT_RGBA15_LE:
	case VBI_PIXFMT_BGRA15_LE:
	case VBI_PIXFMT_RGB16_LE:
	case VBI_PIXFMT_BGR16_LE:
		raw0 = green (raw + 0, fmt);
		raw1 = green (raw + 2, fmt);
		return (raw1 - raw0) * frac + (raw0 << 8);

	default: /* Y or G (intermediate bytes skipped by caller) */
		return (raw[bytes_per_pixel (fmt)] - raw[0])
			* frac + (raw[0] << 8);
	}
}

/*
 * Subfunction of bit_slicer_tmpl(). Note this is just
 * a template. The code is inlined, with fmt being const.
 *
 * This function verifies the framing code and
 * extracts the payload from the raw vbi data.
 */
static __inline__ vbi_bool
payload				(vbi_bit_slicer *	d,
				 uint8_t *		raw,
				 uint8_t *		buf,
				 unsigned int		tr,
				 vbi_pixfmt		fmt)
{
	unsigned int i, j, k;
	unsigned int c;

	i = d->phase_shift;
	tr *= 256;

	c = 0;

	for (j = d->frc_bits; j > 0; j--) {
		c = c * 2 + (sample (raw, i, fmt) >= tr);
		i += d->step;
	}

	if (c ^= d->frc)
		return FALSE;

	switch (d->endian) {
	case 3: /* bitwise, lsb first */
		for (j = 0; j < (unsigned int) d->payload; j++) {
			c >>= 1;
			c += (sample (raw, i, fmt) >= tr) << 7;
			i += d->step;

			if ((j & 7) == 7)
				*buf++ = c;
		}

		*buf = c >> ((8 - d->payload) & 7);
		break;

	case 2: /* bitwise, msb first */
		for (j = 0; j < (unsigned int) d->payload; j++) {
			c = c * 2 + (sample (raw, i, fmt) >= tr);
			i += d->step;

			if ((j & 7) == 7)
				*buf++ = c;
		}

		*buf = c & ((1 << (d->payload & 7)) - 1);
		break;

	case 1: /* octets, lsb first */
		for (j = d->payload; j > 0; j--) {
			for (k = 0; k < 8; k++) {
				c >>= 1;
				c += (sample(raw, i, fmt) >= tr) << 7;
				i += d->step;
			}

			*buf++ = c;
		}

		break;

	case 0: /* octets, msb first */
		for (j = d->payload; j > 0; j--) {
			for (k = 0; k < 8; k++) {
				c = c * 2 + (sample (raw, i, fmt) >= tr);
				i += d->step;
			}

			*buf++ = c;
		}

		break;
	}

	return TRUE;
}

/*
 * Note this is just a template. The code is inlined,
 * with fmt being const.
 */
static __inline__ vbi_bool
bit_slicer_tmpl			(vbi_bit_slicer *	d,
				 uint8_t *		raw,
				 uint8_t *		buf,
				 vbi_pixfmt		fmt)
{
	unsigned int i, j;
	unsigned int cl = 0, thresh0 = d->thresh, tr;
	unsigned int c = 0, t;
	unsigned char b, b1 = 0;
	int raw0, raw1;

	raw += d->skip;

	for (i = d->cri_bytes; i > 0; raw += bytes_per_pixel (fmt), i--) {
		tr = d->thresh >> THRESH_FRAC;

		switch (fmt) {
		case VBI_PIXFMT_ARGB15_LE:
		case VBI_PIXFMT_ABGR15_LE:
		case VBI_PIXFMT_RGBA15_LE:
		case VBI_PIXFMT_BGRA15_LE:
		case VBI_PIXFMT_ARGB15_BE:
		case VBI_PIXFMT_ABGR15_BE:
		case VBI_PIXFMT_RGBA15_BE:
		case VBI_PIXFMT_BGRA15_BE:
		case VBI_PIXFMT_RGB16_LE:
		case VBI_PIXFMT_BGR16_LE:
		case VBI_PIXFMT_RGB16_BE:
		case VBI_PIXFMT_BGR16_BE:
			raw0 = green (raw + 0, fmt);
			raw1 = green (raw + 2, fmt);
			d->thresh += ((raw0 - tr) * (int) ABS (raw1 - raw0))
				>> (8 - green_shift (fmt));
			t = raw0 * OVERSAMPLING;
			break;

		default:
			d->thresh += ((int) raw[0] - tr)
				* (int) ABS (raw[bytes_per_pixel (fmt)] - raw[0]);
			t = raw[0] * OVERSAMPLING;
			break;
		}

		for (j = OVERSAMPLING; j > 0; j--) {
			b = ((t + (OVERSAMPLING / 2)) / OVERSAMPLING >= tr);

    			if (b ^ b1) {
				cl = d->oversampling_rate >> 1;
			} else {
				cl += d->cri_rate;

				if (cl >= (unsigned int) d->oversampling_rate) {
					cl -= d->oversampling_rate;

					c = c * 2 + b;

					if ((c & d->cri_mask) == d->cri)
						return payload (d, raw, buf, tr, fmt);
				}
			}

			b1 = b;

			if (OVERSAMPLING > 1) {
				switch (fmt) {
				case VBI_PIXFMT_ARGB15_LE:
				case VBI_PIXFMT_ABGR15_LE:
				case VBI_PIXFMT_RGBA15_LE:
				case VBI_PIXFMT_BGRA15_LE:
				case VBI_PIXFMT_ARGB15_BE:
				case VBI_PIXFMT_ABGR15_BE:
				case VBI_PIXFMT_RGBA15_BE:
				case VBI_PIXFMT_BGRA15_BE:
				case VBI_PIXFMT_RGB16_LE:
				case VBI_PIXFMT_BGR16_LE:
				case VBI_PIXFMT_RGB16_BE:
				case VBI_PIXFMT_BGR16_BE:
					t += raw1;
					t -= raw0;
					break;

				default:
					t += raw[bytes_per_pixel (fmt)];
					t -= raw[0];
					break;
				}
			}
		}
	}

	d->thresh = thresh0;

	return FALSE;
}

#define BIT_SLICER_IMPL(suffix, fmt)					\
static vbi_bool								\
bit_slicer_##suffix (vbi_bit_slicer *d, uint8_t *raw, uint8_t *buf)	\
{									\
	return bit_slicer_tmpl (d, raw, buf, fmt);			\
}

BIT_SLICER_IMPL (yuv420,	VBI_PIXFMT_YUV420);
BIT_SLICER_IMPL (yuyv,		VBI_PIXFMT_YUYV);
BIT_SLICER_IMPL (rgb24,		VBI_PIXFMT_RGB24);
BIT_SLICER_IMPL (rgba32_le,	VBI_PIXFMT_RGBA32_LE);
BIT_SLICER_IMPL (argb15_le,	VBI_PIXFMT_ARGB15_LE);
BIT_SLICER_IMPL (rgba15_le,	VBI_PIXFMT_RGBA15_LE);
BIT_SLICER_IMPL (rgb16_le,	VBI_PIXFMT_RGB16_LE);
BIT_SLICER_IMPL (argb15_be,	VBI_PIXFMT_ARGB15_BE);
BIT_SLICER_IMPL (rgba15_be,	VBI_PIXFMT_RGBA15_BE);
BIT_SLICER_IMPL (rgb16_be,	VBI_PIXFMT_RGB16_BE);

/**
 * @param slicer Pointer to vbi_bit_slicer object to be initialized. 
 * @param raw_samples Number of samples or pixels in one raw vbi line
 *   later passed to vbi_bit_slice(). This limits the number of
 *   bytes read from the sample buffer.
 * @param sampling_rate Raw vbi sampling rate in Hz, that is the number of
 *   samples or pixels sampled per second by the hardware. 
 * @param cri_rate The Clock Run In is a NRZ modulated
 *   sequence of '0' and '1' bits prepending most data transmissions to
 *   synchronize data acquisition circuits. This parameter gives the CRI bit
 *   rate in Hz, that is the number of CRI bits transmitted per second.
 * @param bit_rate The transmission bit rate of all data bits following the CRI
 *   in Hz.
 * @param cri_frc The FRaming Code usually following the CRI is a bit sequence
 *   identifying the data service, and per libzvbi definition modulated
 *   and transmitted at the same bit rate as the payload (however nothing
 *   stops you from counting all nominal CRI and FRC bits as CRI).
 *   The bit slicer compares the bits in this word, lsb last transmitted,
 *   against the transmitted CRI and FRC. Decoding of payload starts
 *   with the next bit after a match.
 * @param cri_mask Of the CRI bits in @c cri_frc, only these bits are
 *   actually significant for a match. For instance it is wise
 *   not to rely on the very first CRI bits transmitted. Note this
 *   mask is not shifted left by @a frc_bits.
 * @param cri_bits 
 * @param frc_bits Number of CRI and FRC bits in @a cri_frc, respectively.
 *   Their sum is limited to 32.
 * @param payload Number of payload <em>bits</em>. Only this data
 *   will be stored in the vbi_bit_slice() output. If this number
 *   is no multiple of eight, the most significant bits of the
 *   last byte are undefined.
 * @param modulation Modulation of the vbi data, see vbi_modulation.
 * @param fmt Format of the raw data, see vbi_pixfmt.
 * 
 * Initializes vbi_bit_slicer object. Usually you will not use this
 * function but vbi_raw_decode(), the vbi image decoder which handles
 * all these details.
 */
void
vbi_bit_slicer_init		(vbi_bit_slicer *	slicer,
				 int			raw_samples,
				 int			sampling_rate,
				 int			cri_rate,
				 int			bit_rate,
				 unsigned int		cri_frc,
				 unsigned int		cri_mask,
				 int			cri_bits,
				 int			frc_bits,
				 int			payload,
				 vbi_modulation		modulation,
				 vbi_pixfmt		fmt)
{
	unsigned int c_mask = (unsigned int)(-(cri_bits > 0)) >> (32 - cri_bits);
	unsigned int f_mask = (unsigned int)(-(frc_bits > 0)) >> (32 - frc_bits);
	unsigned int gsh;

	switch (fmt) {
	case VBI_PIXFMT_RGB24:
	case VBI_PIXFMT_BGR24:
	        slicer->func = bit_slicer_rgb24;
		break;

	case VBI_PIXFMT_RGBA32_LE:
	case VBI_PIXFMT_BGRA32_LE:
	case VBI_PIXFMT_RGBA32_BE:
	case VBI_PIXFMT_BGRA32_BE:
		slicer->func = bit_slicer_rgba32_le;
		break;

	case VBI_PIXFMT_RGB16_LE:
	case VBI_PIXFMT_BGR16_LE:
		slicer->func = bit_slicer_rgb16_le;
		break;

	case VBI_PIXFMT_RGBA15_LE:
	case VBI_PIXFMT_BGRA15_LE:
		slicer->func = bit_slicer_rgba15_le;
		break;

	case VBI_PIXFMT_ARGB15_LE:
	case VBI_PIXFMT_ABGR15_LE:
		slicer->func = bit_slicer_argb15_le;
		break;

	case VBI_PIXFMT_RGB16_BE:
	case VBI_PIXFMT_BGR16_BE:
		slicer->func = bit_slicer_rgb16_be;
		break;

	case VBI_PIXFMT_RGBA15_BE:
	case VBI_PIXFMT_BGRA15_BE:
		slicer->func = bit_slicer_rgba15_be;
		break;

	case VBI_PIXFMT_ARGB15_BE:
	case VBI_PIXFMT_ABGR15_BE:
		slicer->func = bit_slicer_argb15_be;
		break;

	case VBI_PIXFMT_YUV420:
		slicer->func = bit_slicer_yuv420;
		break;

	case VBI_PIXFMT_YUYV:
	case VBI_PIXFMT_YVYU:
	case VBI_PIXFMT_UYVY:
	case VBI_PIXFMT_VYUY:
		slicer->func = bit_slicer_yuyv;
		break;

	default:
		fprintf (stderr, "vbi_bit_slicer_init: unknown pixfmt %d\n", fmt);
		exit (EXIT_FAILURE);
	}

	gsh = green_shift (fmt);

	slicer->skip			= gsh >> 3;
	slicer->cri_mask		= cri_mask & c_mask;
	slicer->cri		 	= (cri_frc >> frc_bits) & slicer->cri_mask;
	/* We stop searching for CRI/FRC when the payload
	   cannot possibly fit anymore. */
	slicer->cri_bytes		= raw_samples
		- (sampling_rate * (long long)(payload + frc_bits)) / bit_rate;
	slicer->cri_rate		= cri_rate;
	/* Raw vbi data is oversampled to account for low sampling rates. */
	slicer->oversampling_rate	= sampling_rate * OVERSAMPLING;
	/* 0/1 threshold */
	slicer->thresh			=
		105 << (THRESH_FRAC + ((32 - gsh) & 7));
	slicer->frc			= cri_frc & f_mask;
	slicer->frc_bits		= frc_bits;
	/* Payload bit distance in 1/256 raw samples. */
	slicer->step			= (int)(sampling_rate * 256.0 / bit_rate);

	if (payload & 7) {
		slicer->payload	= payload;
		slicer->endian	= 3;
	} else {
		slicer->payload	= payload >> 3;
		slicer->endian	= 1;
	}

	switch (modulation) {
	case VBI_MODULATION_NRZ_MSB:
		slicer->endian--;
	case VBI_MODULATION_NRZ_LSB:
		slicer->phase_shift = (int)
			(sampling_rate * 256.0 / cri_rate * .5
			 + sampling_rate * 256.0 / bit_rate * .5 + 128);
		break;

	case VBI_MODULATION_BIPHASE_MSB:
		slicer->endian--;
	case VBI_MODULATION_BIPHASE_LSB:
		/* Phase shift between the NRZ modulated CRI and the
		   biphase modulated rest */
		slicer->phase_shift = (int)
			(sampling_rate * 256.0 / cri_rate * .5
			 + sampling_rate * 256.0 / bit_rate * .25 + 128);
		break;
	}
}

/*
 * Data Service Table
 */

#define MAX_JOBS		N_ELEMENTS (((vbi_raw_decoder *) 0)->jobs)
#define MAX_WAYS		8

struct vbi_service_par {
	unsigned int	id;		/* VBI_SLICED_ */
	const char *	label;
	short		first[2];	/* scanning lines (ITU-R), max. distribution; */
        short		last[2];	/*  zero: no data from this field, requires field sync */
	int		offset;		/* leading edge hsync to leading edge first CRI one bit
					    half amplitude points, nanoseconds */
	int		cri_rate;	/* Hz */
	int		bit_rate;	/* Hz */
	int		scanning;	/* scanning system: 525 (FV = 59.94 Hz, FH = 15734 Hz),
							    625 (FV = 50 Hz, FH = 15625 Hz) */
	unsigned int	cri_frc;	/* Clock Run In and FRaming Code, LSB last txed bit of FRC */
	unsigned int	cri_mask;	/* cri bits significant for identification, */
	uint8_t		cri_bits;
	uint8_t		frc_bits;	/* cri_bits at cri_rate, frc_bits at bit_rate */
	uint16_t	payload;	/* in bits */
	uint8_t		modulation;	/* payload modulation */
};

const struct vbi_service_par
vbi_services[] = {
	{
		VBI_SLICED_TELETEXT_B_L10_625, "Teletext System B Level 1.5, 625",
		{ 7, 320 },
		{ 22, 335 },
		10300, 6937500, 6937500, /* 444 x FH */
		625, 0x00AAAAE4, ~0, 10, 6, 42 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_TELETEXT_B, "Teletext System B, 625",
		{ 6, 318 },
		{ 22, 335 },
		10300, 6937500, 6937500, /* 444 x FH */
		625, 0x00AAAAE4, ~0, 10, 6, 42 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_VPS, "Video Programming System",
		{ 16, 0 },
		{ 16, 0 },
		12500, 5000000, 2500000, /* 160 x FH */
		625, 0xAAAA8A99, ~0, 24, 0, 13 * 8, VBI_MODULATION_BIPHASE_MSB
	}, {
		VBI_SLICED_WSS_625, "Wide Screen Signalling 625",
		{ 23, 0 },
		{ 23, 0 },
		11000, 5000000, 833333, /* 160/3 x FH */
		625, 0xC71E3C1F, 0x924C99CE, 32, 0, 14 * 1, VBI_MODULATION_BIPHASE_LSB
	}, {
		VBI_SLICED_CAPTION_625_F1, "Closed Caption 625, field 1",
		{ 22, 0 },
		{ 22, 0 },
		10500, 1000000, 500000, /* 32 x FH */
		625, 0x00005551, ~0, 9, 2, 2 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_CAPTION_625_F2, "Closed Caption 625, field 2",
		{ 0, 335 },
		{ 0, 335 },
		10500, 1000000, 500000, /* 32 x FH */
		625, 0x00005551, ~0, 9, 2, 2 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_VBI_625, "VBI 625", /* Blank VBI */
		{ 6, 318 },
		{ 22, 335 },
		10000, 1510000, 1510000,
		625, 0, 0, 0, 0, 10 * 8, 0 /* 10.0-2 ... 62.9+1 us */
	}, {
		VBI_SLICED_NABTS, "Teletext System C, 525", /* NOT CONFIRMED */
		{ 10, 0 },
		{ 21, 0 },
		10500, 5727272, 5727272,
		525, 0x00AAAAE7, ~0, 10, 6, 33 * 8, VBI_MODULATION_NRZ_LSB /* Tb. */
	}, {
		VBI_SLICED_WSS_CPR1204, "Wide Screen Signalling 525",
		/* NOT CONFIRMED (EIA-J CPR-1204) */
		{ 20, 283 },
		{ 20, 283 },
		11200, 3579545, 447443, /* 1/8 x FSC */
		525, 0x0000FF00, 0x00003C3C, 16, 0, 20 * 1, VBI_MODULATION_NRZ_MSB
		/* No useful FRC, but a six bit CRC */
	}, {
		VBI_SLICED_CAPTION_525_F1, "Closed Caption 525, field 1",
		{ 21, 0 },
		{ 21, 0 },
		10500, 1006976, 503488, /* 32 x FH */
		525, 0x00005551, ~0, 9, 2, 2 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_CAPTION_525_F2, "Closed Caption 525, field 2",
		{ 0, 284 },
		{ 0, 284 },
		10500, 1006976, 503488, /* 32 x FH */
		525, 0x00005551, ~0, 9, 2, 2 * 8, VBI_MODULATION_NRZ_LSB
	}, {
		VBI_SLICED_2xCAPTION_525, "2xCaption 525", /* NOT CONFIRMED */
		{ 10, 0 },
		{ 21, 0 },
		10500, 1006976, 1006976, /* 64 x FH */
		525, 0x000554ED, ~0, 8, 8, 4 * 8, VBI_MODULATION_NRZ_LSB /* Tb. */
	}, {
		VBI_SLICED_TELETEXT_BD_525, "Teletext System B / D (Japan), 525",
		/* NOT CONFIRMED */
		{ 10, 0 },
		{ 21, 0 },
		9600, 5727272, 5727272,
		525, 0x00AAAAE4, ~0, 10, 6, 34 * 8, VBI_MODULATION_NRZ_LSB /* Tb. */
	}, {
		VBI_SLICED_VBI_525, "VBI 525", /* Blank VBI */
		{ 10, 272 },
		{ 21, 284 },
		9500, 1510000, 1510000,
		525, 0, 0, 0, 0, 10 * 8, 0 /* 9.5-1 ... 62.4+1 us */
	},
	{
		0, NULL,
		{ 0, 0 },
		{ 0, 0 },
		0, 0, 0,
		0, 0, 0, 0, 0, 0, 0
	}
};

/**
 * @param service A @ref VBI_SLICED_ symbol, for example from a vbi_sliced structure.
 *
 * @return
 * Name of the @a service, ASCII, or @c NULL if unknown.
 */
const char *
vbi_sliced_name			(unsigned int		service)
{
	int i;

	/* These are ambiguous */
	if (service & VBI_SLICED_TELETEXT_B)
		return "Teletext System B";
	if (service & VBI_SLICED_CAPTION_525)
		return "Closed Caption 525";
	if (service & VBI_SLICED_CAPTION_625)
		return "Closed Caption 625";

	for (i = 0; vbi_services[i].id; i++)
		if (service & vbi_services[i].id)
			return vbi_services[i].label;

	return NULL;
}

#ifndef DECODER_PATTERN_DUMP
#define DECODER_PATTERN_DUMP 0
#endif

#ifndef DECODER_SERVICE_DIAG
#define DECODER_SERVICE_DIAG 0
#endif

#define dsdiag(templ, args...)						\
do { /* Check syntax in any case */					\
	if (DECODER_SERVICE_DIAG)					\
		fprintf (stderr, templ , ##args);			\
} while (0)

static __inline__ int
cpr1204_crc			(const vbi_sliced *	out)
{
	const int poly = (1 << 6) + (1 << 1) + 1;
	int crc, i;

	crc = (out->data[0] << 12) + (out->data[1] << 4) + out->data[2];
	crc |= (((1 << 6) - 1) << (14 + 6));

	for (i = 14 + 6 - 1; i >= 0; i--) {
		if (crc & ((1 << 6) << i))
			crc ^= poly << i;
	}

	return crc;
}

/**
 * @param rd Initialized vbi_raw_decoder structure.
 * @param raw A raw vbi image as defined in the vbi_raw_decoder structure
 *   (rd->sampling_format, rd->bytes_per_line, rd->count[0] + rd->count[1]
 *    scan lines).
 * @param out Buffer to store the decoded vbi_sliced data. Since every
 *   vbi scan line may contain data, this must be an array of vbi_sliced
 *   with the same number of entries as scan lines in the raw image
 *   (rd->count[0] + rd->count[1]).
 * 
 * Decode a raw vbi image, consisting of several scan lines of raw vbi data,
 * into sliced vbi data. The output is sorted by line number.
 * 
 * Note this function attempts to learn which lines carry which data
 * service, or none, to speed up decoding. You should avoid using the same
 * vbi_raw_decoder structure for different sources.
 *
 * @bug This function ignores the sampling_format field in struct
 * vbi_raw_decoder, always assuming VBI_PIXFMT_YUV420.
 *
 * @return
 * The number of lines decoded, i. e. the number of vbi_sliced records
 * written.
 */
/* XXX bit_slicer_1() */
int
vbi_raw_decode(vbi_raw_decoder *rd, uint8_t *raw, vbi_sliced *out)
{
/* FIXME not thread safe */
	static int readj = 1;

	int pitch = rd->bytes_per_line << rd->interlaced;
	int8_t *pat, *pattern = rd->pattern;
	uint8_t *raw1 = raw;
	vbi_sliced *out1 = out;
	struct _vbi_raw_decoder_job *job;
	int i, j;

	pthread_mutex_lock(&rd->mutex);

	if (!rd->services) {
		pthread_mutex_unlock(&rd->mutex);
		return 0;
	}

	for (i = 0; i < rd->count[0] + rd->count[1]; i++) {
		if (rd->interlaced && i == rd->count[0])
			raw = raw1 + rd->bytes_per_line;

		if (DECODER_PATTERN_DUMP) {
			fprintf(stderr, "L%02d ", i);
			for (j = 0; j < MAX_WAYS; j++)
				if (pattern[j] < 1 || pattern[j] > 8)
					fprintf(stderr, "0x%02x       ",
						pattern[j] & 0xFF);
				else
					fprintf(stderr, "0x%08x ",
						rd->jobs[pattern[j] - 1].id);
			fprintf(stderr, "\n");
		}

		for (pat = pattern;; pat++) {
			j = *pat; /* data service n, blank 0, or counter -n */

			if (j > 0) {
				job = rd->jobs + (j - 1);

				if (!bit_slicer_yuv420 (&job->slicer, raw + job->offset, out->data))
					continue; /* no match, try next data service */

				/* This service has no CRI/FRC but a CRC */
				if (job->id == VBI_SLICED_WSS_CPR1204) {
					if (cpr1204_crc (out))
						continue;
				}

				/* Positive match, output decoded line */

				out->id = job->id;

				if (i >= rd->count[0])
					out->line = (rd->start[1] > 0) ?
						rd->start[1] - rd->count[0] + i : 0;
				else
					out->line = (rd->start[0] > 0) ?
						rd->start[0] + i : 0;
				out++;

				/* Predict line as non-blank, force testing for
				   data services in the next 128 frames */
				pattern[MAX_WAYS - 1] = -128;

			} else if (pat == pattern) {
				/* Line was predicted as blank, once in 16
				   frames look for data services */
				if ((readj & 15) == 0) {
					j = pattern[0];
					memmove (&pattern[0], &pattern[1],
						 sizeof (*pattern) * (MAX_WAYS - 1));
					pattern[MAX_WAYS - 1] = j;
				}

				break;
			} else if ((j = pattern[MAX_WAYS - 1]) < 0) {
				/* Increment counter, when zero predict line as
				   blank and stop looking for data services */
				pattern[MAX_WAYS - 1] = j + 1;
    				break;
			} else {
				/* found nothing, j = 0 (blank) */
			}

			/* Try the found data service first next time */
			*pat = pattern[0];
			pattern[0] = j;

			break; /* line done */
		}

		raw += pitch;
		pattern += MAX_WAYS;
	}

	++readj;

	pthread_mutex_unlock(&rd->mutex);

	return out - out1;
}

static void
vbi_raw_decoder_remove_pattern( int job, int8_t * pattern, int pattern_size )
{
        int8_t * pat;
	int ways_left;
        int  i, j;

        for (i = 0; i < pattern_size; i++) {
                pat = pattern + i * MAX_WAYS;
                for (j = 0; j < MAX_WAYS; j++) {
                        if (pat[j] == job + 1) {
                                ways_left = (MAX_WAYS - 1) - j;

				if (0) {
					fprintf (stderr, "row %d: %02X%02X%02X%02X%02X%02X%02X%02X\n",
						 i, pat[0] & 0xff, pat[1] & 0xff,
						 pat[2] & 0xff, pat[3] & 0xff,
						 pat[4] & 0xff, pat[5] & 0xff,
						 pat[6] & 0xff, pat[7] & 0xff);
					fprintf (stderr, "row %d: memmove 0x%lX 0x%lX %d\n",
						 i, (long) &pat[j], (long) &pat[j + 1],
						 ways_left * sizeof (*pattern));
				}
                                if (ways_left > 0)
                                        memmove(&pat[j], &pat[j + 1],
                                                ways_left * sizeof(*pattern));

                                pat[MAX_WAYS - 1] = 0;
				if (0)
					fprintf (stderr, "row %d: %02X%02X%02X%02X%02X%02X%02X%02X\n",
						 i, pat[0] & 0xff,  pat[1] & 0xff,
						 pat[2] & 0xff, pat[3] & 0xff,
						 pat[4] & 0xff, pat[5] & 0xff,
						 pat[6] & 0xff, pat[7] & 0xff);
                        }
                }
        }

        for (i = 0; i < pattern_size; i++) {
                if (pattern[i] >= job + 1)
                        pattern[i] -= 1;
        }
}

/**
 * @param rd Initialized vbi_raw_decoder structure.
 * @param services Set of @ref VBI_SLICED_ symbols.
 * 
 * Removes one or more data services to be decoded from the
 * vbi_raw_decoder structure. This function can be called at any
 * time and does not touch sampling parameters. 
 * 
 * @return 
 * Set of @ref VBI_SLICED_ symbols describing the remaining data
 * services that will be decoded.
 */
unsigned int
vbi_raw_decoder_remove_services(vbi_raw_decoder *rd, unsigned int services)
{
	int i;
	int pattern_size = (rd->count[0] + rd->count[1]) * MAX_WAYS;

	pthread_mutex_lock(&rd->mutex);

	for (i = 0; i < rd->num_jobs;) {
		if (rd->jobs[i].id & services) {
			if (rd->pattern)
                                vbi_raw_decoder_remove_pattern(i, rd->pattern, pattern_size);

                        if (i + 1 < MAX_JOBS)
			        memmove(rd->jobs + i, rd->jobs + (i + 1),
				        (MAX_JOBS - (i + 1)) * sizeof(rd->jobs[0]));

			rd->num_jobs -= 1;
			rd->jobs[rd->num_jobs].id = 0;
		} else
			i++;
	}

	pthread_mutex_unlock(&rd->mutex);

	return rd->services &= ~services;
}

/**
 * @param rd Initialized vbi_raw_decoder structure.
 * @param services Set of @ref VBI_SLICED_ symbols.
 * @param strict A value of 0, 1 or 2 requests loose, reliable or strict
 *  matching of sampling parameters. For example if the data service
 *  requires knowledge of line numbers while they are not known, @c 0
 *  will accept the service (which may work if the scan lines are
 *  populated in a non-confusing way) but @c 1 or @c 2 will not. If the
 *  data service <i>may</i> use more lines than are sampled, @c 1 will
 *  accept but @c 2 will not. If unsure, set to @c 1.
 * 
 * After you initialized the sampling parameters in @a rd (according to
 * the abilities of your raw vbi source), this function adds one or more
 * data services to be decoded. The libzvbi raw vbi decoder can decode up
 * to eight data services in parallel. You can call this function while
 * already decoding, it does not change sampling parameters and you must
 * not change them either after calling this.
 * 
 * @return
 * Set of @ref VBI_SLICED_ symbols describing the data services that actually
 * will be decoded. This excludes those services not decodable given
 * the sampling parameters in @a rd.
 */
unsigned int
vbi_raw_decoder_add_services(vbi_raw_decoder *rd, unsigned int services, int strict)
{
	double off_min = (rd->scanning == 525) ? 7.9e-6 : 8.0e-6;
	int row[2], count[2], way;
	struct _vbi_raw_decoder_job *job;
	int8_t *pattern;
	int i, j, k;

	pthread_mutex_lock(&rd->mutex);

	services &= ~(VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625);

	if (!rd->pattern)
		rd->pattern = (int8_t *) calloc((rd->count[0] + rd->count[1])
						* MAX_WAYS, sizeof(rd->pattern[0]));

	for (i = 0; vbi_services[i].id; i++) {
		double signal;
		int skip = 0;

		if (rd->num_jobs >= (int) MAX_JOBS)
			break;

		if (!(vbi_services[i].id & services))
			continue;

		if (vbi_services[i].scanning != rd->scanning)
			goto eliminate;

		if ((vbi_services[i].id & (VBI_SLICED_CAPTION_525_F1
					   | VBI_SLICED_CAPTION_525))
		    && (rd->start[0] <= 0 || rd->start[1] <= 0)) {
			/*
			 *  The same format is used on other lines
			 *  for non-CC data.
			 */
			goto eliminate;
		}

		signal = vbi_services[i].cri_bits / (double) vbi_services[i].cri_rate
			 + (vbi_services[i].frc_bits + vbi_services[i].payload)
			   / (double) vbi_services[i].bit_rate;

		if (rd->offset > 0 && strict > 0) {
			double offset = rd->offset / (double) rd->sampling_rate;
			double samples_end = (rd->offset + rd->bytes_per_line)
					     / (double) rd->sampling_rate;

			if (offset > (vbi_services[i].offset / 1e9 - 0.5e-6)) {
                                dsdiag ("skipping service 0x%08X: H-Off %d = %f > %f\n",
					vbi_services[i].id, rd->offset, offset,
					vbi_services[i].offset / 1e9 - 0.5e-6);
				goto eliminate;
                        }

			if (samples_end < (vbi_services[i].offset / 1e9
					   + signal + 0.5e-6)) {
                                dsdiag ("skipping service 0x%08X: sampling window too short: "
					"end %f < %f = offset %d *10^-9 + %f\n",
					vbi_services[i].id, samples_end,
					vbi_services[i].offset / 1e9 + signal + 0.5e-6,
					vbi_services[i].offset, signal);
				goto eliminate;
                        }

			if (offset < off_min) /* skip colour burst */
				skip = (int)(off_min * rd->sampling_rate);
		} else {
			double samples = rd->bytes_per_line
				         / (double) rd->sampling_rate;

			if (samples < (signal + 1.0e-6)) {
                                dsdiag ("skipping service 0x%08X: not enough samples\n",
					vbi_services[i].id);
				goto eliminate;
                        }
		}

		for (j = 0, job = rd->jobs; j < rd->num_jobs; job++, j++) {
			unsigned int id = job->id | vbi_services[i].id;

			if ((id & ~(VBI_SLICED_TELETEXT_B_L10_625 | VBI_SLICED_TELETEXT_B_L25_625)) == 0
			    || (id & ~(VBI_SLICED_CAPTION_625_F1 | VBI_SLICED_CAPTION_625)) == 0
			    || (id & ~(VBI_SLICED_CAPTION_525_F1 | VBI_SLICED_CAPTION_525)) == 0)
				break;
			/*
			 *  General form implies the special form. If both are
			 *  available from the device, decode() will set both
			 *  bits in the id field for the respective line. 
			 */
		}

		for (j = 0; j < 2; j++) {
			int start = rd->start[j];
			int end = start + rd->count[j] - 1;

			if (!rd->synchronous) {
                                dsdiag ("skipping service 0x%08X: not sync'ed\n",
					vbi_services[i].id);
				goto eliminate; /* too difficult */
                        }

			if (!(vbi_services[i].first[j] && vbi_services[i].last[j])) {
				count[j] = 0;
				continue;
			}

			if (rd->count[j] == 0) {
                                dsdiag ("skipping service 0x%08X: zero count\n",
					vbi_services[i].id);
				goto eliminate;
                        }

			if (rd->start[j] > 0 && strict > 0) {
				/*
				 *  May succeed if not all scanning lines
				 *  available for the service are actually used.
				 */
				if (strict > 1
				    || (vbi_services[i].first[j] ==
					vbi_services[i].last[j]))
					if (start > vbi_services[i].first[j] ||
					    end < vbi_services[i].last[j]) {
                                                dsdiag ("skipping service 0x%08X: lines not available "
							"have %d-%d, need %d-%d\n",
							vbi_services[i].id, start, end,
							vbi_services[i].first[j],
							vbi_services[i].last[j]);
						goto eliminate;
                                        }

				row[j] = MAX(0, (int) vbi_services[i].first[j] - start);
				count[j] = MIN(end, vbi_services[i].last[j]) - (start + row[j]) + 1;  /* XXX TZ FIX */
			} else {
				row[j] = 0;
				count[j] = rd->count[j];
			}

			row[1] += rd->count[0];

			for (pattern = rd->pattern + row[j] * MAX_WAYS, k = count[j];
			     k > 0; pattern += MAX_WAYS, k--) {
				int free = 0;

				for (way = 0; way < MAX_WAYS; way++)
					free += (pattern[way] <= 0
						 || ((pattern[way] - 1)
						     == job - rd->jobs));

				if (free <= 1) { /* reserve one NULL way */
                                        dsdiag ("skipping service 0x%08X: no more patterns free\n",
						vbi_services[i].id);
					goto eliminate;
                                }
			}
		}

		for (j = 0; j < 2; j++) {
			for (pattern = rd->pattern + row[j] * MAX_WAYS, k = count[j];
			     k > 0; pattern += MAX_WAYS, k--) {
				for (way = 0; pattern[way] > 0
				      && (pattern[way] - 1) != (job - rd->jobs); way++);

                                assert((pattern + MAX_WAYS - rd->pattern) <= (rd->count[0] + rd->count[1]) * MAX_WAYS);

				pattern[way] = (job - rd->jobs) + 1;
				pattern[MAX_WAYS - 1] = -128;
			}
                }

		job->id |= vbi_services[i].id;
		job->offset = skip;

		vbi_bit_slicer_init(&job->slicer,
				    rd->bytes_per_line - skip, /* XXX * bpp? */
				    rd->sampling_rate,
				    vbi_services[i].cri_rate,
				    vbi_services[i].bit_rate,
				    vbi_services[i].cri_frc,
				    vbi_services[i].cri_mask,
				    vbi_services[i].cri_bits,
				    vbi_services[i].frc_bits,
				    vbi_services[i].payload,
				    vbi_services[i].modulation,
				    rd->sampling_format);

		if (job >= rd->jobs + rd->num_jobs)
			rd->num_jobs++;

		rd->services |= vbi_services[i].id;
eliminate:
		;
	}

	pthread_mutex_unlock(&rd->mutex);

	return rd->services;
}

/**
 * @param rd Initialized vbi_raw_decoder structure.
 * @param services Set of VBI_SLICED_ symbols. Here (and only here) you
 *   can add @c VBI_SLICED_VBI_625 or @c VBI_SLICED_VBI_525 to include all
 *   vbi scan lines in the calculated sampling parameters.
 * @param scanning When 525 accept only NTSC services, when 625
 *   only PAL/SECAM services. When scanning is 0, assume the scanning
 *   from the requested services, an ambiguous set will pick
 *   a 525 or 625 line system at random.
 * @param max_rate If given, the highest data bit rate in Hz of all
 *   services requested is stored here. (The sampling rate
 *   should be at least twice as high; rd->sampling_rate will
 *   be set to a more reasonable value of 27 MHz derived
 *   from ITU-R Rec. 601.)
 * 
 * Calculate the sampling parameters in @a rd required to receive and
 * decode the requested data @a services. rd->sampling_format will be
 * @c VBI_PIXFMT_YUV420, rd->bytes_per_line set accordingly to a
 * reasonable minimum. This function can be used to initialize hardware
 * prior to calling vbi_raw_decoder_add_service().
 * 
 * @return
 * Set of @ref VBI_SLICED_ symbols describing the data services covered
 * by the calculated sampling parameters. This excludes services the libzvbi
 * raw decoder cannot decode.
 */
unsigned int
vbi_raw_decoder_parameters(vbi_raw_decoder *rd, unsigned int services,
			   int scanning, int *max_rate)
{
	int i, j;

	pthread_mutex_lock(&rd->mutex);

	rd->scanning = scanning;
	rd->sampling_format = VBI_PIXFMT_YUV420;
	rd->sampling_rate = 27000000;	/* ITU-R Rec. 601 */
	rd->bytes_per_line = 0;
	rd->offset = (int)(1000e-6 * rd->sampling_rate);
	rd->start[0] = 1000;
	rd->count[0] = 0;
	rd->start[1] = 1000;
	rd->count[1] = 0;
	rd->interlaced = FALSE;
	rd->synchronous = TRUE;

	/*
	 *  Will only allocate as many vbi lines as
	 *  we need, eg. two lines per frame for CC 525,
	 *  and set reasonable parameters.
	 */
	for (i = 0; vbi_services[i].id; i++) {
		double left_margin;
		int offset, samples;
		double signal;

		if (!(vbi_services[i].id & services))
			continue;

		if (rd->scanning == 0)
			rd->scanning = vbi_services[i].scanning;

		left_margin = (rd->scanning == 525) ? 1.0e-6 : 2.0e-6;

		if (vbi_services[i].scanning != rd->scanning) {
			services &= ~vbi_services[i].id;
			continue;
		}

		*max_rate = MAX(*max_rate,
			MAX(vbi_services[i].cri_rate,
			    vbi_services[i].bit_rate));

		signal = vbi_services[i].cri_bits / (double) vbi_services[i].cri_rate
			 + (vbi_services[i].frc_bits + vbi_services[i].payload)
			   / (double) vbi_services[i].bit_rate;

		offset = (int)((vbi_services[i].offset / 1e9 - left_margin)
			* rd->sampling_rate + 0.5);
		samples = (int)((signal + left_margin + 1.0e-6)
			  * rd->sampling_rate + 0.5);

		rd->offset = MIN(rd->offset, offset);

		rd->bytes_per_line =
			MIN(rd->bytes_per_line + rd->offset,
			    samples + offset) - rd->offset;

		for (j = 0; j < 2; j++)
			if (vbi_services[i].first[j] &&
			    vbi_services[i].last[j]) {
				rd->start[j] =
					MIN(rd->start[j],
					    vbi_services[i].first[j]);
				rd->count[j] =
					MAX(rd->start[j] + rd->count[j],
					    vbi_services[i].last[j] + 1)
					- rd->start[j];
			}
	}

	if (!rd->count[0])
		rd->start[0] = -1;

	if (!rd->count[1]) {
		rd->start[1] = -1;

		if (!rd->count[0])
			rd->offset = 0;
	}

	pthread_mutex_unlock(&rd->mutex);

	return services;
}

/**
 * @param rd Initialized vbi_raw_decoder structure.
 * 
 * Reset a vbi_raw_decoder structure. This removes
 * all previously added services to be decoded (if any)
 * but does not touch the sampling parameters. You are
 * free to change the sampling parameters after calling this.
 */
void
vbi_raw_decoder_reset(vbi_raw_decoder *rd)
{
	if (!rd)
		return;

	pthread_mutex_lock(&rd->mutex);

	if (rd->pattern)
		free(rd->pattern);

	rd->services = 0;
	rd->num_jobs = 0;

	rd->pattern = NULL;

	memset(rd->jobs, 0, sizeof(rd->jobs));

	pthread_mutex_unlock(&rd->mutex);
}

/**
 * @param rd Pointer to initialized vbi_raw_decoder
 *  structure, can be @c NULL.
 * 
 * Free all resources associated with @a rd.
 */
void
vbi_raw_decoder_destroy(vbi_raw_decoder *rd)
{
	vbi_raw_decoder_reset(rd);

	pthread_mutex_destroy(&rd->mutex);
}

/**
 * @param rd Pointer to a vbi_raw_decoder structure.
 * 
 * Initializes a vbi_raw_decoder structure.
 */
void
vbi_raw_decoder_init(vbi_raw_decoder *rd)
{
	pthread_mutex_init(&rd->mutex, NULL);

	rd->pattern = NULL;

	vbi_raw_decoder_reset(rd);
}
