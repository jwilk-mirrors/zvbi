/*
 *  libzvbi - Tables
 *
 *  Copyright (C) 1999, 2000, 2001 Michael H. Schimek
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

/* $Id: tables.c,v 1.4.2.5 2007-11-01 00:21:25 mschimek Exp $ */

#include <stdlib.h>

#include "tables.h"

#if 1

/*
 *  ETS 300 231 Table 3: Codes for programme type (PTY) Principle of classification
 */
const char *
ets_program_class[16] =
{
	"undefined content",
	"drama & films",
	"news/current affairs/social",
	"show/game show/leisure hobbies",
	"sports",
	"children/youth/education/science",
	"music/ballet/Dance",
	"arts/culture (without music)",
	"series code",
	"series code",
	"series code",
	"series code",
	"series code",
	"series code",
	"series code",
	"series code",
};

#endif

/*
 *  ETS 300 231 Table 3: Codes for programme type (PTY) Principle of classification
 */
const char *
ets_program_type[8][16] =
{
	{
		0
	}, {
		"movie (general)",
		"detective/thriller",
		"adventure/western/war",
		"science fiction/fantasy/horror",
		"comedy",
		"soap/melodrama/folklore",
		"romance",
		"serious/classical/religious/historical drama",
		"adult movie"
	}, {
		"news/current affairs (general)",
		"news/weather report",
		"news magazine",
		"documentary",
		"discussion/interview/debate",
		"social/political issues/economics (general)",
		"magazines/reports/documentary",
		"economics/social advisory",
		"remarkable people"
	}, {
		"show/game show (general)",
		"game show/quiz/contest",
		"variety show",
		"talk show",
		"leisure hobbies (general)",
		"tourism/travel",
		"handicraft",
		"motoring",
		"fitness & health",
		"cooking",
		"advertisement/shopping",
		0,
		0,
		0,
		0,
		"alarm/emergency identification"
	}, {
		"sports (general)"
		"special event (Olympic Games, World Cup etc.)",
		"sports magazine",
		"football/soccer",
		"tennish/squash",
		"team sports (excluding football)",
		"athletics",
		"motor sport",
		"water sport",
		"winter sports",
		"equestrian",
		"martial sports",
		"local sports"
	}, {
		"children's/youth programmes (general)",
		"pre-school children's programmes",
		"entertainment programmes for 6 to 14",
		"entertainment programmes for 10 to 16",
		"informational/educational/school programmes",
		"cartoons/puppets",
		"education/science/factual topics (general)",
		"nature/animals/environement",
		"technology/natural sciences",
		"medicine/physiology/psychology",
		"foreign countries/expeditions",
		"social/spiritual sciences",
		"further education",
		"languages"
	}, {
		"music/ballet/dance (general)",
		"rock/Pop",
		"serious music/classical Music",
		"folk/traditional music",
		"jazz",
		"musical/opera",
		"ballet"
	}, {
		"arts/culture (general)",
		"performing arts",
		"fine arts",
		"religion",
		"popular culture/traditional arts",
		"literature",
		"film/cinema",
		"experimental film/video",
		"broadcasting/press",
		"new media",
		"arts/culture magazines",
		"fashion"
	}
};

static const char *
eia608_program_type[96] =
{
	"education",
	"entertainment",
	"movie",
	"news",
	"religious",
	"sports",
	"other",
	"action",
	"advertisement",
	"animated",
	"anthology",
	"automobile",
	"awards",
	"baseball",
	"basketball",
	"bulletin",
	"business",
	"classical",
	"college",
	"combat",
	"comedy",
	"commentary",
	"concert",
	"consumer",
	"contemporary",
	"crime",
	"dance",
	"documentary",
	"drama",
	"elementary",
	"erotica",
	"exercise",
	"fantasy",
	"farm",
	"fashion",
	"fiction",
	"food",
	"football",
	"foreign",
	"fund raiser",
	"game/quiz",
	"garden",
	"golf",
	"government",
	"health",
	"high school",
	"history",
	"hobby",
	"hockey",
	"home",
	"horror",
	"information",
	"instruction",
	"international",
	"interview",
	"language",
	"legal",
	"live",
	"local",
	"math",
	"medical",
	"meeting",
	"military",
	"miniseries",
	"music",
	"mystery",
	"national",
	"nature",
	"police",
	"politics",
	"premiere",
	"prerecorded",
	"product",
	"professional",
	"public",
	"racing",
	"reading",
	"repair",
	"repeat",
	"review",
	"romance",
	"science",
	"series",
	"service",
	"shopping",
	"soap opera",
	"special",
	"suspense",
	"talk",
	"technical",
	"tennis",
	"travel",
	"variety",
	"video",
	"weather",
	"western"
};

/**
 * @param auth From vbi3_program_info.rating_auth.
 * @param id From vbi3_program_info.rating_id.
 * 
 * Translate a vbi3_program_info program rating code into a
 * Latin-1 string, native language.
 * 
 * @a return
 * Static pointer to the string (don't free()), or @c NULL if
 * this code is undefined.
 */
const char *
vbi3_rating_string(vbi3_rating_auth auth, int id)
{
	static const char *ratings[4][8] = {
		{ NULL, "G", "PG", "PG-13", "R", "NC-17", "X", "Not rated" },
		{ "Not rated", "TV-Y", "TV-Y7", "TV-G", "TV-PG", "TV-14", "TV-MA", "Not rated" },
		{ "Exempt", "C", "C8+", "G", "PG", "14+", "18+", NULL },
		{ "Exempt", "G", "8 ans +", "13 ans +", "16 ans +", "18 ans +", NULL, NULL },
	};

	if (id < 0 || id > 7)
		return NULL;

	switch (auth) {
	case VBI3_RATING_AUTH_MPAA:
		return ratings[0][id];

	case VBI3_RATING_AUTH_TV_US:
		return ratings[1][id];

	case VBI3_RATING_AUTH_TV_CA_EN:
		return ratings[2][id];

	case VBI3_RATING_AUTH_TV_CA_FR:
		return ratings[3][id];

	default:
		return NULL;
	}
}

/**
 * @param classf From vbi3_program_info.type_classf.
 * @param id From vbi3_program_info.type_id.
 * 
 * Translate a vbi3_program_info program type code into a
 * Latin-1 string, currently English only.
 * 
 * @return 
 * Static pointer to the string (don't free()), or @c NULL if
 * this code is undefined.
 */
const char *
vbi3_prog_type_string(vbi3_prog_classf classf, int id)
{
	switch (classf) {
	case VBI3_PROG_CLASSF_EIA_608:
		if (id < 0x20 || id > 0x7F)
			return NULL;
		return eia608_program_type[id - 0x20];

	case VBI3_PROG_CLASSF_ETS_300231:
		if (id < 0x00 || id > 0x7F)
			return NULL;
		return ets_program_type[0][id];

	default:
		return NULL;
	}
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
