/*
 *  libzvbi - VBI decoding library
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
 *  Copyright (C) 2000, 2001 I�aki Garc�a Etxebarria
 *
 *  Based on AleVT 1.5.1
 *  Copyright (C) 1998, 1999 Edgar Toernig <froese@gmx.de>
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

/* $Id: vbi.c,v 1.6.2.9 2004-03-31 00:41:35 mschimek Exp $ */

#include "../site_def.h"
#include "../config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <pthread.h>

#include "vbi.h"
#include "hamm.h"
#include "lang.h"
#include "export.h"
#include "tables.h"
#include "format.h"
#include "wss.h"
#include "version.h"
#include "vps.h"

/**
 * @mainpage ZVBI - VBI Decoding Library
 *
 * @author I�aki Garc�a Etxebarria<br>
 * Michael H. Schimek<br>
 * based on AleVT by Edgar Toernig
 *
 * @section intro Introduction
 *
 * The ZVBI library provides routines to access raw VBI sampling devices
 * (currently the Linux <a href="http://roadrunner.swansea.uk.linux.org/v4l.shtml">V4L</a>
 * and <a href="http://www.thedirks.org/v4l2/">V4L2</a> API and the
 * FreeBSD, OpenBSD, NetBSD and BSDi
 * <a href="http://telepresence.dmem.strath.ac.uk/bt848/">bktr driver</a> API
 * are supported), a versatile raw VBI bit slicer,
 * decoders for various data services and basic search,
 * render and export functions for text pages. The library was written for
 * the <a href="http://zapping.sourceforge.net">Zapping TV viewer and
 * Zapzilla Teletext browser</a>.
 */

/** @defgroup Raw Raw VBI */
/** @defgroup Service Data Service Decoder */
/** @defgroup LowDec Low Level Decoding */

//pthread_once_t vbi_init_once = PTHREAD_ONCE_INIT;

void
vbi_init			(void)
{
#ifdef ENABLE_NLS
	bindtextdomain (PACKAGE, PACKAGE_LOCALE_DIR);
#endif
}

/*
 *  Events
 */

/* Should this be public? */
static void
vbi_event_enable(vbi_decoder *vbi, int mask) 
{
	int activate;

	activate = mask & ~vbi->event_mask;

	if (activate & VBI_EVENT_TTX_PAGE)
		vbi_teletext_channel_switched(vbi);
	if (activate & VBI_EVENT_CAPTION)
		vbi_caption_channel_switched(vbi);
	if (activate & VBI_EVENT_NETWORK)
		memset(&vbi->network, 0, sizeof(vbi->network));
	if (activate & VBI_EVENT_TRIGGER)
		vbi_trigger_flush(vbi);
	if (activate & (VBI_EVENT_ASPECT | VBI_EVENT_PROG_INFO)) {
		if (!(vbi->event_mask & (VBI_EVENT_ASPECT | VBI_EVENT_PROG_INFO))) {
			vbi_reset_prog_info(&vbi->prog_info[0]);
			vbi_reset_prog_info(&vbi->prog_info[1]);

			vbi->prog_info[1].future = TRUE;
			vbi->prog_info[0].future = FALSE;

			vbi->aspect_source = 0;
		}
	}

	vbi->event_mask = mask;
}

/**
 * @param vbi Initialized vbi decoding context.
 * @param event_mask Events the handler is waiting for.
 * @param handler Event handler function.
 * @param user_data Pointer passed to the handler.
 * 
 * @deprecated Use vbi_event_handler_register() in new code.
 * 
 * @return
 * FALSE on failure.
 */
vbi_bool
vbi_event_handler_add(vbi_decoder *vbi, int event_mask,
		      vbi_event_handler handler, void *user_data) 
{
	struct event_handler *eh, **ehp;
	int found = 0, mask = 0, was_locked;

	/* If was_locked we're a handler, no recursion. */
	was_locked = pthread_mutex_trylock(&vbi->event_mutex);

	ehp = &vbi->handlers;

	while ((eh = *ehp)) {
		if (eh->handler == handler) {
			found = 1;

			if (!event_mask) {
				*ehp = eh->next;

				if (vbi->next_handler == eh)
					vbi->next_handler = eh->next;
						/* in event send loop */
				free(eh);

				continue;
			} else
				eh->event_mask = event_mask;
		}

		mask |= eh->event_mask;	
		ehp = &eh->next;
	}

	if (!found && event_mask) {
		if (!(eh = (struct event_handler *) calloc(1, sizeof(*eh))))
			return FALSE;

		eh->event_mask = event_mask;
		mask |= event_mask;

		eh->handler = handler;
		eh->user_data = user_data;

		*ehp = eh;
	}

	vbi_event_enable(vbi, mask);

	if (!was_locked)
		pthread_mutex_unlock(&vbi->event_mutex);

	return TRUE;
}

/**
 * @param vbi Initialized vbi decoding context.
 * @param handler Event handler function.
 * 
 * @deprecated Use vbi_event_handler_register() in new code.
 */
void
vbi_event_handler_remove(vbi_decoder *vbi, vbi_event_handler handler)
{
	vbi_event_handler_add(vbi, 0, handler, NULL);
} 

/**
 * @param vbi Initialized vbi decoding context.
 * @param event_mask Events the handler is waiting for.
 * @param handler Event handler function.
 * @param user_data Pointer passed to the handler.
 * 
 * Registers a new event handler. @a event_mask can be any 'or' of VBI_EVENT_
 * symbols, -1 for all events and 0 for none. When the @a handler with
 * @a user_data is already registered, its event_mask will be changed. Any
 * number of handlers can be registered, also different handlers for the same
 * event which will be called in registration order.
 * 
 * Apart of adding handlers this function also enables and disables decoding
 * of data services depending on the presence of at least one handler for the
 * respective data. A @c VBI_EVENT_TTX_PAGE handler for example enables Teletext
 * decoding.
 * 
 * This function can be safely called at any time, even from a handler.
 * 
 * @return
 * @c FALSE on failure.
 */
vbi_bool
vbi_event_handler_register(vbi_decoder *vbi, int event_mask,
		           vbi_event_handler handler, void *user_data) 
{
	struct event_handler *eh, **ehp;
	int found = 0, mask = 0, was_locked;

	/* If was_locked we're a handler, no recursion. */
	was_locked = pthread_mutex_trylock(&vbi->event_mutex);

	ehp = &vbi->handlers;

	while ((eh = *ehp)) {
		if (eh->handler == handler
		    && eh->user_data == user_data) {
			found = 1;

			if (!event_mask) {
				*ehp = eh->next;

				if (vbi->next_handler == eh)
					vbi->next_handler = eh->next;
						/* in event send loop */
				free(eh);

				continue;
			} else
				eh->event_mask = event_mask;
		}

		mask |= eh->event_mask;	
		ehp = &eh->next;
	}

	if (!found && event_mask) {
		if (!(eh = (struct event_handler *) calloc(1, sizeof(*eh))))
			return FALSE;

		eh->event_mask = event_mask;
		mask |= event_mask;

		eh->handler = handler;
		eh->user_data = user_data;

		*ehp = eh;
	}

	vbi_event_enable(vbi, mask);

	if (!was_locked)
		pthread_mutex_unlock(&vbi->event_mutex);

	return TRUE;
}

/**
 * @param vbi Initialized vbi decoding context.
 * @param handler Event handler function.
 * @param user_data Pointer passed to the handler.
 * 
 * Unregisters an event handler.
 *
 * Apart of removing a handler this function also disables decoding
 * of data services when no handler is registered to consume the
 * respective data. Removing the last @c VBI_EVENT_TTX_PAGE handler for
 * example disables Teletext decoding.
 * 
 * This function can be safely called at any time, even from a handler
 * removing itself or another handler, and regardless if the @a handler
 * has been successfully registered.
 **/
void
vbi_event_handler_unregister(vbi_decoder *vbi,
			     vbi_event_handler handler, void *user_data)
{
	vbi_event_handler_register(vbi, 0, handler, user_data);
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * @param ev The event to send.
 * 
 * Traverses the list of event handlers and calls each handler waiting
* * for this @a ev->type of event, passing @a ev as parameter.
 * 
 * This function is reentrant, but not supposed to be called from
 * different threads to ensure correct event order.
 */
void
vbi_send_event(vbi_decoder *vbi, vbi_event *ev)
{
	struct event_handler *eh;

	pthread_mutex_lock(&vbi->event_mutex);

	for (eh = vbi->handlers; eh; eh = vbi->next_handler) {
		vbi->next_handler = eh->next;

		if (eh->event_mask & ev->type)
			eh->handler(ev, eh->user_data);
	}

	pthread_mutex_unlock(&vbi->event_mutex);
}

/*
 *  VBI Decoder
 */

vbi_inline double
current_time(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_sec + tv.tv_usec * (1 / 1e6);
}

/**
 * @param vbi Initialized vbi decoding context as returned by vbi_decoder_new().
 * @param sliced Array of vbi_sliced data packets to be decoded.
 * @param lines Number of vbi_sliced data packets, i. e. VBI lines.
 * @param time Timestamp associated with <em>all</em> sliced data packets.
 *   This is the time in seconds and fractions since 1970-01-01 00:00,
 *   for example from function gettimeofday(). @a time should only
 *   increment, the latest time entered is considered the current time
 *   for activity calculation.
 * 
 * @brief Main function of the data service decoder.
 *
 * Decodes zero or more lines of sliced VBI data from the same video
 * frame, updates the decoder state and calls event handlers.
 * 
 * @a timestamp shall advance by 1/30 to 1/25 seconds whenever calling this
 * function. Failure to do so will be interpreted as frame dropping, which
 * starts a resynchronization cycle, eventually a channel switch may be assumed
 * which resets even more decoder state. So even if a frame did not contain
 * any useful data this function must be called, with @a lines set to zero.
 * 
 * @note This is one of the few not reentrant libzvbi functions. If multiple
 * threads call this with the same @a vbi context you must implement your
 * own locking mechanism. Never call this function from an event handler.
 */
void
vbi_decode(vbi_decoder *vbi, vbi_sliced *sliced, int lines, double time)
{
	double d;

	d = time - vbi->time;

	if (vbi->time > 0 && (d < 0.025 || d > 0.050)) {
	  /*
	   *  Since (dropped >= channel switch) we give
	   *  ~1.5 s, then assume a switch.
	   */
	  pthread_mutex_lock(&vbi->chswcd_mutex);

	  if (vbi->chswcd == 0)
		  vbi->chswcd = 40;

	  pthread_mutex_unlock(&vbi->chswcd_mutex);

	  if (0)
		  fprintf(stderr, "vbi frame/s dropped at %f, D=%f\n",
			  time, time - vbi->time);

	  if (vbi->event_mask &
	      (VBI_EVENT_TTX_PAGE | VBI_EVENT_NETWORK))
		  vbi_teletext_desync(vbi);
	  if (vbi->event_mask &
	      (VBI_EVENT_CAPTION | VBI_EVENT_NETWORK))
		  vbi_caption_desync(vbi);
	} else {
		pthread_mutex_lock(&vbi->chswcd_mutex);

		if (vbi->chswcd > 0 && --vbi->chswcd == 0) {
			pthread_mutex_unlock(&vbi->chswcd_mutex);
			vbi_chsw_reset(vbi, 0);
		} else
			pthread_mutex_unlock(&vbi->chswcd_mutex);
	}

	if (time > vbi->time)
		vbi->time = time;

	while (lines) {
		if (sliced->id & VBI_SLICED_TELETEXT_B)
			vbi_decode_teletext(vbi, sliced->data);
		else if (sliced->id & (VBI_SLICED_CAPTION_525 | VBI_SLICED_CAPTION_625))
			vbi_decode_caption(vbi, sliced->line, sliced->data);
		else if (sliced->id & VBI_SLICED_VPS)
			vbi_decode_vps(vbi, sliced->data);
		else if (sliced->id & VBI_SLICED_WSS_625)
			vbi_decode_wss_625(vbi, sliced->data, time);
		else if (sliced->id & VBI_SLICED_WSS_CPR1204)
			vbi_decode_wss_cpr1204(vbi, sliced->data);

		sliced++;
		lines--;
	}

	if (vbi->event_mask & VBI_EVENT_TRIGGER)
		vbi_deferred_trigger(vbi);

	if (0 && (rand() % 511) == 0)
		vbi_eacem_trigger(vbi, (unsigned char *) /* Latin-1 */
				  "<http://zapping.sourceforge.net>[n:Zapping][5450]");
}

void
vbi_chsw_reset(vbi_decoder *vbi, vbi_nuid identified)
{
	vbi_nuid old_nuid = vbi->network.ev.network.nuid;

	if (0)
		fprintf(stderr, "*** chsw identified=%d old nuid=%d\n",
			identified, old_nuid);

	vbi_cache_flush (vbi, TRUE);

	vbi_teletext_channel_switched(vbi);
	vbi_caption_channel_switched(vbi);

	if (identified == 0) {
		memset(&vbi->network, 0, sizeof(vbi->network));

		if (old_nuid != 0) {
			vbi->network.type = VBI_EVENT_NETWORK;
			vbi_send_event(vbi, &vbi->network);
		}
	} /* else already identified */

	vbi_trigger_flush(vbi); /* sic? */

	if (vbi->aspect_source > 0) {
		vbi_event e;

		e.ev.aspect.first_line = (vbi->aspect_source == 1) ? 23 : 22;
		e.ev.aspect.last_line =	(vbi->aspect_source == 1) ? 310 : 262;
		e.ev.aspect.ratio = 1.0;
		e.ev.aspect.film_mode = 0;
		e.ev.aspect.open_subtitles = VBI_SUBT_UNKNOWN;

		e.type = VBI_EVENT_ASPECT;
		vbi_send_event(vbi, &e);
	}

	vbi_reset_prog_info(&vbi->prog_info[0]);
	vbi_reset_prog_info(&vbi->prog_info[1]);
	/* XXX event? */

	vbi->prog_info[1].future = TRUE;
	vbi->prog_info[0].future = FALSE;

	vbi->aspect_source = 0;

	vbi->wss_last[0] = 0;
	vbi->wss_last[1] = 0;
	vbi->wss_rep_ct = 0;
	vbi->wss_time = 0.0;

	vbi->vt.header_page.pgno = 0;

	pthread_mutex_lock(&vbi->chswcd_mutex);

	vbi->chswcd = 0;

	pthread_mutex_unlock(&vbi->chswcd_mutex);
}

/**
 * @param vbi VBI decoding context.
 * @param nuid Set to zero for now.
 * 
 * Call this after switching away from the channel (RF channel,
 * video input line, precisely: the network) from which this context
 * used to receive vbi data, to reset the decoding context accordingly.
 * This includes deletion of all cached Teletext and Closed Caption pages.
 *
 * The decoder attempts to detect channel switches automatically, but this
 * is not 100 % reliable, especially without receiving and decoding Teletext
 * or VPS which frequently transmit network identifiers.
 *
 * Note the reset is not executed until the next frame is about to be
 * decoded, so you may still receive "old" events after calling this. You
 * may also receive blank events (e. g. unknown network, unknown aspect
 * ratio) revoking a previously sent event, until new information becomes
 * available.
 */
void
vbi_channel_switched(vbi_decoder *vbi, vbi_nuid nuid)
{
	/* XXX nuid */

	pthread_mutex_lock(&vbi->chswcd_mutex);

	vbi->chswcd = 1;

	pthread_mutex_unlock(&vbi->chswcd_mutex);
}

/*
 *  Cache queries
 */

/**
 * @param vbi Initialized vbi decoding context.
 * @param pgno Teletext or Closed Caption page to examine, see vbi_pgno.
 *
 * This function returns the language (or languages) a page is written
 * in. Languages are identified by their ISO 639 two-character code,
 * for example French by the string "fr".
 *
 * The function returns a NULL-terminated string vector. The vector can
 * be empty if the language is not known to the decoder, for example
 * when the page has not been received yet. It can contain more than
 * one language code if the page is multilingual or the available
 * information is ambiguous. In fact the language code or codes may
 * be misleading.
 *
 * @note The results of this function are volatile: As more information
 * becomes available and pages are edited (e. g. activation of subtitles)
 * languages can change.
 *
 * @return
 * Pointer to a string vector. The function returns a @c NULL pointer
 * when @a pgno is invalid.
 */
const char **
vbi_cache_page_language		(vbi_decoder *		vbi,
				 vbi_pgno		pgno)
{
	static const char *nil = NULL;

	if (pgno <= 8 && pgno >= 1) {
		return vbi->cc.channel[pgno - 1].lang_code;
	} else if (pgno >= 0x100 && pgno <= 0x8FF) {
		struct page_info *pi;
		int code;

		pi = vbi->vt.page_info + pgno - 0x100;
		code = pi->code;
		if (code != VBI_UNKNOWN_PAGE) {
/* FIXME normal pages */		
			if (code == VBI_SUBTITLE_PAGE) {
				const vbi_character_set *cs;

				if (pi->language != 0xFF
				    && (cs = vbi_character_set_from_code (pi->language)))
					return cs->language_code;
			}
		}
	}

	return &nil;
}

/**
 * @param vbi Initialized vbi decoding context.
 * @param pgno Teletext or Closed Caption page to examine, see vbi_pgno.
 * @param subno The highest subpage number of this page will be
 *   stored here. @a subno can be @c NULL.
 * 
 * Returns information about the page.
 * 
 * For Closed Caption pages (@a pgno 1 ... 8) @a subno will always
 * be zero, @a language set or @c NULL. The return value will be
 * @c VBI_SUBTITLE_PAGE for page 1 ... 4 (Closed Caption
 * channel 1 ... 4), @c VBI_NORMAL_PAGE for page 5 ... 8 (Text channel
 * 1 ... 4), or @c VBI_NO_PAGE if no data is currently transmitted on
 * the channel.
 *
 * For Teletext pages (@a pgno 0x100 ... 0x8FF) @a subno returns
 * the highest subpage number used. Note this number can be larger
 * (but not smaller) than the number of subpages actually received
 * and cached. Still there is no guarantee the advertised subpages
 * will ever appear or stay in cache.
 *
 * <table>
 * <tr><td><b>subno</b></td><td><b>meaning</b></td></tr>
 * <tr><td>0</td><td>single page, no subpages</td></tr>
 * <tr><td>1</td><td>never</td></tr>
 * <tr><td>2 ... 0x3F7F</td><td>has subpages 1 ... @a subno </td></tr>
 * <tr><td>0xFFFE</td><td>has unknown number (two or more) of subpages</td></tr>
 * <tr><td>0xFFFF</td><td>presence of subpages unknown</td></tr>
 * </table>
 *
 * @a language currently returns the language of subtitle pages, @c NULL
 * if unknown or the page is not classified as @c VBI_SUBTITLE_PAGE.
 *
 * Other page types are:
 *
 * <table>
 * <tr><td>VBI_NO_PAGE</td><td>Page is not in transmission</td></tr>
 * <tr><td>VBI_NORMAL_PAGE</td><td>&nbsp;</td></tr>
 * <tr><td>VBI_SUBTITLE_PAGE</td><td>&nbsp;</td></tr>
 * <tr><td>VBI_SUBTITLE_INDEX</td><td>List of subtitle pages</td></tr>
 * <tr><td>VBI_NONSTD_SUBPAGES</td><td>For example a world time page</td></tr>
 * <tr><td>VBI_PROGR_WARNING</td><td>Program related warning (perhaps
 * schedule change anouncements, the Teletext specification does not
 * elaborate on this)</td></tr>
 * <tr><td>VBI_CURRENT_PROGR</td><td>Information about the
 * current program</td></tr>
 * <tr><td>VBI_NOW_AND_NEXT</td><td>Brief information about the
 * current and next program</td></tr>
 * <tr><td>VBI_PROGR_INDEX</td><td>Program index page (perhaps the front
 * page of all program related pages)</td></tr>
 * <tr><td>VBI_PROGR_SCHEDULE</td><td>Program schedule page</td></tr>
 * <tr><td>VBI_UNKNOWN_PAGE</td><td>&nbsp;</td></tr>
 * </table>
 *
 * @note The results of this function are volatile: As more information
 * becomes available and pages are edited (e. g. activation of subtitles,
 * news updates, program related pages) subpage numbers can grow, page
 * types, subno 0xFFFE and 0xFFFF and languages can change.
 *
 * @return
 * Page type.
 */
vbi_page_type
vbi_classify_page(vbi_decoder *vbi, vbi_pgno pgno, vbi_subno *subno)
{
	struct page_info *pi;
	int code, subc;
//	char *lang;

	if (!subno)
		subno = &subc;
//	if (!language)
//		language = &lang;

	*subno = 0;
	//	*language = NULL;

	if (pgno < 1) {
		return VBI_UNKNOWN_PAGE;
	} else if (pgno <= 8) {
		if ((current_time() - vbi->cc.channel[pgno - 1].time) > 20)
			return VBI_NO_PAGE;

		//		*language = vbi->cc.channel[pgno - 1].language;

		return (pgno <= 4) ? VBI_SUBTITLE_PAGE : VBI_NORMAL_PAGE;
	} else if (pgno < 0x100 || pgno > 0x8FF) {
		return VBI_UNKNOWN_PAGE;
	}

	pi = vbi->vt.page_info + pgno - 0x100;
	code = pi->code;

	if (code != VBI_UNKNOWN_PAGE) {
		if (code == VBI_SUBTITLE_PAGE) {
			if (pi->language != 0xFF)
				;
	//		*language = vbi_font_descriptors[pi->language].label;
		} else if (code == VBI_TOP_BLOCK || code == VBI_TOP_GROUP)
			code = VBI_NORMAL_PAGE;
		else if (code == VBI_NOT_PUBLIC || code > 0xE0)
			return VBI_UNKNOWN_PAGE;

		*subno = pi->subcode;

		return code;
	}

	if ((pgno & 0xFF) <= 0x99) {
		*subno = 0xFFFF;
		return VBI_NORMAL_PAGE; /* wild guess */
	}

	return VBI_UNKNOWN_PAGE;
}








/**
 * @param pi 
 * 
 * Convenience function to set a vbi_program_info
 * structure to defaults.
 */
void
vbi_reset_prog_info(vbi_program_info *pi)
{
	int i;

	/* PID */
	pi->month = -1;
	pi->day = -1;
	pi->hour = -1;
	pi->min = -1;
	pi->tape_delayed = 0;
	/* PL */
	pi->length_hour = -1;
	pi->length_min = -1;
	pi->elapsed_hour = -1;
	pi->elapsed_min = -1;
	pi->elapsed_sec = -1;
	/* PN */
	pi->title[0] = 0;
	/* PT */
	pi->type_classf = VBI_PROG_CLASSF_NONE;
	/* PR */
	pi->rating_auth = VBI_RATING_AUTH_NONE;
	/* PAS */
	pi->audio[0].mode = VBI_AUDIO_MODE_UNKNOWN;
	pi->audio[0].lang_code = NULL;
	pi->audio[1].mode = VBI_AUDIO_MODE_UNKNOWN;
	pi->audio[1].lang_code = NULL;
	/* PCS */
	pi->caption_services = -1;
	for (i = 0; i < 8; i++)
		pi->caption_lang_code[i] = NULL;
	/* CGMS */
	pi->cgms_a = -1;
	/* AR */
	pi->aspect.first_line = -1;
	pi->aspect.last_line = -1;
	pi->aspect.ratio = 0.0;
	pi->aspect.film_mode = 0;
	pi->aspect.open_subtitles = VBI_SUBT_UNKNOWN;
	/* PD */
	for (i = 0; i < 8; i++)
		pi->description[i][0] = 0;
}

/**
 * @param vbi Decoder structure allocated with vbi_decoder_new().
 * @brief Delete a data service decoder instance.
 */
void
vbi_decoder_delete(vbi_decoder *vbi)
{
	vbi_trigger_flush(vbi);

	vbi_caption_destroy(vbi);

	pthread_mutex_destroy(&vbi->prog_info_mutex);
	pthread_mutex_destroy(&vbi->event_mutex);
	pthread_mutex_destroy(&vbi->chswcd_mutex);

	vbi_cache_destroy(vbi);

	free(vbi);
}

/**
 * @brief Allocate a new data service decoder instance.
 * 
 * @return
 * vbi_decoder pointer or @c NULL on failure, probably due to lack
 * of memory.
 */
vbi_decoder *
vbi_decoder_new(void)
{
	vbi_decoder *vbi;

#warning
	fprintf (stderr, "LIBZVBI NG\n");

	//	pthread_once (&vbi_init_once, vbi_init);

	if (!(vbi = (vbi_decoder *) calloc(1, sizeof(*vbi))))
		return NULL;

	vbi_cache_init(vbi);

	pthread_mutex_init(&vbi->chswcd_mutex, NULL);
	pthread_mutex_init(&vbi->event_mutex, NULL);
	pthread_mutex_init(&vbi->prog_info_mutex, NULL);

	vbi->time = 0.0;

	vbi_teletext_init(vbi);

	vbi_teletext_set_level(vbi, VBI_WST_LEVEL_2p5);

	vbi_caption_init(vbi);

	return vbi;
}

/**
 * @param major Store major number here, can be NULL.
 * @param minor Store minor number here, can be NULL.
 * @param micro Store micro number here, can be NULL.
 *
 * Returns the library version defined in the libzvbi.h header file
 * when the library was compiled. This function is available since
 * version 0.2.5.
 */
void
vbi_version			(unsigned int *		major,
				 unsigned int *		minor,
				 unsigned int *		micro)
{
	if (major) *major = VBI_VERSION_MAJOR;
	if (minor) *minor = VBI_VERSION_MINOR;
	if (micro) *micro = VBI_VERSION_MICRO;
}

/**
 * @internal
 */
size_t
vbi_strlcpy			(char *			dst1,
				 const char *		src,
				 size_t			size)
{
	char c, *dst, *end;

	assert (size > 0);

	dst = dst1;
	end = dst1 + size - 1;

	while (dst < end && (c = *src++))
		*dst++ = c;

	*dst = 0;

	return dst - dst1;
}

/**
 * @internal
 * libzvbi internal helper function.
 * Note asprintf() is a GNU libc extension.
 */
void
vbi_asprintf(char **errstr, char *templ, ...)
{
	char buf[512];
	va_list ap;
	int temp;

	if (!errstr)
		return;

	temp = errno;

	va_start(ap, templ);

	vsnprintf(buf, sizeof(buf) - 1, templ, ap);

	va_end(ap);

	*errstr = strdup(buf);

	errno = temp;
}

static vbi_log_fn *		log_function;
static void *			log_user_data;

void
vbi_set_log_fn			(vbi_log_fn *		function,
				 void *			user_data)
{
	log_function = function;
	log_user_data = user_data;
}

void
vbi_log_printf			(const char *		function,
				 const char *		template,
				 ...)
{
	char buf[512];
	va_list ap;
	int temp;

	if (NULL == log_function)
		return;

	temp = errno;

	va_start (ap, template);

	vsnprintf (buf, sizeof (buf) - 1, template, ap);

	va_end (ap);

	log_function (function, buf, log_user_data);

	errno = temp;
}

void
vbi_page_private_dump		(const vbi_page_private *pgp,
				 unsigned int		mode,
				 FILE *			fp)
{
	unsigned int row;
	unsigned int column;
	vbi_char *acp;

	acp = pgp->pg.text;

	for (row = 0; row < pgp->pg.rows; ++row) {
		fprintf (fp, "%2u: ", row);

		for (column = 0; column < pgp->pg.columns; ++column) {
			unsigned int c;

			switch (mode) {
			case 0:
				c = acp->unicode;
				if (c < 0x20 || c >= 0x7F)
					c = '.';
				fputc (c, fp);
				break;

			case 1:
				fprintf (fp, "%04x ", acp->unicode);
				break;

			case 2:
				fprintf (fp, "%04xF%uB%uS%uO%uL%u%u ",
					 acp->unicode,
					 acp->foreground, acp->background,
					 acp->size, acp->opacity,
					 !!(acp->attr & VBI_LINK),
					 !!(acp->attr & VBI_PDC));
				break;
			}

			++acp;
		}

		fputc ('\n', fp);
	}
}
