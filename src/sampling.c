/*
 *  libzvbi - Raw vbi sampling
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

/* $Id: sampling.c,v 1.1.2.2 2004-02-13 02:11:50 mschimek Exp $ */

#include "raw_decoder.h"
#include "errno.h"

/**
 * @addtogroup Sampling Raw VBI sampling
 * @ingroup Raw
 * @brief Raw VBI data sampling interface.
 */

/**
 * @internal
 * @param sp Sampling parameters to verify.
 * 
 * @return
 * TRUE if the sampling parameters are valid (as far as we can tell).
 */
vbi_bool
vbi_sampling_par_verify		(const vbi_sampling_par *sp)
{
	unsigned int min_bpl;

	switch (sp->sampling_format) {
	case VBI_PIXFMT_YUV411:
	case VBI_PIXFMT_YVU411:
	case VBI_PIXFMT_YUV410:
	case VBI_PIXFMT_YVU410:
		if (sp->samples_per_line & 7)
			goto samples;
		break;

	case VBI_PIXFMT_YUV420:
	case VBI_PIXFMT_YVU420:
		if (sp->samples_per_line & 3)
			goto samples;
		break;

	case VBI_PIXFMT_YUV422:
	case VBI_PIXFMT_YVU422:
	case VBI_PIXFMT_YUYV:
	case VBI_PIXFMT_YVYU:
	case VBI_PIXFMT_UYVY:
	case VBI_PIXFMT_VYUY:
		if (sp->samples_per_line & 1)
			goto samples;
		break;

	default:
		break;
	}

	if (0 == sp->count[0]
	    && 0 == sp->count[1])
		goto range;

	if (VBI_VIDEOSTD_SET_525_60 & sp->videostd_set) {
		if (VBI_VIDEOSTD_SET_625_50 & sp->videostd_set)
			goto ambiguous;

		if (0 != sp->start[0])
			if ((sp->start[0] + sp->count[0]) > 265)
				goto range;

		if (0 != sp->start[1])
			if (sp->start[1] < 263
			    || (sp->start[1] + sp->count[0]) > 526)
				goto range;
	} else if (VBI_VIDEOSTD_SET_625_50 & sp->videostd_set) {
		if (0 != sp->start[0])
			if ((sp->start[0] + sp->count[0]) > 336)
				goto range;

		if (0 != sp->start[1])
			if (sp->start[1] < 310
			    || (sp->start[1] + sp->count[0]) > 626)
				goto range;
	} else {
	ambiguous:
		vbi_log_printf (__FUNCTION__,
				"Ambiguous videostd_set 0x%08llx",
				sp->videostd_set);
		return FALSE;
	}

	if (sp->interlaced
	    && sp->count[0] != sp->count[1]) {
		vbi_log_printf (__FUNCTION__,
				"Line count %u, %u must be equal and non-zero "
				"when interlaced",
				sp->count[0], sp->count[1]);
		return FALSE;
	}

	min_bpl = sp->samples_per_line
		* vbi_pixfmt_bytes_per_pixel (sp->sampling_format);

	if (sp->bytes_per_line < min_bpl) {
		vbi_log_printf (__FUNCTION__,
				"Invalid samples or bytes per line: %u, %u",
				sp->samples_per_line,
				sp->bytes_per_line);
		return FALSE;
	}

	return TRUE;

 samples:
	vbi_log_printf (__FUNCTION__,
			"Misaligned samples_per_line %u",
			sp->samples_per_line);
	return FALSE;

 range:
	vbi_log_printf (__FUNCTION__,
			"Invalid scan range %u-%u (%u lines), %u-%u (%u lines)",
			sp->start[0], sp->start[0] + sp->count[0] - 1, sp->count[0],
			sp->start[1], sp->start[1] + sp->count[1] - 1, sp->count[1]);
	return FALSE;
}

/**
 * @internal
 * @param sp Sampling parameters to check against.
 * @param par Data service to check.
 * @param strict See description of vbi_raw_decoder_add_services().
 *
 * Like vbi_sampling_parameters_check_services(), but checks
 * one service only.
 *
 * @return
 * TRUE if @a sp can decode @a par.
 */
vbi_bool
vbi_sampling_par_check_service	(const vbi_sampling_par *sp,
				 const vbi_service_par *par,
				 unsigned int		strict)
{
	double signal;
	unsigned int field;

	if (0 == (par->videostd_set & sp->videostd_set)) {
		vbi_log_printf (__FUNCTION__,
				"Service 0x%08x (%s) requires "
				"videostd_set 0x%08llx, have 0x%08llx",
				par->id, par->label,
				par->videostd_set,
				sp->videostd_set);
		return FALSE;
	}

	/* I've seen CC signals on other lines and there's no
	   way to distinguish from the transmitted data. */
	if ((VBI_SLICED_CAPTION_525 & par->id)
	    && (0 == sp->start[0] /* unknown */
		|| 0 == sp->start[1])) {
		vbi_log_printf (__FUNCTION__,
				"Service 0x%08x (%s) requires exact line numbers",
				par->id, par->label);
		return FALSE;
	}

	{
		unsigned int rate;

		rate = MAX (par->cri_rate, par->bit_rate);

		switch (par->id) {
		case VBI_SLICED_WSS_625:
			/* Effective bit rate is just 1/3 max_rate,
			   so 1 * max_rate should suffice. */
			break;

		default:
			rate = (rate * 3) >> 1;
			break;
		}

		if (rate > sp->sampling_rate) {
			vbi_log_printf (__FUNCTION__,
					"Sampling rate %f MHz too low "
					"for service 0x%08x (%s)",
					sp->sampling_rate / 1e6,
					par->id, par->label);
			return FALSE;
		}
	}

	signal = par->cri_bits / (double) par->cri_rate
		+ (par->frc_bits + par->payload) / (double) par->bit_rate;

	if (sp->offset > 0 && strict > 0) {
		double sampling_rate;
		double offset;
		double end;

		sampling_rate = (double) sp->sampling_rate;

		offset = sp->offset / sampling_rate;
		end = (sp->offset + sp->samples_per_line) / sampling_rate;

		if (offset > (par->offset / 1e3 - 0.5e-6)) {
			vbi_log_printf (__FUNCTION__,
					"Sampling offset %f us too late "
					"for service 0x%08x (%s) at %f us",
					offset * 1e6,
					par->id, par->label,
					par->offset / 1e3);
			return FALSE;
		}

		if (end < (par->offset / 1e9 + signal + 0.5e-6)) {
			vbi_log_printf (__FUNCTION__,
					"Sampling ends too early at %f us "
					"for service 0x%08x (%s) up to %f us",
					end * 1e6,
					par->id, par->label,
					par->offset / 1e3
					 + signal * 1e6 + 0.5);
			return FALSE;
		}
	} else {
		double samples;

		samples = sp->samples_per_line / (double) sp->sampling_rate;

		if (samples < (signal + 1.0e-6)) {
			vbi_log_printf (__FUNCTION__,
					"Service 0x%08x (%s) signal %f us "
					"exceeds %f us sampling period",
					par->id, par->label,
					signal * 1e6, samples * 1e6);
			return FALSE;
		}
	}

	if (!sp->synchronous) {
		/* Not always, but it's too difficult now to bother. */
		vbi_log_printf (__FUNCTION__,
				"Service 0x%08x (%s) requires "
				"synchronous field order",
				par->id, par->label);
		return FALSE;
	}

	for (field = 0; field < 2; ++field) {
		unsigned int start;
		unsigned int end;

		start = sp->start[field];
		end = start + sp->count[field] - 1;

		if (0 == par->first[field]
		    || 0 == par->last[field]) {
			/* No data on this field. */
			continue;
		}

		if (0 == sp->count[field]) {
			vbi_log_printf (__FUNCTION__,
					"Service 0x%08x (%s) requires "
					"data from field %u",
					par->id, par->label, field);
			return FALSE;
		}

		if (sp->start[field] > 0 && strict > 0) {
			/* May succeed if not all scanning lines
			   available for the service are actually used. */
			if (strict > 1
			    || (par->first[field] == par->last[field]))
				if (start > par->first[field]
				    || end < par->last[field]) {
					vbi_log_printf (__FUNCTION__,
							"Service 0x%08x (%s) requires "
							"lines %u-%u, have %u-%u",
							par->id, par->label,
							par->first[field],
							par->last[field],
							start, end);
					return FALSE;
				}
		}
	}

	return TRUE;
}

/**
 * @param sp Sampling parameters to check against.
 * @param services Set of data services.
 * @param strict See description of vbi_raw_decoder_add_services().
 *
 * Check which of the given services can be decoded with the given
 * sampling parameters at the given strictness level.
 *
 * @return
 * Subset of @a services decodable with the given sampling parameters.
 */
vbi_service_set
vbi_sampling_par_check_services	(const vbi_sampling_par *sp,
				 vbi_service_set	services,
				 unsigned int		strict)
{
	const vbi_service_par *par;
	vbi_service_set rservices;

	assert (NULL != sp);

	rservices = 0;

	for (par = vbi_service_table; par->id; ++par) {
		if (par->id & services)
			if (vbi_sampling_par_check_service (sp, par, strict))
				rservices |= par->id;
	}

	return rservices;
}

/**
 * @param sp Sampling parameters calculated by this function
 *   will be stored here.
 * @param max_rate If not NULL, the highest data bit rate in Hz of
 *   all services requested will be stored here. The sampling rate
 *   should be at least twice as high; @sp sampling_rate will
 *   be set to a more reasonable value of 27 MHz, which is twice
 *   the video sampling rate defined by ITU-R Rec. BT.601.
 * @param videostd_set Create sampling parameters matching these
 *   video standards. When 0 determine video standard from requested
 *   services.
 * @param services Set of VBI_SLICED_ symbols. Here (and only here) you
 *   can add @c VBI_SLICED_VBI_625 or @c VBI_SLICED_VBI_525 to include all
 *   vbi scan lines in the calculated sampling parameters.
 *
 * Calculate the sampling parameters required to receive and decode the
 * requested data @a services. The @a sp sampling_format will be
 * @c VBI_PIXFMT_Y8, offset and bytes_per_line will be set to
 * reasonable minimums. This function can be used to initialize hardware
 * prior to creating a vbi_raw_decoder object.
 * 
 * @return
 * Subset of @a services covered by the calculated sampling parameters.
 */
vbi_service_set
vbi_sampling_par_from_services	(vbi_sampling_par *	sp,
				 unsigned int *		max_rate,
				 vbi_videostd_set	videostd_set,
				 vbi_service_set	services)
{
	const vbi_service_par *par;
	vbi_service_set rservices;
	unsigned int rate;

	assert (NULL != sp);

	if (0 != videostd_set) {
		if (0 == (VBI_VIDEOSTD_SET_ALL & videostd_set)
		    || ((VBI_VIDEOSTD_SET_525_60 & videostd_set)
			&& (VBI_VIDEOSTD_SET_625_50 & videostd_set))) {
			vbi_log_printf (__FUNCTION__,
					"Ambiguous videostd_set 0x%08llx",
					videostd_set);
			CLEAR (*sp);
			return 0;
		}
	}

	sp->videostd_set	= videostd_set;
	sp->sampling_format	= VBI_PIXFMT_Y8;
	sp->sampling_rate	= 27000000;		/* ITU-R BT.601 */
	sp->samples_per_line	= 0;
	sp->offset		= (int)(64e-6 * sp->sampling_rate);
	sp->start[0]		= 30000;
	sp->count[0]		= 0;
	sp->start[1]		= 30000;
	sp->count[1]		= 0;
	sp->interlaced		= FALSE;
	sp->synchronous		= TRUE;

	rservices = 0;
	rate = 0;

	for (par = vbi_service_table; par->id; ++par) {
		double margin;
		double signal;
		int offset;
		unsigned int samples;
		unsigned int i;

		if (0 == (par->id & services))
			continue;

		if (0 == videostd_set) {
			vbi_videostd_set set;

			set = par->videostd_set | sp->videostd_set;

			if (0 == (VBI_VIDEOSTD_SET_525_60 & set)
			    || 0 == (VBI_VIDEOSTD_SET_625_50 & set))
				sp->videostd_set = set;
		}

		if (VBI_VIDEOSTD_SET_525_60 & sp->videostd_set)
			margin = 1.0e-6;
		else
			margin = 2.0e-6;

		if (0 == (par->videostd_set & sp->videostd_set)) {
			vbi_log_printf (__FUNCTION__,
					"Service 0x%08x (%s) requires "
					"videostd_set 0x%08llx, have 0x%08llx",
					par->id, par->label,
					par->videostd_set,
					sp->videostd_set);
			continue;
		}

		rate = MAX (rate, par->cri_rate);
		rate = MAX (rate, par->bit_rate);

		signal = par->cri_bits / (double) par->cri_rate
			+ ((par->frc_bits + par->payload) / (double) par->bit_rate);

		offset = (int)((par->offset / 1e9) * sp->sampling_rate);
		samples = (int)((signal + 1.0e-6) * sp->sampling_rate);

		sp->offset = MIN (sp->offset, offset);

		sp->samples_per_line = MAX (sp->samples_per_line + sp->offset,
					    samples + offset) - sp->offset;

		for (i = 0; i < 2; ++i)
			if (par->first[i] > 0
			    && par->last[i] > 0) {
				sp->start[i] = MIN (sp->start[i], (unsigned int) par->first[i]);
				sp->count[i] = MAX (sp->start[i] + sp->count[i],
						    (unsigned int) par->last[i] + 1)
					- sp->start[i];
			}

		rservices |= par->id;
	}

	if (0 == rservices) {
		CLEAR (*sp);
		return 0;
	}

	if (0 == sp->count[1]) {
		sp->start[1] = 0;

		if (0 == sp->count[0]) {
			sp->start[0] = 0;
			sp->offset = 0;
		}
	} else if (0 == sp->count[0]) {
		sp->start[0] = 0;
	}

	sp->bytes_per_line = sp->samples_per_line;

	if (max_rate)
		*max_rate = rate;

	return rservices;
}

/**
 * @param videostd A video standard number.
 *
 * Returns the name of a video standard like VBI_VIDEOSTD_PAL_B ->
 * "PAL_B". This is mainly intended for debugging.
 * 
 * @return
 * Static ASCII string, NULL if @a videostd is a custom standard
 * or invalid.
 */
const char *
vbi_videostd_name		(vbi_videostd		videostd)
{
	switch (videostd) {

#undef CASE
#define CASE(std) case VBI_VIDEOSTD_##std : return #std ;

     	CASE (NONE)
     	CASE (PAL_B)
	CASE (PAL_B1)
	CASE (PAL_G)
	CASE (PAL_H)
	CASE (PAL_I)
	CASE (PAL_D)
	CASE (PAL_D1)
	CASE (PAL_K)
	CASE (PAL_M)
	CASE (PAL_N)
	CASE (PAL_NC)
	CASE (NTSC_M)
	CASE (NTSC_M_JP)
	CASE (SECAM_B)
	CASE (SECAM_D)
	CASE (SECAM_G)
	CASE (SECAM_H)
	CASE (SECAM_K)
	CASE (SECAM_K1)
	CASE (SECAM_L)

	case VBI_VIDEOSTD_CUSTOM_BEGIN ... VBI_VIDEOSTD_CUSTOM_END:
		return "CUSTOM";

		/* No default, gcc warns. */
	}

	return NULL;
}