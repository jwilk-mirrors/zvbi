/*
 *  libzvbi
 *
 *  Copyright (C) 2004, 2006 Michael H. Schimek
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

/* $Id: dvb_demux.c,v 1.13 2006-05-26 00:45:53 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "misc.h"		/* CLEAR() */
#include "hamm.h"		/* vbi_rev8() */
#include "dvb.h"
#include "dvb_demux.h"

/**
 * @addtogroup DVBDemux DVB VBI demultiplexer
 * @ingroup Raw
 * @brief Separating VBI data from a DVB PES stream
 *   (EN 301 472, EN 301 775).
 */

struct wrap {
	/* Size must be >= maximum consume + maximum lookahead. */
	uint8_t	*		buffer;

	/* End of data in buffer. */
	uint8_t *		bp;

	/* See below. */
	unsigned int		skip;
     /* unsigned int		consume; */
	unsigned int		lookahead;

	/* Unconsumed data in buffer, starting at bp[-leftover]. */
	unsigned int		leftover;
};

/**
 * @internal
 * @param w Wrap-around context.
 * @param dst Wrapped data pointer.
 * @param scan_end End of lookahead range.
 * @param src Source buffer pointer, will be incremented.
 * @param src_left Bytes left in source buffer, will be decremented.
 * @param src_size Size of source buffer.
 *
 * A buffer is assumed in memory at *src + *src_left - src_size, with
 * src_size. This function reads at most *src_left bytes from this
 * buffer starting at *src, incrementing *src and decrementing *src_left
 * by the number of bytes read. NOTE *src_left must be equal to src_size
 * when you change buffers.
 *
 * It removes (reads) w->skip bytes from the buffer and sets w->skip to
 * zero, then removes w->consume bytes (not implemented at this time,
 * assumed to be zero), copying the data AND the following w->lookahead
 * bytes to an output buffer. In other words, *src is incremented by
 * at most w->skip + w->consume bytes.
 *
 * On success TRUE is returned, *dst will point to the begin of the
 * copied data (w->consume + w->lookahead), *scan_end to the end.
 * However *scan_end - *dst can be greater than w->consume + w->lookahead
 * if *src_left permits this. NOTE if copying can be avoided *dst and
 * *scan_end may point into the source buffer, so don't free /
 * overwrite it prematurely. *src_left will be >= 0.
 *
 * w->skip, w->consume and w->lookahead can change between successful
 * calls.
 *
 * If more data is needed the function returns FALSE, and *src_left
 * will be 0.
 */
vbi_inline vbi_bool
wrap_around			(struct wrap *		w,
				 const uint8_t **	dst,
				 const uint8_t **	scan_end,
				 const uint8_t **	src,
				 unsigned int *		src_left,
				 unsigned int		src_size)
{
	unsigned int available;
	unsigned int required;

	if (w->skip > 0) {
		/* w->skip is not w->consume to save copying. */

		if (w->skip > w->leftover) {
			w->skip -= w->leftover;
			w->leftover = 0;

			if (w->skip > *src_left) {
				w->skip -= *src_left;

				*src += *src_left;
				*src_left = 0;

				return FALSE;
			}

			*src += w->skip;
			*src_left -= w->skip;
		} else {
			w->leftover -= w->skip;
		}

		w->skip = 0;
	}

	available = w->leftover + *src_left;
	required = /* w->consume + */ w->lookahead;

	if (required > available || available > src_size) {
		/* Not enough data at s, or we have bytes left
		   over from the previous buffer, must wrap. */

		if (required > w->leftover) {
			/* Need more data in the wrap_buffer. */

			memmove (w->buffer, w->bp - w->leftover, w->leftover);
			w->bp = w->buffer + w->leftover;

			required -= w->leftover;

			if (required > *src_left) {
				memcpy (w->bp, *src, *src_left);
				w->bp += *src_left;

				w->leftover += *src_left;

				*src += *src_left;
				*src_left = 0;

				return FALSE;
			}

			memcpy (w->bp, *src, required);
			w->bp += required;

			w->leftover = w->lookahead;

			*src += required;
			*src_left -= required;

			*dst = w->buffer;
			*scan_end = w->bp - w->lookahead;
		} else {
			*dst = w->bp - w->leftover;
			*scan_end = w->bp - w->lookahead;

			/* w->leftover -= w->consume; */
		}
	} else {
		/* All the required bytes are in this frame and
		   we have a complete copy of the w->buffer
		   leftover bytes before s. */

		*dst = *src - w->leftover;
		*scan_end = *src + *src_left - w->lookahead;

		/* if (w->consume > w->leftover) {
			unsigned int advance;

			advance = w->consume - w->leftover;

			*src += advance;
			*src_left -= advance;

			w->leftover = 0;
		} else {
			w->leftover -= w->consume;
		} */
	}

	return TRUE;
}

/** @internal */
struct frame {
	/* Buffers for sliced and raw data of one frame. */

	vbi_sliced *		sliced_begin;
	vbi_sliced *		sliced_end;

	uint8_t *		raw;

	/* XXX replace by vbi_sampling_par. */
	unsigned int		raw_start[2];
	unsigned int		raw_count[2];

	/* Current position. */

	vbi_sliced *		sp;

	unsigned int		last_line;

	/* Number of lines extracted from current packet. */
	unsigned int		sliced_count;

	uint8_t *		rp;
	vbi_sliced *		raw_sp;
	unsigned int		raw_offset;
};

/**
 * @internal
 * Minimum lookahead to identify the packet header.
 */
#define HEADER_LOOKAHEAD 48

/** @internal */
struct _vbi_dvb_demux {
	/** Wrap-around buffer. Must hold one PES packet,
	    at most 6 + 65535 bytes. */
	uint8_t			buffer[65536 + 16];

	/** Output buffer for vbi_dvb_demux_demux(). */
	vbi_sliced		sliced[64];

	/** Wrap-around state. */
	struct wrap		wrap;

	/** Data unit demux state. */
	struct frame		frame;

	/** PTS of current frame. */
	int64_t			frame_pts;

	/** PTS of current packet. */
	int64_t			packet_pts;

	/** New frame commences in this packet. (We cannot reset
	    immediately due to the coroutine design.) */
	vbi_bool		new_frame;

	/** vbi_dvb_demux_demux() data. */
	vbi_dvb_demux_cb *	callback;
	void *			user_data;

	_vbi_log_hook		log;
};

/* Converts the line_offset and field_parity byte. */
vbi_inline unsigned int
lofp_to_line			(unsigned int		lofp,
				 unsigned int		system)
{
	static const unsigned int start [2][2] = {
		{ 0, 263 },
		{ 0, 313 },
	};
	unsigned int line_offset;

	line_offset = lofp & 31;

	if (line_offset > 0) {
		unsigned int field_parity;

		field_parity = !(lofp & (1 << 5));

		return line_offset + start[system][field_parity];
	} else {
		/* Unknown line. */
		return 0;
	}
}

static vbi_sliced *
line_address			(vbi_dvb_demux *	dx,
				 unsigned int		lofp,
				 unsigned int		system,
				 vbi_bool		raw)
{
	struct frame *f = &dx->frame;
	unsigned int line;
	vbi_sliced *s;

	if (f->sp >= f->sliced_end) {
		error (&dx->log,
		       "Out of buffer space (%d lines).",
		       (int)(f->sliced_end - f->sliced_begin));
		return NULL;
	}

	line = lofp_to_line (lofp, system);
	debug2 (&dx->log, "Line %u.", line);

	if (line > 0) {
		if (raw) {
			unsigned int field;

			field = (line >= f->raw_start[1]);

			if (line < f->raw_start[0]
			    || line >= (f->raw_start[field]
					+ f->raw_count[field])) {
				notice (&dx->log,
					"Raw line %u outside sampling range "
					"%u ... %u, %u ... %u.",
					line,
					f->raw_start[0],
					f->raw_start[0] + f->raw_count[0],
					f->raw_start[1],
					f->raw_start[1] + f->raw_count[1]);

				return NULL;
			} else if (0 != field) {
				f->rp = f->raw
					+ (line + f->raw_count[0]) * 720;
			} else {
				f->rp = f->raw + line * 720;
			}
		}

		if (line > f->last_line) {
			f->last_line = line;

			s = f->sp++;
			s->line = line;
		} else {
			/* EN 301 775 section 4.1: ascending line
			   order, no line twice. */

			/* When the first line number in a packet is
			   smaller than the last line number in the
			   previous packet the frame is complete. */
			if (0 == f->sliced_count)
				return NULL;

			notice (&dx->log,
				"Illegal line order %u >= %u.",
				line, f->last_line);

			return NULL;

			for (s = f->sliced_begin; s < f->sp; ++s)
				if (line <= s->line)
					break;

			if (line < s->line) {
				memmove (s, s + 1, (f->sp - s) * sizeof (*s));
				++f->sp;
				s->line = line;
			}

			if (raw) {
				memset (f->rp, 0, 720);
			}
		}
	} else {
		/* Unknown line. */

		if (0 == f->sliced_count
		    && f->last_line > 0)
			return NULL;

		++f->last_line;

		s = f->sp++;
		s->line = 0;

		f->rp += raw;
	}

	++f->sliced_count;

	return s;
}

#if 0 /* not used yet */

static void
discard_raw			(struct frame *		f)
{
	debug2 (&dx->log, "Discard raw VBI packet.");

	memset (f->rp, 0, 720);

	memmove (f->raw_sp + 1, f->raw_sp,
		 (f->sp - f->raw_sp - 1) * sizeof (vbi_sliced));
	--f->sp;

	f->raw_sp = NULL;
	f->raw_offset = 0;
}

static int
demux_samples			(struct frame *		f,
				 uint8_t *		p,
				 unsigned int		system)
{
	vbi_sliced *s;
	unsigned int offset;
	unsigned int n_pixels;

	assert (0);
	s = NULL; /* FIXME */

	offset = p[3] * 256 + p[4];
	n_pixels = p[5];

	debug2 (&dx->log,
		"Raw VBI packet offset=%u n_pixels=%u "
		"first_segment=%u last_segment=%u.",
		offset, n_pixels,
		!!(p[2] & (1 << 7)),
		!!(p[2] & (1 << 6)));

	/* n_pixels <= 251 has been checked by caller. */
	if (0 == n_pixels || (offset + n_pixels) > 720) {
		notice (&dx->log,
			"Illegal segment size %u ... %u (%u pixels).",
			offset, offset + n_pixels, n_pixels);

		discard_raw (f);

		return 0;
	}

	if (p[2] & (1 << 7)) {
		/* First segment. */

		if (f->raw_offset > 0) {
			debug2 (&dx->log,
				"Last segment missing, line %u, offset %u.",
				f->raw_sp->line, f->raw_offset);

			discard_raw (f);

			/* Recoverable error. */
		}

		if (!(f->raw_sp = line_address (f, p[2], system, TRUE))) {
			if (0 == f->sliced_count)
				return -1; /* is a new frame */
			else
				return 0; /* bad packet */
		}

		s->id = VBI_SLICED_NONE;
	} else {
		unsigned int line;

		line = lofp_to_line (p[2], system);

		if (0 == f->raw_offset) {
			debug2 (&dx->log,
				"First segment missing of line %u, "
				"offset %u.",
				line, offset);

			/* Recoverable error. */
			return 1;
		} else if (line != f->raw_sp->line
			   || offset != f->raw_offset) {
			debug2 (&dx->log,
				"Segment(s) missing or out of order, "
				"expected line=%u offset=%u, "
				"got line=%u offset=%u.",
				f->raw_sp->line, f->raw_offset,
				line, offset);

			discard_raw (f);

			/* Recoverable error. */
			return 1;
		}
	}

	memcpy (f->rp + offset, p + 6, n_pixels);

	if (p[2] & (1 << 6)) {
		/* Last segment. */
		f->raw_offset = 0;
	} else {
		f->raw_offset = offset + n_pixels;
	}

	return TRUE;
}

#endif /* 0 */

static void
log_du_ttx			(vbi_dvb_demux *	dx,
				 const vbi_sliced *	s)
{
	uint8_t buffer[43];
	unsigned int i;

	for (i = 0; i < 42; ++i)
		buffer[i] = _vbi_to_ascii (s->data[i]);
	buffer[i] = 0;

	debug2 (&dx->log, "DU-TTX %u >%s<", s->line, buffer);
}

static int
demux_data_units		(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	struct frame *f = &dx->frame;
	const uint8_t *p;
	const uint8_t *end2;
	int r;

	assert (*src_left >= 2);

	r = 0; /* bad packet */

	p = *src;
	end2 = p + *src_left
		- 2 /* data_unit_id,
		       data_unit_length */;

	while (p < end2) {
		unsigned int data_unit_id;
		unsigned int data_unit_length;
		vbi_sliced *s;
		unsigned int i;

		data_unit_id = p[0];
		data_unit_length = p[1];

		debug2 (&dx->log,
			"data_unit_id=0x%02x data_unit_length=%u.",
			data_unit_id, data_unit_length);

		/* EN 301 775 section 4.3.1: Data units
		   must not cross PES packet boundaries. */
		if (p + data_unit_length > end2)
			goto failure;

		switch (data_unit_id) {
		case DATA_UNIT_STUFFING:
			break;

		case DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE:
		case DATA_UNIT_EBU_TELETEXT_SUBTITLE:
			if (data_unit_length < 1 + 1 + 42) {
			bad_length:
				notice (&dx->log,
					"data_unit_length=%u too small "
					"for data_unit_id=%u.",
					data_unit_length, data_unit_id);

				goto failure;
			}

			/* We cannot transcode custom framing codes. */
			if (0xE4 != p[3]) /* vbi_rev8 (0x27) */
				break;

			if (!(s = line_address (dx, p[2], 1, FALSE)))
				goto no_line;

			s->id = VBI_SLICED_TELETEXT_B;

			for (i = 0; i < 42; ++i)
				s->data[i] = vbi_rev8 (p[4 + i]);

			if (dx->log.mask & VBI_LOG_DEBUG2)
				log_du_ttx (dx, s);

			break;

		case DATA_UNIT_VPS:
			if (data_unit_length < 1 + 13)
				goto bad_length;

			if (!(s = line_address (dx, p[2], 1, FALSE)))
				goto no_line;

			s->id = (s->line >= 313) ?
				VBI_SLICED_VPS : VBI_SLICED_VPS;

			for (i = 0; i < 13; ++i)
				s->data[i] = p[3 + i];

			break;

		case DATA_UNIT_WSS:
			if (data_unit_length < 1 + 2)
				goto bad_length;

			if (!(s = line_address (dx, p[2], 1, FALSE)))
				goto no_line;

			s->id = VBI_SLICED_WSS_625;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_ZVBI_WSS_CPR1204:
			if (data_unit_length < 1 + 3)
				goto bad_length;

			if (!(s = line_address (dx, p[2], 0, FALSE)))
				goto no_line;

			s->id = VBI_SLICED_WSS_CPR1204;

			s->data[0] = p[3];
			s->data[1] = p[4];
			s->data[2] = p[5];

			break;

		case DATA_UNIT_ZVBI_CLOSED_CAPTION_525:
			if (data_unit_length < 1 + 2)
				goto bad_length;

			if (!(s = line_address (dx, p[2], 0, FALSE)))
				goto no_line;

			s->id = VBI_SLICED_CAPTION_525;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_CLOSED_CAPTION:
			if (data_unit_length < 1 + 2)
				goto bad_length;

			if (!(s = line_address (dx, p[2], 1, FALSE)))
				goto no_line;

			s->id = VBI_SLICED_CAPTION_625;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

#if 0 /* later */
		case DATA_UNIT_MONOCHROME_SAMPLES:
			if (data_unit_length < 1 + 2 + 1 + p[5]) {
			bad_sample_length:
				notice (&dx->log,
					"data_unit_length=%u too small "
					"for data_unit_id=%u with %u "
					"samples.",
					data_unit_length,
					data_unit_id, p[5]);

				goto failure;
			}

			if ((r = demux_samples (f, p, 1)) < 1)
				goto failure;

			break;

		case DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525:
			if (data_unit_length < 1 + 2 + 1 + p[5])
				goto bad_sample_length;

			if ((r = demux_samples (f, p, 0)) < 1)
				goto failure;

			break;
#endif

		default:
			notice (&dx->log,
				"Unknown data_unit_id=%u.",
				data_unit_id);
			break;
		}

		p += data_unit_length + 2;
	}

	*src = p;
	*src_left = 0;

	return 1; /* success */

 no_line:
	if (0 == f->sliced_count)
		r = -1; /* is a new frame */

 failure:
	*src_left = end2 + 2 - p;
	*src = p;

	return r;
}

vbi_inline void
reset_frame			(struct frame *		f)
{
	f->sp = f->sliced_begin;

	f->last_line = 0;
	f->sliced_count = 0;

	if (f->rp > f->raw) {
		unsigned int lines;

		lines = f->raw_count[0] + f->raw_count[1];
		memset (f->raw, 0, lines * 720);
	}

	f->rp = f->raw;
	f->raw_sp = NULL;
	f->raw_offset = 0;
}



/*
  	Add _vbi_dvb_demultiplex() here.
*/

static vbi_bool
timestamp			(vbi_dvb_demux *	dx,
				 int64_t *		pts,
				 unsigned int		mark,
				 const uint8_t *	p)
{
	unsigned int t;

	if (mark != (p[0] & 0xF1u))
		return FALSE;

	t  = p[1] << 22;
	t |= (p[2] & ~1) << 14;
	t |= p[3] << 7;
	t |= p[4] >> 1;

	if (dx->log.mask & VBI_LOG_DEBUG) {
		int64_t old_pts;
		int64_t new_pts;

		old_pts = *pts;
		new_pts = t | (((int64_t) p[0] & 0x0E) << 29);

		debug1 (&dx->log,
			"TS%x 0x%" PRIx64 " (%+" PRId64 ").",
			mark, new_pts, new_pts - old_pts);
	}

	*pts = t | (((int64_t) p[0] & 0x0E) << 29);

	return TRUE;
}

static vbi_bool
demux_packet			(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	const uint8_t *s;
	unsigned int s_left;

	s = *src;
	s_left = *src_left;

	for (;;) {
		const uint8_t *p;
		const uint8_t *scan_begin;
		const uint8_t *scan_end;
		unsigned int packet_length;
		unsigned int header_length;

		if (!wrap_around (&dx->wrap,
				  &p, &scan_end,
				  &s, &s_left, *src_left))
			break; /* out of data */

		/* Data units */

		if (dx->wrap.lookahead > HEADER_LOOKAHEAD) {
			unsigned int left;

			dx->frame.sliced_count = 0;

			left = dx->wrap.lookahead;

			for (;;) {
				unsigned int lines;
				int r;

				if (dx->new_frame) {
					/* New frame commences
					   in this packet. */

					reset_frame (&dx->frame);

					dx->frame_pts = dx->packet_pts;
					dx->new_frame = FALSE;
				}

				r = demux_data_units (dx, &p, &left);

				if (0 == r) {
					debug1 (&dx->log,
						"Bad packet, discard.");
					dx->new_frame = TRUE;
					break;
				}

				if (r > 0) {
					/* Data unit extraction successful.
					   Packet continues previous frame. */
					break;
				}

				debug1 (&dx->log, "New frame.");

				/* A new frame commences in this packet.
				   We must flush dx->frame before we extract
				   data units from this packet. */

				/* Must not change dx->frame or dx->frame_pts
				   here to permit "pass by return". */
				dx->new_frame = TRUE;

				if (!dx->callback)
					goto failure;

				lines = dx->frame.sp - dx->frame.sliced_begin;

				if (!dx->callback (dx,
						   dx->user_data,
						   dx->frame.sliced_begin,
						   lines,
						   dx->frame_pts))
					goto failure;
			}

			dx->wrap.skip = dx->wrap.lookahead;
			dx->wrap.lookahead = HEADER_LOOKAHEAD;

			continue;
		}

		/* Start code scan */

		scan_begin = p;

		for (;;) {
			/* packet_start_code_prefix [24] == 0x000001,
			   stream_id [8] == PRIVATE_STREAM_1 */

			debug1 (&dx->log,
				"packet_start_code=%02x%02x%02x%02x.",
				p[0], p[1], p[2], p[3]);

			if (likely (p[2] & ~1)) {
				/* Not 000001 or xx0000 or xxxx00. */
				p += 3;
			} else if (0 != (p[0] | p[1]) || 1 != p[2]) {
				++p;
			} else if (PRIVATE_STREAM_1 == p[3]) {
				break;
			} else if (p[3] >= 0xBC) {
				packet_length = p[4] * 256 + p[5];
				dx->wrap.skip = (p - scan_begin)
					+ 6 + packet_length;
				goto outer_continue;
			}

			if (unlikely (p >= scan_end)) {
				dx->wrap.skip = p - scan_begin;
				goto outer_continue;
			}
		}

		/* Packet header */

		packet_length = p[4] * 256 + p[5];

		debug1 (&dx->log,
			"packet_length=%u.",
			packet_length);

		dx->wrap.skip = (p - scan_begin) + 6 + packet_length;

		/* EN 300 472 section 4.2: N x 184 - 6. (We'll read
		   46 bytes without further checks and need at least
		   one data unit to function properly.) */
		if (packet_length < 178)
			continue;

		/* PES_header_data_length [8] */
		header_length = p[8];

		debug1 (&dx->log,
			"header_length=%u.",
			header_length);

		/* EN 300 472 section 4.2: 0x24. */
		if (36 != header_length)
			continue;

		debug1 (&dx->log,
			"data_identifier=%u.",
			p[9 + 36]);

		/* data_identifier (EN 301 775 section 4.3.2) */
		switch (p[9 + 36]) {
		case 0x10 ... 0x1F:
		case 0x99 ... 0x9B:
			break;

		default:
			continue;
		}

		/* '10', PES_scrambling_control [2] == 0 (not scrambled),
		   PES_priority, data_alignment_indicator == 1 (data unit
		   starts immediately after header),
		   copyright, original_or_copy */
		if (0x84 != (p[6] & 0xF4))
			continue;

		/* PTS_DTS_flags [2], ESCR_flag, ES_rate_flag,
		   DSM_trick_mode_flag, additional_copy_info_flag,
		   PES_CRC_flag, PES_extension_flag */
		switch (p[7] >> 6) {
		case 2:	/* PTS 0010 xxx 1 ... */
			if (!timestamp (dx, &dx->packet_pts, 0x21, p + 9))
				continue;
			break;

		case 3:	/* PTS 0011 xxx 1 ... DTS ... */
			if (!timestamp (dx, &dx->packet_pts, 0x31, p + 9))
				continue;
			break;

		default:
			/* EN 300 472 section 4.2: a VBI PES packet [...]
			   always carries a PTS. */
			/* But it doesn't matter if this packet continues
			   the previous frame. */
			if (dx->new_frame)
				continue;
			break;
		}

		/* Habemus packet. */

		dx->wrap.skip = (p - scan_begin) + 9 + 36 + 1;
		dx->wrap.lookahead = packet_length - 3 - 36 - 1;

 outer_continue:
		;
	}

	*src = s;
	*src_left = s_left;

	return TRUE;

 failure:
	*src = s;
	*src_left = s_left;

	return FALSE;
}

/**
 * @brief DVB VBI demux coroutine.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 * @param sliced Demultiplexed sliced data will be stored here. You must
 *   not change @a sliced and @a sliced_lines in successive calls until
 *   a frame is complete (i.e. the function returns a value > 0).
 * @param sliced_lines At most this number of sliced lines will be stored
 *   at @a sliced.
 * @param pts If not @c NULL the Presentation Time Stamp associated with the
 *   first line of the demultiplexed frame will be stored here.
 * @param buffer *buffer points to DVB PES data, will be incremented by the
 *   number of bytes read from the buffer. This pointer need not align with
 *   packet boundaries.
 * @param buffer_left *buffer_left is the number of bytes left in @a buffer,
 *   will be decremented by the number of bytes read. *buffer_left need not
 *   align with packet size. The packet filter works faster with larger
 *   buffers. When you read from an MPEG file, mapping the file into memory
 *   and passing pointers to the mapped data will be fastest.
 *
 * This function takes an arbitrary number of DVB PES data bytes, filters
 * out PRIVATE_STREAM_1 packets, filters out valid VBI data units, converts
 * them to vbi_sliced format and stores the sliced data at @a sliced.
 *
 * @returns
 * The number of sliced lines stored at @a sliced when a frame is complete,
 * @c 0 if more data is needed (@a *buffer_left is @c 0) or the data
 * contained errors.
 *
 * @bug
 * Demultiplexing of raw VBI data is not supported yet,
 * raw data will be discarded.
 *
 * @since 0.2.10
 */
unsigned int
vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		sliced_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left)
{
	assert (NULL != dx);
	assert (NULL != sliced);
	assert (NULL != buffer);
	assert (NULL != buffer_left);

	dx->frame.sliced_begin = sliced;
	dx->frame.sliced_end = sliced + sliced_lines;

	if (!demux_packet (dx, buffer, buffer_left)) {
		if (pts)
			*pts = dx->frame_pts;

		return dx->frame.sp - dx->frame.sliced_begin;
	}

	return 0;
}

/**
 * @brief Feeds DVB VBI demux with data.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 * @param buffer DVB PES data, need not align with packet boundaries.
 * @param buffer_size Number of bytes in @a buffer, need not align with
 *   packet size. The packet filter works faster with larger buffers.
 *
 * This function takes an arbitrary number of DVB PES data bytes, filters
 * out PRIVATE_STREAM_1 packets, filters out valid VBI data units, converts
 * them to vbi_sliced format and calls the vbi_dvb_demux_cb function given
 * to vbi_dvb_pes_demux_new() when a new frame is complete.
 *
 * @returns
 * @c FALSE if the data contained errors.
 *
 * @bug
 * Demultiplexing of raw VBI data is not supported yet,
 * raw data will be discarded.
 *
 * @since 0.2.10
 */
vbi_bool
vbi_dvb_demux_feed		(vbi_dvb_demux *	dx,
				 const uint8_t *	buffer,
				 unsigned int		buffer_size)
{
	vbi_bool success;

	assert (NULL != dx);
	assert (NULL != buffer);
	assert (NULL != dx->callback);

	success = demux_packet (dx, &buffer, &buffer_size);

	return success;
}

/**
 * @brief Resets DVB VBI demux.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 *
 * Resets the DVB demux to the initial state as after vbi_dvb_pes_demux_new(),
 * useful for example after a channel change.
 *
 * @since 0.2.10
 */
void
vbi_dvb_demux_reset		(vbi_dvb_demux *	dx)
{
	assert (NULL != dx);

	CLEAR (dx->wrap);

	dx->wrap.buffer = dx->buffer;
	dx->wrap.bp = dx->buffer;

	dx->wrap.lookahead = HEADER_LOOKAHEAD;

	CLEAR (dx->frame);

	dx->frame.sliced_begin = dx->sliced;
	dx->frame.sliced_end = dx->sliced + N_ELEMENTS (dx->sliced);

	/* Raw data ignored for now. */

	dx->frame_pts = 0;
	dx->packet_pts = 0;

	dx->new_frame = TRUE;
}

/**
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 * @param mask Which kind of information to log. Can be @c 0.
 * @param log_fn This function is called with log messages. Consider
 *   vbi_log_on_stderr(). Can be @c NULL to disable logging.
 * @param user_data User pointer passed through to the @a log_fn function.
 *
 * The DVB demultiplexer supports the logging of errors in the PES stream and
 * information useful to debug the demultiplexer.
 *
 * With this function you can redirect log messages generated by this module
 * from the global log function ( see vbi_set_log_fn() ) to a different function,
 * or enable logging only in the DVB demultiplexer @a dx.
 *
 * @note
 * The kind and contents of log messages may change in the future.
 *
 * @since 0.2.22
 */
void
vbi_dvb_demux_set_log_fn	(vbi_dvb_demux *	dx,
				 vbi_log_mask		mask,
				 vbi_log_fn *		log_fn,
				 void *			user_data)
{
	assert (NULL != dx);

	if (NULL == log_fn)
		mask = 0;

	dx->log.mask = mask;
	dx->log.fn = log_fn;
	dx->log.user_data = user_data;
}

/**
 * @brief Deletes DVB VBI demux.
 * @param dx DVB demultiplexer context allocated with
 *   vbi_dvb_pes_demux_new(), can be @c NULL.
 *
 * Frees all resources associated with @a dx.
 *
 * @since 0.2.10
 */
void
vbi_dvb_demux_delete		(vbi_dvb_demux *	dx)
{
	if (NULL == dx)
		return;

	CLEAR (*dx);

	free (dx);		
}

/**
 * @brief Allocates DVB VBI demux.
 * @param callback Function to be called by vbi_dvb_demux_demux() when
 *   a new frame is available.
 * @param user_data User pointer passed through to @a callback function.
 *
 * Allocates a new DVB VBI (EN 301 472, EN 301 775) demultiplexer taking
 * a PES stream as input.
 *
 * @returns
 * Pointer to newly allocated DVB demux context which must be
 * freed with vbi_dvb_demux_delete() when done. @c NULL on failure
 * (out of memory).
 *
 * @since 0.2.10
 */
vbi_dvb_demux *
vbi_dvb_pes_demux_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data)
{
	vbi_dvb_demux *dx;

	if (!(dx = malloc (sizeof (*dx)))) {
		return NULL;
	}

	CLEAR (*dx);

	vbi_dvb_demux_reset (dx);

	dx->callback = callback;
	dx->user_data = user_data;

	return dx;
}

/* For compatibility with Zapping 0.8 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

extern unsigned int
_vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		sliced_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left);
extern void
_vbi_dvb_demux_delete		(vbi_dvb_demux *	dx);
extern vbi_dvb_demux *
_vbi_dvb_demux_pes_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data);

unsigned int
_vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		sliced_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left)
{
	return vbi_dvb_demux_cor (dx, sliced, sliced_lines,
				  pts, buffer, buffer_left);
}

void
_vbi_dvb_demux_delete		(vbi_dvb_demux *	dx)
{
	vbi_dvb_demux_delete (dx);
}

vbi_dvb_demux *
_vbi_dvb_demux_pes_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data)
{
	return vbi_dvb_pes_demux_new (callback, user_data);
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
