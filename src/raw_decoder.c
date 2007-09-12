/*
 *  libzvbi - Raw VBI decoder
 *
 *  Copyright (C) 2000-2004 Michael H. Schimek
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

/* $Id: raw_decoder.c,v 1.19 2007-09-12 15:55:05 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"
#include "raw_decoder.h"

#define vbi_inline static __inline__

#ifndef RAW_DECODER_PATTERN_DUMP
#  define DECODER_PATTERN_DUMP 0
#endif

#ifndef RAW_DECODER_LOG
#  define RAW_DECODER_LOG 0
#endif

#define sp_log(level, templ, args...)					\
do {									\
	_vbi_log_printf (log_fn, log_user_data,				\
			level, __FUNCTION__, templ , ##args);		\
} while (0)

/**
 * $addtogroup RawDecoder Raw VBI decoder
 * $ingroup Raw
 * $brief Converting a raw VBI image to sliced VBI data.
 */

/* Missing:
   VITC PAL 6-22 11.2us 1.8125 Mbit NRZ two start bits + CRC
   VITC NTSC 10-21 ditto
   CGMS NTSC 20 11us .450450 Mbit NRZ ?
*/
const _vbi_service_par
_vbi_service_table [] = {
	{
		VBI_SLICED_TELETEXT_A, /* UNTESTED */
		"Teletext System A",
		VBI_VIDEOSTD_SET_625_50,
		{ 6, 318 },
		{ 22, 335 },
		10500, 6203125, 6203125, /* 397 x FH */
		0x00AAAAE7, 0xFFFF, 18, 6, 37 * 8, VBI_MODULATION_NRZ_LSB,
		0, /* probably */
	}, {
		VBI_SLICED_TELETEXT_B_L10_625,
		"Teletext System B 625 Level 1.5",
		VBI_VIDEOSTD_SET_625_50,
		{ 7, 320 },
		{ 22, 335 },
		10300, 6937500, 6937500, /* 444 x FH */
		0x00AAAAE4, 0xFFFF, 18, 6, 42 * 8, VBI_MODULATION_NRZ_LSB,
		0,
	}, {
		VBI_SLICED_TELETEXT_B,
		"Teletext System B, 625",
		VBI_VIDEOSTD_SET_625_50,
		{ 6, 318 },
		{ 22, 335 },
		10300, 6937500, 6937500, /* 444 x FH */
		0x00AAAAE4, 0xFFFF, 18, 6, 42 * 8, VBI_MODULATION_NRZ_LSB,
		0,
	}, {
		VBI_SLICED_TELETEXT_C_625, /* UNTESTED */
		"Teletext System C 625",
		VBI_VIDEOSTD_SET_625_50,
		{ 6, 318 },
		{ 22, 335 },
		10480, 5734375, 5734375, /* 367 x FH */
		0x00AAAAE7, 0xFFFF, 18, 6, 33 * 8, VBI_MODULATION_NRZ_LSB,
		0,
	}, {
		VBI_SLICED_TELETEXT_D_625, /* UNTESTED */
		"Teletext System D 625",
		VBI_VIDEOSTD_SET_625_50,
		{ 6, 318 },
		{ 22, 335 },
		10500, /* or 10970 depending on field order */
		5642787, 5642787, /* 14/11 x FSC (color subcarrier) */
		0x00AAAAE5, 0xFFFF, 18, 6, 34 * 8, VBI_MODULATION_NRZ_LSB,
		0,
	}, {
		VBI_SLICED_VPS, "Video Program System",
		VBI_VIDEOSTD_SET_PAL_BG,
		{ 16, 0 },
		{ 16, 0 },
		12500, 5000000, 2500000, /* 160 x FH */
		0xAAAA8A99, 0xFFFFFF, 32, 0, 13 * 8, VBI_MODULATION_BIPHASE_MSB,
		_VBI_SP_FIELD_NUM,
	}, {
		VBI_SLICED_VPS_F2, "Pseudo-VPS on field 2",
		VBI_VIDEOSTD_SET_PAL_BG,
		{ 0, 329 },
		{ 0, 329 },
		12500, 5000000, 2500000, /* 160 x FH */
		0xAAAA8A99, 0xFFFFFF, 32, 0, 13 * 8, VBI_MODULATION_BIPHASE_MSB,
		_VBI_SP_FIELD_NUM,
	}, {
		VBI_SLICED_WSS_625, "Wide Screen Signalling 625",
		VBI_VIDEOSTD_SET_625_50,
		{ 23, 0 },
		{ 23, 0 },
		11000, 5000000, 833333, /* 160/3 x FH */
		/* ...1000 111 / 0 0011 1100 0111 1000 0011 111x */
		/* ...0010 010 / 0 1001 1001 0011 0011 1001 110x */	
		0x8E3C783E, 0x2499339C, 32, 0, 14 * 1,
		VBI_MODULATION_BIPHASE_LSB,
		/* Hm. Too easily confused with caption?? */
		_VBI_SP_FIELD_NUM | _VBI_SP_LINE_NUM,
	}, {
		VBI_SLICED_CAPTION_625_F1, "Closed Caption 625, field 1",
		VBI_VIDEOSTD_SET_625_50,
		{ 22, 0 },
		{ 22, 0 },
		10500, 1000000, 500000, /* 32 x FH */
		0x00005551, 0x7FF, 14, 2, 2 * 8, VBI_MODULATION_NRZ_LSB,
		_VBI_SP_FIELD_NUM,
	}, {
		VBI_SLICED_CAPTION_625_F2, "Closed Caption 625, field 2",
		VBI_VIDEOSTD_SET_625_50,
		{ 0, 335 },
		{ 0, 335 },
		10500, 1000000, 500000, /* 32 x FH */
		0x00005551, 0x7FF, 14, 2, 2 * 8, VBI_MODULATION_NRZ_LSB,
		_VBI_SP_FIELD_NUM,
	}, {
		VBI_SLICED_VBI_625, "VBI 625", /* Blank VBI */
		VBI_VIDEOSTD_SET_625_50,
		{ 6, 318 },
		{ 22, 335 },
		10000, 1510000, 1510000,
		0, 0, 0, 0, 10 * 8, 0, /* 10.0-2 ... 62.9+1 us */
		0,
	}, {
		VBI_SLICED_TELETEXT_B_525, /* UNTESTED */
		"Teletext System B 525",
		VBI_VIDEOSTD_SET_525_60,
		{ 10, 272 },
		{ 21, 284 },
		10500, 5727272, 5727272, /* 364 x FH */
		0x00AAAAE4, 0xFFFF, 18, 6, 34 * 8, VBI_MODULATION_NRZ_LSB,
		0,
	}, {
		VBI_SLICED_TELETEXT_C_525, /* UNTESTED */
		"Teletext System C 525",
		VBI_VIDEOSTD_SET_525_60,
		{ 10, 272 },
		{ 21, 284 },
		10480, 5727272, 5727272, /* 364 x FH */
		0x00AAAAE7, 0xFFFF, 18, 6, 33 * 8, VBI_MODULATION_NRZ_LSB,
		0,
	}, {
		VBI_SLICED_TELETEXT_D_525, /* UNTESTED */
		"Teletext System D 525",
		VBI_VIDEOSTD_SET_525_60,
		{ 10, 272 },
		{ 21, 284 },
		9780, 5727272, 5727272, /* 364 x FH */
		0x00AAAAE5, 0xFFFF, 18, 6, 34 * 8, VBI_MODULATION_NRZ_LSB,
		0,
	}, {
#if 0 /* FIXME probably wrong */
		VBI_SLICED_WSS_CPR1204,	/* NOT CONFIRMED (EIA-J CPR-1204) */
		"Wide Screen Signalling 525",
		VBI_VIDEOSTD_SET_NTSC_M_JP,
		{ 20, 283 },
		{ 20, 283 },
		11200, 1789773, 447443, /* 1/8 x FSC */
		0x000000F0, 0xFF, 8, 0, 20 * 1, VBI_MODULATION_NRZ_MSB,
		/* No useful FRC, but a six bit CRC */
		0,
	}, {
#endif
		VBI_SLICED_CAPTION_525_F1,
		"Closed Caption 525, field 1",
		VBI_VIDEOSTD_SET_525_60,
		{ 21, 0 },
		{ 21, 0 },
		10500, 1006976, 503488, /* 32 x FH */
		/* Test of CRI bits has been removed to handle the
		   incorrect signal observed by Rich Kandel (see
		   _VBI_RAW_SHIFT_CC_CRI). */
		0x03, 0x0F, 4, 0, 2 * 8, VBI_MODULATION_NRZ_LSB,
		/* I've seen CC signals on other lines and there's no
		   way to distinguish from the transmitted data. */
		_VBI_SP_FIELD_NUM | _VBI_SP_LINE_NUM,
	}, {
		VBI_SLICED_CAPTION_525_F2,
		"Closed Caption 525, field 2",
		VBI_VIDEOSTD_SET_525_60,
		{ 0, 284 },
		{ 0, 284 },
		10500, 1006976, 503488, /* 32 x FH */
		0x03, 0x0F, 4, 0, 2 * 8, VBI_MODULATION_NRZ_LSB,
		_VBI_SP_FIELD_NUM | _VBI_SP_LINE_NUM,
	}, {
		VBI_SLICED_2xCAPTION_525, /* NOT CONFIRMED */
		"2xCaption 525",
		VBI_VIDEOSTD_SET_525_60,
		{ 10, 0 },
		{ 21, 0 },
		10500, 1006976, 1006976, /* 64 x FH */
		0x000554ED, 0xFFFF, 12, 8, 4 * 8,
		VBI_MODULATION_NRZ_LSB, /* Tb. */
		_VBI_SP_FIELD_NUM,
	}, {
		VBI_SLICED_VBI_525, "VBI 525", /* Blank VBI */
		VBI_VIDEOSTD_SET_525_60,
		{ 10, 272 },
		{ 21, 284 },
		9500, 1510000, 1510000,
		0, 0, 0, 0, 10 * 8, 0, /* 9.5-1 ... 62.4+1 us */
		0,
	}, {
		0, NULL,
		VBI_VIDEOSTD_SET_EMPTY,
		{ 0, 0 },
		{ 0, 0 },
		0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0,
	}
};

vbi_inline const _vbi_service_par *
find_service_par		(unsigned int		service)
{
	unsigned int i;

	for (i = 0; _vbi_service_table[i].id; ++i)
		if (service == _vbi_service_table[i].id)
			return _vbi_service_table + i;

	return NULL;
}

/**
 * $ingroup Sliced
 * $param service A data service identifier, for example from a
 *   vbi_sliced structure.
 *
 * $return
 * Name of the $a service, in ASCII, or $c NULL if unknown.
 */
const char *
vbi_sliced_name			(vbi_service_set	service)
{
	const _vbi_service_par *par;

	/* These are ambiguous */
	if (service == VBI_SLICED_CAPTION_525)
		return "Closed Caption 525";
	if (service == VBI_SLICED_CAPTION_625)
		return "Closed Caption 625";
	if (service == (VBI_SLICED_VPS | VBI_SLICED_VPS_F2))
		return "Video Program System";
	if (service == VBI_SLICED_TELETEXT_B_L25_625)
		return "Teletext System B 625 Level 2.5";

	/* Incorrect, no longer in table */
	if (service == VBI_SLICED_TELETEXT_BD_525)
		return "Teletext System B/D";

	if ((par = find_service_par (service)))
		return par->label;

	return NULL;
}

/**
 * @ingroup Sliced
 * @param service A data service identifier, for example from a
 *   vbi_sliced structure.
 *
 * @return
 * Number of payload bits, @c 0 if the service is unknown.
 */
unsigned int
vbi_sliced_payload_bits		(unsigned int		service)
{
	const _vbi_service_par *par;

	/* These are ambiguous */
	if (service == VBI_SLICED_CAPTION_525)
		return 16;
	if (service == VBI_SLICED_CAPTION_625)
		return 16;
	if (service == (VBI_SLICED_VPS | VBI_SLICED_VPS_F2))
		return 13 * 8;
	if (service == VBI_SLICED_TELETEXT_B_L25_625)
		return 42 * 8;

	/* Incorrect, no longer in table */
	if (service == VBI_SLICED_TELETEXT_BD_525)
		return 34 * 8;

	if ((par = find_service_par (service)))
		return par->payload;

	return 0;
}

static void
dump_pattern_line		(const vbi3_raw_decoder *rd,
				 unsigned int		row,
				 FILE *			fp)
{
	const vbi_sampling_par *sp;
	unsigned int line;
	unsigned int i;

	sp = &rd->sampling;

	if (sp->interlaced) {
		unsigned int field = row & 1;

		if (0 == sp->start[field])
			line = 0;
		else
			line = sp->start[field] + (row >> 1);
	} else {
		if (row >= (unsigned int) sp->count[0]) {
			if (0 == sp->start[1])
				line = 0;
			else
				line = sp->start[1] + row - sp->count[0];
		} else {
			if (0 == sp->start[0])
				line = 0;
			else
				line = sp->start[0] + row;
		}
	}

	fprintf (fp, "scan line %3u: ", line);

	for (i = 0; i < _VBI3_RAW_DECODER_MAX_WAYS; ++i) {
		unsigned int pos;

		pos = row * _VBI3_RAW_DECODER_MAX_WAYS;
		fprintf (fp, "%02x ", (uint8_t) rd->pattern[pos + i]);
	}

	fputc ('\n', fp);
}

void
_vbi3_raw_decoder_dump		(const vbi3_raw_decoder *rd,
				 FILE *			fp)
{
	const vbi_sampling_par *sp;
	unsigned int i;

	assert (NULL != fp);

	fprintf (fp, "vbi3_raw_decoder %p\n", rd);

	if (NULL == rd)
		return;

	fprintf (fp, "  services 0x%08x\n", rd->services);

	for (i = 0; i < rd->n_jobs; ++i)
		fprintf (fp, "  job %u: 0x%08x (%s)\n",
			 i + 1, rd->jobs[i].id,
			 vbi_sliced_name (rd->jobs[i].id));

	if (!rd->pattern) {
		fprintf (fp, "  no pattern\n");
		return;
	}

	sp = &rd->sampling;

	for (i = 0; i < ((unsigned int) sp->count[0]
			 + (unsigned int) sp->count[1]); ++i) {
		fputs ("  ", fp);
		dump_pattern_line (rd, i, fp);
	}
}

vbi_inline int
cpr1204_crc			(const vbi_sliced *	sliced)
{
	const int poly = (1 << 6) + (1 << 1) + 1;
	int crc, i;

	crc = (+ (sliced->data[0] << 12)
	       + (sliced->data[1] << 4)
	       + (sliced->data[2]));

	crc |= (((1 << 6) - 1) << (14 + 6));

	for (i = 14 + 6 - 1; i >= 0; i--) {
		if (crc & ((1 << 6) << i))
			crc ^= poly << i;
	}

	return crc;
}

vbi_inline vbi_sliced *
decode_pattern			(vbi3_raw_decoder *	rd,
				 vbi_sliced *		sliced,
				 int8_t *		pattern,
				 unsigned int		i,
				 const uint8_t *	raw)
{
	vbi_sampling_par *sp;
	int8_t *pat;

	sp = &rd->sampling;

	for (pat = pattern;; ++pat) {
		int j;

		j = *pat; /* data service n, blank 0, or counter -n */

		if (j > 0) {
			_vbi3_raw_decoder_job *job;

			job = rd->jobs + j - 1;

			if (!job->slicer.func (&job->slicer,
					       sliced->data, raw)) {
				continue; /* no match, try next data service */
			}

			/* FIXME probably wrong */
			if (0 && VBI_SLICED_WSS_CPR1204 == job->id) {
				const int poly = (1 << 6) + (1 << 1) + 1;
				int crc, j;

				crc = (sliced->data[0] << 12)
					+ (sliced->data[1] << 4)
					+ sliced->data[2];
				crc |= (((1 << 6) - 1) << (14 + 6));

				for (j = 14 + 6 - 1; j >= 0; j--) {
					if (crc & ((1 << 6) << j))
						crc ^= poly << j;
				}

				if (crc)
					continue; /* no match */
			}

			/* Positive match, output decoded line. */

			sliced->id = job->id;
			sliced->line = 0;

			if (i >= (unsigned int) sp->count[0]) {
				if (sp->synchronous
				    && 0 != sp->start[1])
					sliced->line = sp->start[1]
						+ i - sp->count[0];
			} else {
				if (sp->synchronous
				    && 0 != sp->start[0])
					sliced->line = sp->start[0] + i;
			}

			if (0)
				fprintf (stderr, "%2d %s\n",
					 sliced->line,
					 vbi_sliced_name (sliced->id));

			++sliced;

			/* Predict line as non-blank, force testing for
			   all data services in the next 128 frames. */
			pattern[_VBI3_RAW_DECODER_MAX_WAYS - 1] = -128;
		} else if (pat == pattern) {
			/* Line was predicted as blank, once in 16
			   frames look for data services. */
			if (0 == rd->readjust) {
				unsigned int size;

				size = sizeof (*pattern)
					* (_VBI3_RAW_DECODER_MAX_WAYS - 1);

				j = pattern[0];
				memmove (&pattern[0], &pattern[1], size);
				pattern[_VBI3_RAW_DECODER_MAX_WAYS - 1] = j;
			}

			break;
		} else if ((j = pattern[_VBI3_RAW_DECODER_MAX_WAYS - 1]) < 0) {
			/* Increment counter, when zero predict line as
			   blank and stop looking for data services until
			   0 == rd->readjust. */
			/* Disabled because we may miss caption/subtitles
			   when the signal inserter is disabled during silent
			   periods for more than 4-5 seconds. */
			/* pattern[_VBI3_RAW_DECODER_MAX_WAYS - 1] = j + 1; */
			break;
		} else {
			/* found nothing, j = 0 */
		}

		/* Try the found data service first next time. */
		*pat = pattern[0];
		pattern[0] = j;

		break; /* line done */
	}

	return sliced;
}

/**
 * $param rd Pointer to vbi3_raw_decoder object allocated with
 *   vbi3_raw_decoder_new().
 * $param sliced Buffer to store the decoded vbi_sliced data. Since every
 *   vbi scan line may contain data, this should be an array of vbi_sliced
 *   with the same number of elements as scan lines in the raw image
 *   (vbi_sampling_parameters.count[0] + .count[1]).
 * $param sliced_lines Size of $a sliced data array, in lines, not bytes.
 * $param raw A raw vbi image as described by the vbi_sampling_par
 *   associated with $a rd.
 * 
 * Decodes a raw vbi image, consisting of several scan lines of raw vbi data,
 * to sliced vbi data. The output is sorted by ascending line number.
 * 
 * Note this function attempts to learn which lines carry which data
 * service, or if any, to speed up decoding. You should avoid using the same
 * vbi3_raw_decoder object for different sources.
 *
 * $return
 * The number of lines decoded, i. e. the number of vbi_sliced records
 * written.
 */
unsigned int
vbi3_raw_decoder_decode		(vbi3_raw_decoder *	rd,
				 vbi_sliced *		sliced,
				 unsigned int		sliced_lines,
				 const uint8_t *	raw)
{
	vbi_sampling_par *sp;
	unsigned int scan_lines;
	unsigned int pitch;
	int8_t *pattern;
	const uint8_t *raw1;
	vbi_sliced *sliced_begin;
	vbi_sliced *sliced_end;
	unsigned int i;

	if (!rd->services)
		return 0;

	sp = &rd->sampling;

	scan_lines = sp->count[0] + sp->count[1];
	pitch = sp->bytes_per_line << sp->interlaced;

	pattern = rd->pattern;

	raw1 = raw;

	sliced_begin = sliced;
	sliced_end = sliced + sliced_lines;

	if (DECODER_PATTERN_DUMP)
		_vbi3_raw_decoder_dump (rd, stderr);

	for (i = 0; i < scan_lines; ++i) {
		if (sliced >= sliced_end)
			break;

		if (sp->interlaced && i == (unsigned int) sp->count[0])
			raw = raw1 + sp->bytes_per_line;

		sliced = decode_pattern (rd, sliced, pattern, i, raw);

		pattern += _VBI3_RAW_DECODER_MAX_WAYS;
		raw += pitch;
	}

	rd->readjust = (rd->readjust + 1) & 15;

	return sliced - sliced_begin;
}

/**
 * $param rd Pointer to vbi3_raw_decoder object allocated with
 *   vbi3_raw_decoder_new().
 * 
 * Resets a vbi3_raw_decoder object, removing all services added
 * with vbi3_raw_decoder_add_services().
 */
void
vbi3_raw_decoder_reset		(vbi3_raw_decoder *	rd)
{
	assert (NULL != rd);

	if (rd->pattern)
		free (rd->pattern);

	rd->pattern = NULL;

	rd->services = 0;
	rd->n_jobs = 0;

	rd->readjust = 1;

	CLEAR (rd->jobs);
}

static void
remove_job_from_pattern		(vbi3_raw_decoder *	rd,
				 int			job_num)
{
	int8_t *pattern;
	unsigned int scan_lines;

	job_num += 1; /* index into rd->jobs, 0 means no job */

	pattern = rd->pattern;
	scan_lines = rd->sampling.count[0] + rd->sampling.count[1];

	/* For each scan line. */
	while (scan_lines-- > 0) {
		int8_t *dst;
		int8_t *src;
		int8_t *end;

		dst = pattern;
		end = pattern + _VBI3_RAW_DECODER_MAX_WAYS;

		/* Remove jobs with job_num, fill up pattern with 0.
		   Jobs above job_num move down in rd->jobs. */
		for (src = dst; src < end; ++src) {
			int8_t num = *src;

			if (num > job_num)
				*dst++ = num - 1;
			else if (num != job_num)
				*dst++ = num;
		}

		while (dst < end)
			*dst++ = 0;

		pattern = end;
	}
}

/**
 * $param rd Pointer to vbi3_raw_decoder object allocated with
 *   vbi3_raw_decoder_new().
 * $param services Set of data services.
 * 
 * Removes one or more data services to be decoded from the
 * vbi3_raw_decoder object.
 * 
 * $return 
 * Set describing the remaining data services $a rd will decode.
 */
vbi_service_set
vbi3_raw_decoder_remove_services
				(vbi3_raw_decoder *	rd,
				 vbi_service_set	services)
{
	_vbi3_raw_decoder_job *job;
	unsigned int job_num;

	assert (NULL != rd);

	job = rd->jobs;
	job_num = 0;

	while (job_num < rd->n_jobs) {
		if (job->id & services) {
			if (rd->pattern)
                                remove_job_from_pattern (rd, job_num);

			memmove (job, job + 1,
				 (rd->n_jobs - job_num - 1) * sizeof (*job));

			--rd->n_jobs;

			CLEAR (rd->jobs[rd->n_jobs]);
		} else {
			++job_num;
		}
	}

	rd->services &= ~services;

	return rd->services;
}

static vbi_bool
add_job_to_pattern		(vbi3_raw_decoder *	rd,
				 int			job_num,
				 unsigned int *		start,
				 unsigned int *		count)
{
	int8_t *pattern_end;
	unsigned int scan_lines;
	unsigned int field;

	job_num += 1; /* index into rd->jobs, 0 means no job */

	scan_lines = rd->sampling.count[0]
		+ rd->sampling.count[1];

	pattern_end = rd->pattern + scan_lines * _VBI3_RAW_DECODER_MAX_WAYS;

	for (field = 0; field < 2; ++field) {
		int8_t *pattern;
		unsigned int i;

		pattern = rd->pattern
			+ start[field] * _VBI3_RAW_DECODER_MAX_WAYS;

		/* For each line where we may find the data. */
		for (i = 0; i < count[field]; ++i) {
			unsigned int free;
			int8_t *dst;
			int8_t *src;
			int8_t *end;

			assert (pattern < pattern_end);

			dst = pattern;
			end = pattern + _VBI3_RAW_DECODER_MAX_WAYS;

			free = 0;

			for (src = dst; src < end; ++src) {
				int8_t num = *src;

				if (num <= 0) {
					++free;
					continue;
				} else {
					free += (num == job_num);
					*dst++ = num;
				}
			}

			while (dst < end)
				*dst++ = 0;

			if (free <= 1) /* reserve a NULL way */
				return FALSE;

			pattern = end;
		}
	}

	for (field = 0; field < 2; ++field) {
		int8_t *pattern;
		unsigned int i;

		pattern = rd->pattern
			+ start[field] * _VBI3_RAW_DECODER_MAX_WAYS;

		/* For each line where we may find the data. */
		for (i = 0; i < count[field]; ++i) {
			unsigned int way;

			for (way = 0; pattern[way] > 0; ++way)
				if (pattern[way] == job_num)
					break;

			pattern[way] = job_num;
			pattern[_VBI3_RAW_DECODER_MAX_WAYS - 1] = -128;

			pattern += _VBI3_RAW_DECODER_MAX_WAYS;
                }
	}

	return TRUE;
}

static void
lines_containing_data		(unsigned int		start[2],
				 unsigned int		count[2],
				 const vbi_sampling_par *sp,
				 const _vbi_service_par *par)
{
	unsigned int field;

	start[0] = 0;
	start[1] = sp->count[0];

	count[0] = sp->count[0];
	count[1] = sp->count[1];

	if (!sp->synchronous) {
		/* XXX Scanning all lines isn't always necessary. */
		return;
	}

	for (field = 0; field < 2; ++field) {
		unsigned int first;
		unsigned int last;

		if (0 == par->first[field]
		    || 0 == par->last[field]) {
			/* No data on this field. */
			count[field] = 0;
			continue;
		}

		first = sp->start[field];
		last = first + sp->count[field] - 1;

		if (first > 0 && sp->count[field] > 0) {
			assert (par->first[field] <= par->last[field]);

			if ((unsigned int) par->first[field] > last
			    || (unsigned int) par->last[field] < first)
				continue;

			first = MAX (first, (unsigned int) par->first[field]);
			last = MIN ((unsigned int) par->last[field], last);

			start[field] += first - sp->start[field];
			count[field] = last + 1 - first;
		}
	}
}

/**
 * $param rd Pointer to vbi3_raw_decoder object allocated with
 *   vbi3_raw_decoder_new().
 * $param services Set of data services.
 * $param strict A value of 0, 1 or 2 requests loose, reliable or strict
 *  matching of sampling parameters. For example if the data service
 *  requires knowledge of line numbers, $c 0 will always accept the
 *  service (which may work if the scan lines are populated in a
 *  non-confusing way) but $c 1 or $c 2 will not. If the data service
 *  might use more lines than are sampled, $c 1 will accept but $c 2
 *  will not. If unsure, set to $c 1.
 * 
 * Adds one or more data services to be decoded. Currently the libzvbi
 * raw vbi decoder can decode up to eight data services in parallel.
 * 
 * $return
 * Set describing the data services $a rd will decode. The function
 * eliminates services which cannot be decoded with the current
 * sampling parameters, or when they exceed the decoder capacity.
 */
/* Attn: strict must be int for compatibility with libzvbi 0.2 (-1 == 0) */
vbi_service_set
vbi3_raw_decoder_add_services	(vbi3_raw_decoder *	rd,
				 vbi_service_set	services,
				 int			strict)
{
	const _vbi_service_par *par;
	double min_offset;

	assert (NULL != rd);

	services &= ~(VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625);

	if (rd->services & services) {
		info (&rd->log,
		      "Already decoding services 0x%08x.",
		      rd->services & services);
		services &= ~rd->services;
	}

	if (0 == services) {
		info (&rd->log, "No services to add.");
		return rd->services;
	}

	if (!rd->pattern) {
		unsigned int scan_lines;
		unsigned int scan_ways;

		scan_lines = rd->sampling.count[0] + rd->sampling.count[1];
		scan_ways = scan_lines * _VBI3_RAW_DECODER_MAX_WAYS;

		rd->pattern = (int8_t *)
			malloc (scan_ways * sizeof (rd->pattern[0]));
		if (!rd->pattern) {
			error (&rd->log, "Out of memory.");
			return rd->services;
		}

		memset (rd->pattern, 0, scan_ways * sizeof (rd->pattern[0]));
	}

	if (525 == rd->sampling.scanning) {
		min_offset = 7.9e-6;
	} else {
		min_offset = 8.0e-6;
	}

	for (par = _vbi_service_table; par->id; ++par) {
		vbi_sampling_par *sp;
		_vbi3_raw_decoder_job *job;
		unsigned int start[2];
		unsigned int count[2];
		unsigned int sample_offset;
		unsigned int samples_per_line;
		unsigned int cri_end;
		unsigned int j;

		if (0 == (par->id & services))
			continue;

		job = rd->jobs;

		/* Some jobs can be merged, otherwise we add a new job. */
		for (j = 0; j < rd->n_jobs; ++j) {
			unsigned int id = job->id | par->id;

			/* Level 1.0 and 2.5 */
			if (0 == (id & ~VBI_SLICED_TELETEXT_B)
			/* Field 1 and 2 */
			    || 0 == (id & ~VBI_SLICED_CAPTION_525)
			    || 0 == (id & ~VBI_SLICED_CAPTION_625)
			    || 0 == (id & ~(VBI_SLICED_VPS |
					    VBI_SLICED_VPS_F2)))
				break;

			++job;
		}

		if (j >= _VBI3_RAW_DECODER_MAX_JOBS) {
			error (&rd->log,
			       "Set 0x%08x exceeds number of "
			       "simultaneously decodable "
			       "services (%u).",
			       services, _VBI3_RAW_DECODER_MAX_WAYS);
			break;
		} else if (j >= rd->n_jobs) {
			job->id = 0;
		}


		sp = &rd->sampling;

		if (!_vbi_sampling_par_check_services_log (sp,
							   par->id,
							   strict,
							   &rd->log))
			continue;


		sample_offset = 0;

		/* Skip color burst. */
		if (0
		    && sp->offset > 0 && strict > 0) {
			double offset;

			offset = sp->offset / (double) sp->sampling_rate;
			if (offset < min_offset)
				sample_offset =
					(int)(min_offset * sp->sampling_rate);
		}

		if (VBI_SLICED_WSS_625 & par->id) {
			/* TODO: WSS 625 occupies only first half of line,
			   we can abort earlier. */
			cri_end = ~0;
		} else {
			cri_end = ~0;
		}

		/* XXX use sp->samples_per_line if available */
		samples_per_line = sp->bytes_per_line
			/ VBI_PIXFMT_BPP (sp->sampling_format);

		if (!_vbi3_bit_slicer_init (&job->slicer)) {
			assert (!"bit_slicer_init");
		}

		if (!vbi3_bit_slicer_set_params
		    (&job->slicer,
		     sp->sampling_format,
		     sp->sampling_rate,
		     sample_offset,
		     samples_per_line,
		     par->cri_frc >> par->frc_bits,
		     par->cri_frc_mask >> par->frc_bits,
		     par->cri_bits,
		     par->cri_rate,
		     cri_end,
		     (par->cri_frc & ((1U << par->frc_bits) - 1)),
		     par->frc_bits,
		     par->payload,
		     par->bit_rate,
		     par->modulation)) {
			assert (!"bit_slicer_set_params");
		}

		lines_containing_data (start, count, sp, par);

		if (!add_job_to_pattern (rd, job - rd->jobs, start, count)) {
			error (&rd->log,
			       "Out of decoder pattern space for "
			       "service 0x%08x (%s).",
			       par->id, par->label);
			continue;
		}

		job->id |= par->id;

		if (job >= rd->jobs + rd->n_jobs)
			++rd->n_jobs;

		rd->services |= par->id;
	}

	return rd->services;
}

vbi_service_set
vbi3_raw_decoder_services	(vbi3_raw_decoder *	rd)
{
	assert (NULL != rd);

	return rd->services;
}

/**
 * $param rd Pointer to a vbi3_raw_decoder object allocated with
 *   vbi3_raw_decoder_new().
 * $param sp New sampling parameters.
 * $param strict See vbi3_raw_decoder_add_services().
 *
 * Changes the sampling parameters used by $a rd. This will
 * remove all services which have been added with
 * vbi3_raw_decoder_add_services() but cannot be decoded with
 * the new sampling parameters.
 *
 * $return
 * Set of data services $rd will be decode after the change.
 * Can be zero if the sampling parameters are invalid or some
 * other error occured.
 */
/* Attn: strict must be int for compatibility with libzvbi 0.2 (-1 == 0) */
vbi_service_set
vbi3_raw_decoder_set_sampling_par
				(vbi3_raw_decoder *	rd,
				 const vbi_sampling_par *sp,
				 int			strict)
{
	unsigned int services;

	assert (NULL != rd);
	assert (NULL != sp);

	services = rd->services;

	vbi3_raw_decoder_reset (rd);

	if (!_vbi_sampling_par_valid_log (sp, &rd->log)) {
		CLEAR (rd->sampling);
		return 0;
	}

	rd->sampling = *sp;

	return vbi3_raw_decoder_add_services (rd, services, strict);
}

/**
 * $param rd Pointer to a vbi3_raw_decoder object allocated with
 *   vbi3_raw_decoder_new().
 * $param sp Sampling parameters will be stored here.
 *
 * Returns sampling parameters used by $a rd.
 */
void
vbi3_raw_decoder_get_sampling_par
				(const vbi3_raw_decoder *rd,
				 vbi_sampling_par *	sp)
{
	assert (NULL != rd);
	assert (NULL != sp);

	*sp = rd->sampling;
}

void
vbi3_raw_decoder_set_log_fn	(vbi3_raw_decoder *	rd,
				 vbi_log_fn *		log_fn,
				 void *			user_data,
				 vbi_log_mask		mask)
{
	assert (NULL != rd);

	if (NULL == log_fn)
		mask = 0;

	rd->log.fn = log_fn;
	rd->log.user_data = user_data;
	rd->log.mask = mask;
}

/**
 * $internal
 *
 * Free all resources associated with $a rd.
 */
void
_vbi3_raw_decoder_destroy	(vbi3_raw_decoder *	rd)
{
	vbi3_raw_decoder_reset (rd);

	/* Make unusable. */
	CLEAR (*rd);
}

/**
 * $internal
 * 
 * See vbi3_raw_decoder_new().
 */
vbi_bool
_vbi3_raw_decoder_init		(vbi3_raw_decoder *	rd,
				 const vbi_sampling_par *sp)
{
	CLEAR (*rd);

	vbi3_raw_decoder_reset (rd);

	if (RAW_DECODER_LOG) {
		vbi3_raw_decoder_set_log_fn (rd,
					     vbi_log_on_stderr,
					     /* user_data */ NULL,
					     /* mask */ VBI_LOG_INFO * 2 - 1);
	}

	if (NULL != sp) {
		if (!_vbi_sampling_par_valid_log (sp, &rd->log))
			return FALSE;

		rd->sampling = *sp;
	}

	return TRUE;
}

/**
 * $param rd Pointer to a vbi3_raw_decoder object allocated with
 *   vbi3_raw_decoder_new(), can be NULL
 *
 * Deletes a vbi3_raw_decoder object.
 */
void
vbi3_raw_decoder_delete		(vbi3_raw_decoder *	rd)
{
	if (NULL == rd)
		return;

	_vbi3_raw_decoder_destroy (rd);

	free (rd);
}

/**
 * $param sp VBI sampling parameters describing the raw VBI image
 *   to decode, can be $c NULL. If they are negotiatable you can determine
 *   suitable parameters with vbi_sampling_par_from_services(). You can
 *   change the sampling parameters later with
 *   vbi3_raw_decoder_set_sampling_par().
 *
 * Allocates a vbi3_raw_decoder object. To actually decode data
 * services you must request the data with vbi3_raw_decoder_add_services().
 *
 * $returns
 * NULL when out of memory or the sampling parameters are invalid,
 * Otherwise a pointer to an opaque vbi3_raw_decoder object which must
 * be deleted with vbi3_raw_decoder_delete() when done.
 */
vbi3_raw_decoder *
vbi3_raw_decoder_new		(const vbi_sampling_par *sp)
{
	vbi3_raw_decoder *rd;

	rd = malloc (sizeof (*rd));
	if (NULL == rd)
		return NULL;

	if (!_vbi3_raw_decoder_init (rd, sp)) {
		free (rd);
		rd = NULL;
	}

	return rd;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
