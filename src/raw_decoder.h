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

/* $Id: raw_decoder.h,v 1.1.2.7 2006-05-14 14:14:12 mschimek Exp $ */

#ifndef RAW_DECODER_H
#define RAW_DECODER_H

#include <stdio.h>
#include "macros.h"
#include "bit_slicer.h"

VBI3_BEGIN_DECLS

/**
 * @ingroup RawDecoder
 * @brief Raw VBI decoder.
 *
 * The contents of this structure are private.
 * Call vbi3_raw_decoder_new() to allocate a raw VBI decoder.
 */
typedef struct _vbi3_raw_decoder vbi3_raw_decoder;

/**
 * @addtogroup RawDecoder
 * @{
 */
extern vbi3_bool
vbi3_raw_decoder_get_point	(vbi3_raw_decoder *	rd,
				 vbi3_bit_slicer_point *point,
				 unsigned int		row,
				 unsigned int		nth_bit);
extern unsigned int
vbi3_raw_decoder_decode		(vbi3_raw_decoder *	rd,
				 vbi3_sliced *		sliced,
				 unsigned int		sliced_lines,
				 const uint8_t *	raw);
extern void
vbi3_raw_decoder_reset		(vbi3_raw_decoder *	rd);
extern vbi3_service_set
vbi3_raw_decoder_remove_services	(vbi3_raw_decoder *	rd,
				 vbi3_service_set	services);
extern vbi3_service_set
vbi3_raw_decoder_add_services	(vbi3_raw_decoder *	rd,
				 vbi3_service_set	services,
				 unsigned int		strict);
extern vbi3_bool
vbi3_raw_decoder_collect_points	(vbi3_raw_decoder *	rd,
				 vbi3_bool		enable);
extern vbi3_service_set
vbi3_raw_decoder_set_sampling_par
				(vbi3_raw_decoder *	rd,
				 const vbi3_sampling_par *sp,
				 unsigned int		strict);
extern void
vbi3_raw_decoder_get_sampling_par
				(const vbi3_raw_decoder *rd,
				 vbi3_sampling_par *	sp);
extern void
vbi3_raw_decoder_set_log_fn	(vbi3_raw_decoder *	rd,
				 vbi3_log_mask		mask,
				 vbi3_log_fn *		log_fn,
				 void *			user_data);
extern void
vbi3_raw_decoder_delete		(vbi3_raw_decoder *	rd);
extern vbi3_raw_decoder *
vbi3_raw_decoder_new		(const vbi3_sampling_par *sp);

/** @} */

/* Private */

/** @internal */
#define _VBI3_RAW_DECODER_MAX_JOBS 8
/** @internal */
#define _VBI3_RAW_DECODER_MAX_WAYS 8

/** @internal */
typedef struct {
	vbi3_service_set	id;
	vbi3_bit_slicer		slicer;
} _vbi3_raw_decoder_job;

/** @internal */
typedef struct {
	vbi3_bit_slicer_point   points[512];
	unsigned int		n_points;
} _vbi3_raw_decoder_sp_line;

/** @internal */
struct _vbi3_raw_decoder {
	vbi3_sampling_par	sampling;
	vbi3_service_set	services;
	vbi3_bool		collect_points;
	vbi3_log_fn *		log_fn;
	void *			log_user_data;
	vbi3_log_mask		log_mask;
	unsigned int		n_jobs;
	unsigned int		n_sp_lines;
	int			readjust;
	int8_t *		pattern;	/* n scan lines * MAX_WAYS */
	_vbi3_raw_decoder_job	jobs[_VBI3_RAW_DECODER_MAX_JOBS];
	_vbi3_raw_decoder_sp_line *sp_lines;
};

typedef enum {
	/** Requires field line numbers. */ 
	_VBI3_SP_LINE_NUM	= (1 << 0),
	/** Requires field numbers. */
	_VBI3_SP_FIELD_NUM	= (1 << 1),
} _vbi3_service_par_flag;

/* typedef vbi3_service_par in sliced.h */

/** @internal */
struct _vbi3_service_par {
	vbi3_service_set		id;
	const char *		label;

	/**
	 * Video standard
	 * - 525 lines, FV = 59.94 Hz, FH = 15734 Hz
	 * - 625 lines, FV = 50 Hz, FH = 15625 Hz
	 */
	vbi3_videostd_set	videostd_set;

	/**
	 * Most scan lines used by the data service, first and last
	 * line of first and second field. ITU-R numbering scheme.
	 * Zero if no data from this field, requires field sync.
	 */
	unsigned int		first[2];
        unsigned int		last[2];

	/**
	 * Leading edge hsync to leading edge first CRI one bit,
	 * half amplitude points, in nanoseconds.
	 */
	unsigned int		offset;

	unsigned int		cri_rate;	/**< Hz */
	unsigned int		bit_rate;	/**< Hz */

	/** Clock Run In and FRaming Code, LSB last txed bit of FRC. */
	unsigned int		cri_frc;

	/** CRI and FRC bits significant for identification. */
	unsigned int		cri_frc_mask;

	/**
	 * Number of significat cri_bits (at cri_rate),
	 * frc_bits (at bit_rate).
	 */
	unsigned int		cri_bits;
	unsigned int		frc_bits;

	unsigned int		payload;	/**< bits */
	vbi3_modulation		modulation;

	_vbi3_service_par_flag	flags;
};

extern const vbi3_service_par _vbi3_service_table [];

extern void
_vbi3_raw_decoder_dump		(const vbi3_raw_decoder *rd,
				 FILE *			fp);
extern void
_vbi3_raw_decoder_destroy	(vbi3_raw_decoder *	rd);
extern vbi3_bool
_vbi3_raw_decoder_init		(vbi3_raw_decoder *	rd,
				 const vbi3_sampling_par *sp);

VBI3_END_DECLS

#endif /* RAW_DECODER_H */
