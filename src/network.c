/* -*- coding: utf-8 -*- */

/*
 *  libzvbi - Network identification
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

/* $Id: network.c,v 1.1.2.5 2004-04-17 05:52:24 mschimek Exp $ */

#include "../config.h"

#include <assert.h>
#include <stdlib.h>
#include "misc.h"
#include "bcd.h"
#include "conv.h"
#include "network.h"

/*
    CNI sources:

    Packet 8/30 f1	Byte 13			Byte 14
    Bit (tx order)	0 1 2 3	4 5 6 7		0 1 2 3 4 5 6 7
    CNI			--------------- 15:8	--------------- 7:0

    Packet 8/30 f2	Byte 15		Byte 16		Byte 21		Byte 22		Byte 23
    Bit (tx order)	0 1 2 3		0 1		2 3		0 1 2 3		0 1 2 3
    VPS			Byte 5		Byte 11		Byte 13			Byte 14
    Bit (tx order)	4 5 6 7		0 1		6 7		0 1 2 3		4 5 6 7
    Country		------- 15:12 / 7:4		------------------- 11:8 / 3:0
    Network				--- 7:6			  	5:0 -------------------

    Packet X/26		Address			Mode		Data
    Bit (tx order)	0 1 2 3 4 5 6 7 8 9	A B C D E F	G H I J K L M N
    Data Word A		P P - P ----- P 1 1 0:5 (0x3n)
    Mode				        0 0 0 1 0 P 0:5 ("Country & Programme Source")
    Data Word B							------------- P 0:6
 */

struct cni_entry {
	uint16_t		id;
	const char		country[4];
	const char *		name;	/* (UTF-8) */
	uint16_t		cni1;   /* Packet 8/30 format 1 */
	uint16_t		cni2;	/* Packet 8/30 format 2 */
	uint16_t		cni3;	/* Packet X/26 (PDC B) */
	uint16_t		cni4;	/* VPS */
};

/*
   This information comes from TR 101 231 EBU rev. 5 (2001-08) www.ebu.ch,
   Table A.1: Register of Country and Network Identification
              (CNI) codes for Teletext based systems
   Table B.1: VPS CNI Codes used in Germany, Switzerland and Austria
   Unified to create a unique station id where A.1 and B.1 overlap,
   plus personal observations.
 */
/*
   Maybe we can switch to a XML based table, something like this:
    <?xml version="1.0" encoding="UTF-8"?>
    <!DOCTYPE x [
      <!ELEMENT x (network+)>
      <!ELEMENT network (name,country?,vps?,packet_8301?,packet_8302?,pdc_a?,pdc_b?,icon?)>
      <!ELEMENT name (#PCDATA)
        -- network name: utf-8 -->
      <!ELEMENT country (#PCDATA)
        -- country: RFC 1766 / ISO 3166 alpha-2 -->
      <!ELEMENT nuid (#PCDATA)
        -- (0-9)+ -->
      <!ELEMENT vps (#PCDATA)
        -- 3 digit VPS CNI: ((0-9)|(a-f)|(A-F))+ -->
      <!ELEMENT packet_8301 (#PCDATA)
        -- 4 digit Teletext packet 8/30 format 1 CNI: ((0-9)|(a-f)|(A-F))+ -->
      <!ELEMENT packet_8302 (#PCDATA)
        -- 4 digit Teletext packet 8/30 format 1 CNI: ((0-9)|(a-f)|(A-F))+ -->
      <!ELEMENT pdc_a (#PCDATA)
        -- 5 digit: ((0-9)|(a-f)|(A-F))+ -->
      <!ELEMENT pdc_b (#PCDATA)
        -- 4 digit: 3((0-9)|(a-f)|(A-F))+ -->
    ]>
    <x>
     <network>
      <name>VRT TV1</name>
      <country>BE</country>
      <packet_8301>3201</packet_8301>
      <packet_8302>1601</packet_8302>
      <pdc_b>3603</pdc_b>
      teletext header ?
      fallback nuid ?
      call sign ?
      <icon format="xx"></icon> ?
     </network>
    </x>
*/

const struct cni_entry
cni_table [] = {
	{ 1,	"BE", "VRT TV1",			0x3201, 0x1601, 0x3603, 0x0000 },
	{ 2,	"BE", "Ka2",				0x3206, 0x1606, 0x3606, 0x0000 },
	{ 3,	"BE", "RTBF 1",				0x3203, 0x0000, 0x0000, 0x0000 },
	{ 4,	"BE", "RTBF 2",				0x3204, 0x0000, 0x0000, 0x0000 },
	{ 5,    "BE", "CANVAS",				0x3202, 0x1602, 0x3602, 0x0000 },
	{ 6,	"BE", "VT4",				0x0404, 0x1604, 0x3604, 0x0000 },
	{ 7,	"BE", "VTM",				0x3205, 0x1605, 0x3605, 0x0000 },

	{ 8,	"HR", "HRT",				0x0385, 0x0000, 0x0000, 0x0000 },

	{ 9,	"CZ", "CT 1",				0x4201, 0x32C1, 0x3C21, 0x0000 },
	{ 10,	"CZ", "CT 2",				0x4202, 0x32C2, 0x3C22, 0x0000 },
	{ 11,	"CZ", "CT1 Regional",			0x4231, 0x32F1, 0x3C25, 0x0000 },
	{ 12,	"CZ", "CT1 Brno",			0x4211, 0x32D1, 0x3B01, 0x0000 },
	{ 13,	"CZ", "CT1 Ostravia",			0x4221, 0x32E1, 0x3B02, 0x0000 },
	{ 14,	"CZ", "CT2 Regional",			0x4232, 0x32F2, 0x3B03, 0x0000 },
	{ 15,	"CZ", "CT2 Brno",			0x4212, 0x32D2, 0x3B04, 0x0000 },
	{ 16,	"CZ", "CT2 Ostravia",			0x4222, 0x32E2, 0x3B05, 0x0000 },
	{ 17,	"CZ", "NOVA TV",			0x4203, 0x32C3, 0x3C23, 0x0000 },

	{ 18,	"DK", "DR1",				0x7392, 0x2901, 0x3901, 0x0000 },
	{ 19,	"DK", "DR2",				0x49CF, 0x2903, 0x3903, 0x0000 },
	{ 20,	"DK", "TV2",				0x4502, 0x2902, 0x3902, 0x0000 },
	{ 21,	"DK", "TV2 Zulu",			0x4503, 0x2904, 0x3904, 0x0000 },

	{ 22,	"FI", "OWL3",				0x358F, 0x260F, 0x3614, 0x0000 },
	{ 23,	"FI", "YLE1",				0x3581, 0x2601, 0x3601, 0x0000 },
	{ 24,	"FI", "YLE2",				0x3582, 0x2602, 0x3607, 0x0000 },

	{ 25,	"FR", "AB1",				0x33C1, 0x2FC1, 0x3F41, 0x0000 },
	{ 26,	"FR", "Aqui TV",			0x3320, 0x2F20, 0x3F20, 0x0000 },
	{ 27,	"FR", "Arte / La Cinquième",		0x330A, 0x2F0A, 0x3F0A, 0x0000 },
	{ 28,	"FR", "Canal J",			0x33C2, 0x2FC2, 0x3F42, 0x0000 },
	{ 29,	"FR", "Canal Jimmy",			0x33C3, 0x2FC3, 0x3F43, 0x0000 },
	{ 30,	"FR", "Canal+",				0x33F4, 0x2F04, 0x3F04, 0x0000 },
	{ 31,	"FR", "Euronews",			0xFE01, 0x2FE1, 0x3F61, 0x0000 },
	{ 32,	"FR", "Eurosport",			0xF101, 0x2FE2, 0x3F62, 0x0000 },
	{ 33,	"FR", "France 2",			0x33F2, 0x2F02, 0x3F02, 0x0000 },
	{ 34,	"FR", "France 3",			0x33F3, 0x2F03, 0x3F03, 0x0000 },
	{ 35,	"FR", "La Chaîne Météo",		0x33C5, 0x2FC5, 0x3F45, 0x0000 },
	{ 36,	"FR", "LCI",				0x33C4, 0x2FC4, 0x3F44, 0x0000 },
	{ 37,	"FR", "M6",				0x33F6, 0x2F06, 0x3F06, 0x0000 },
	{ 38,	"FR", "MCM",				0x33C6, 0x2FC6, 0x3F46, 0x0000 },
	{ 39,	"FR", "Paris Première",			0x33C8, 0x2FC8, 0x3F48, 0x0000 },
	{ 40,	"FR", "Planète",			0x33C9, 0x2FC9, 0x3F49, 0x0000 },
	{ 41,	"FR", "RFO1",				0x3311, 0x2F11, 0x3F11, 0x0000 },
	{ 42,	"FR", "RFO2",				0x3312, 0x2F12, 0x3F12, 0x0000 },
	{ 43,	"FR", "Série Club",			0x33CA, 0x2FCA, 0x3F4A, 0x0000 },
	{ 44,	"FR", "Télétoon",			0x33CB, 0x2FCB, 0x3F4B, 0x0000 },
	{ 45,	"FR", "Téva",				0x33CC, 0x2FCC, 0x3F4C, 0x0000 },
	{ 46,	"FR", "TF1",				0x33F1, 0x2F01, 0x3F01, 0x0000 },
	{ 47,	"FR", "TLM",				0x3321, 0x2F21, 0x3F21, 0x0000 },
	{ 48,	"FR", "TLT",				0x3322, 0x2F22, 0x3F22, 0x0000 },
	{ 49,	"FR", "TMC Monte-Carlo",		0x33C7, 0x2FC7, 0x3F47, 0x0000 },
	{ 50,	"FR", "TV5",				0xF500, 0x2FE5, 0x3F65, 0x0000 },

	/* Table B.1 */

	{ 51,	"DE", "FESTIVAL",			0x4941, 0x0000, 0x0000, 0x0D41 },
	{ 52,	"DE", "MUXX",				0x4942, 0x0000, 0x0000, 0x0D42 },
	{ 53,	"DE", "EXTRA",				0x4943, 0x0000, 0x0000, 0x0D43 },

	{ 54,	"DE", "ONYX-TV",			0x0000, 0x0000, 0x0000, 0x0D7C },
	{ 55,	"DE", "QVC-Teleshopping",		0x5C49, 0x0000, 0x0000, 0x0D7D },
	{ 56,	"DE", "Nickelodeon",			0x0000, 0x0000, 0x0000, 0x0D7E },
	{ 57,	"DE", "Home Shopping Europe",	        0x49BF, 0x0000, 0x0000, 0x0D7F },

	{ 58,	"DE", "ORB-1 Regional",			0x0000, 0x0000, 0x0000, 0x0D81 },
	{ 59,	"DE", "ORB-3",				0x4982, 0x0000, 0x0000, 0x0D82 },
	/* not used 0x0D83 */
	/* not used 0x0D84 */
	{ 60,	"DE", "Arte",				0x490A, 0x0000, 0x3D05, 0x0D85 },
	/* not in TR 101 231: 0x3D05 */
	/* not used 0x0D86 */
	{ 61,	"DE", "1A-Fernsehen",			0x0000, 0x0000, 0x0000, 0x0D87 },
	{ 62,	"DE", "VIVA",				0x0000, 0x0000, 0x0000, 0x0D88 },
	{ 63,	"DE", "VIVA 2",				0x0000, 0x0000, 0x0000, 0x0D89 },
	{ 64,	"DE", "Super RTL",			0x0000, 0x0000, 0x0000, 0x0D8A },
	{ 65,	"DE", "RTL Club",			0x0000, 0x0000, 0x0000, 0x0D8B },
	{ 66,	"DE", "n-tv",				0x0000, 0x0000, 0x0000, 0x0D8C },
	{ 67,	"DE", "Deutsches Sportfernsehen",	0x0000, 0x0000, 0x0000, 0x0D8D },
	{ 68,	"DE", "VOX",		                0x490C, 0x0000, 0x0000, 0x0D8E },
	{ 69,	"DE", "RTL 2",				0x0D8F, 0x0000, 0x0000, 0x0D8F },
	/* not in TR 101 231: 0x0D8F */
	{ 70,	"DE", "RTL 2 Regional",			0x0000, 0x0000, 0x0000, 0x0D90 },
	{ 71,	"DE", "Eurosport",			0x0000, 0x0000, 0x0000, 0x0D91 },
	{ 72,	"DE", "Kabel 1",			0x0000, 0x0000, 0x0000, 0x0D92 },
	/* not used 0x0D93 */
	{ 73,	"DE", "PRO 7",				0x0000, 0x0000, 0x0000, 0x0D94 },
	/* not in TR 101 231: 0xAE8, 0xD14
	   Note VPS may be 0xD94 or 0xD14 */
	{ 74,	"DE", "PRO 7 Austria",			0x0AE8, 0x0000, 0x0000, 0x0D14 },
	{ 75,	"DE", "SAT 1 Brandenburg",		0x0000, 0x0000, 0x0000, 0x0D95 },
	{ 76,	"DE", "SAT 1 Thüringen",		0x0000, 0x0000, 0x0000, 0x0D96 },
	{ 77,	"DE", "SAT 1 Sachsen",			0x0000, 0x0000, 0x0000, 0x0D97 },
	{ 78,	"DE", "SAT 1 Mecklenb.-Vorpommern",	0x0000, 0x0000, 0x0000, 0x0D98 },
	{ 79,	"DE", "SAT 1 Sachsen-Anhalt",		0x0000, 0x0000, 0x0000, 0x0D99 },
	{ 80,	"DE", "RTL Regional",			0x0000, 0x0000, 0x0000, 0x0D9A },
	{ 81,	"DE", "RTL Schleswig-Holstein",		0x0000, 0x0000, 0x0000, 0x0D9B },
	{ 82,	"DE", "RTL Hamburg",			0x0000, 0x0000, 0x0000, 0x0D9C },
	{ 83,	"DE", "RTL Berlin",			0x0000, 0x0000, 0x0000, 0x0D9D },
	{ 84,	"DE", "RTL Niedersachsen",		0x0000, 0x0000, 0x0000, 0x0D9E },
	{ 85,	"DE", "RTL Bremen",			0x0000, 0x0000, 0x0000, 0x0D9F },
	{ 86,	"DE", "RTL Nordrhein-Westfalen",	0x0000, 0x0000, 0x0000, 0x0DA0 },
	{ 87,	"DE", "RTL Hessen",			0x0000, 0x0000, 0x0000, 0x0DA1 },
	{ 88,	"DE", "RTL Rheinland-Pfalz",		0x0000, 0x0000, 0x0000, 0x0DA2 },
	{ 89,	"DE", "RTL Baden-Württemberg",		0x0000, 0x0000, 0x0000, 0x0DA3 },
	{ 90,	"DE", "RTL Bayern",			0x0000, 0x0000, 0x0000, 0x0DA4 },
	{ 91,	"DE", "RTL Saarland",			0x0000, 0x0000, 0x0000, 0x0DA5 },
	{ 92,	"DE", "RTL Sachsen-Anhalt",		0x0000, 0x0000, 0x0000, 0x0DA6 },
	{ 93,	"DE", "RTL Mecklenburg-Vorpommern",	0x0000, 0x0000, 0x0000, 0x0DA7 },
	{ 94,	"DE", "RTL Sachsen",			0x0000, 0x0000, 0x0000, 0x0DA8 },
	{ 95,	"DE", "RTL Thüringen",			0x0000, 0x0000, 0x0000, 0x0DA9 },
	{ 96,	"DE", "RTL Brandenburg",		0x0000, 0x0000, 0x0000, 0x0DAA },
	{ 97,	"DE", "RTL",		           	0x0000, 0x0000, 0x0000, 0x0DAB },
	{ 98,	"DE", "Premiere",			0x0000, 0x0000, 0x0000, 0x0DAC },
	{ 99,	"DE", "SAT 1 Regional",			0x0000, 0x0000, 0x0000, 0x0DAD },
	{ 100,	"DE", "SAT 1 Schleswig-Holstein",	0x0000, 0x0000, 0x0000, 0x0DAE },
	{ 101,	"DE", "SAT 1 Hamburg",			0x0000, 0x0000, 0x0000, 0x0DAF },
	{ 102,	"DE", "SAT 1 Berlin",			0x0000, 0x0000, 0x0000, 0x0DB0 },
	{ 103,	"DE", "SAT 1 Niedersachsen",		0x0000, 0x0000, 0x0000, 0x0DB1 },
	{ 104,	"DE", "SAT 1 Bremen",			0x0000, 0x0000, 0x0000, 0x0DB2 },
	{ 105,	"DE", "SAT 1 Nordrhein-Westfalen",	0x0000, 0x0000, 0x0000, 0x0DB3 },
	{ 106,	"DE", "SAT 1 Hessen",			0x0000, 0x0000, 0x0000, 0x0DB4 },
	{ 107,	"DE", "SAT 1 Rheinland-Pfalz",		0x0000, 0x0000, 0x0000, 0x0DB5 },
	{ 108,	"DE", "SAT 1 Baden-Württemberg",	0x0000, 0x0000, 0x0000, 0x0DB6 },
	{ 109,	"DE", "SAT 1 Bayern",			0x0000, 0x0000, 0x0000, 0x0DB7 },
	{ 110,	"DE", "SAT 1 Saarland",			0x0000, 0x0000, 0x0000, 0x0DB8 },
	{ 111,	"DE", "SAT 1",				0x0000, 0x0000, 0x0000, 0x0DB9 },
	{ 112,	"DE", "NEUN LIVE",	                0x0000, 0x0000, 0x0000, 0x0DBA },
	{ 113,	"DE", "Deutsche Welle TV Berlin",	0x0000, 0x0000, 0x0000, 0x0DBB },
	{ 114,	"DE", "Berlin Offener Kanal",		0x0000, 0x0000, 0x0000, 0x0DBD },
	{ 115,	"DE", "Berlin-Mix-Channel 2",		0x0000, 0x0000, 0x0000, 0x0DBE },
	{ 116,	"DE", "Berlin-Mix-Channel 1",		0x0000, 0x0000, 0x0000, 0x0DBF },

	{ 117,	"DE", "ARD",				0x4901, 0x0000, 0x3D41, 0x0DC1 },
	/* not in TR 101 231: 0x3D41 */
	{ 118,	"DE", "ZDF",				0x4902, 0x0000, 0x3D42, 0x0DC2 },
	/* not in TR 101 231: 0x3D42 */
	/* "NOTE: As this code is used for a time in two networks a
	   distinction for automatic tuning systems is given in data line 16:
	   [VPS] bit 3 of byte 5 = 1 for the ARD network / = 0 for the ZDF
	   network." */
	{ 119,	"DE", "ARD/ZDF Vormittagsprogramm",	0x0000, 0x0000, 0x0000, 0x0DC3 },
	{ 120,	"DE", "ARD-TV-Sternpunkt",		0x0000, 0x0000, 0x0000, 0x0DC4 },
	{ 121,	"DE", "ARD-TV-Sternpunkt-Fehler",	0x0000, 0x0000, 0x0000, 0x0DC5 },
	/* not used 0x0DC6 */
	{ 122,	"DE", "3sat",				0x49C7, 0x0000, 0x0000, 0x0DC7 },
	{ 123,	"DE", "Phoenix",			0x4908, 0x0000, 0x0000, 0x0DC8 },
	{ 124,	"DE", "ARD/ZDF Kinderkanal",		0x49C9, 0x0000, 0x0000, 0x0DC9 },
	{ 125,	"DE", "BR-1 Regional",			0x0000, 0x0000, 0x0000, 0x0DCA },
	{ 126,	"DE", "BR-3",				0x49CB, 0x0000, 0x3D4B, 0x0DCB },
	/* not in TR 101 231: 0x3D4B */
	{ 377,	"DE", "BR-Alpha",			0x4944, 0x0000, 0x0000, 0x0000 },
	{ 127,	"DE", "BR-3 Süd",			0x0000, 0x0000, 0x0000, 0x0DCC },
	{ 128,	"DE", "BR-3 Nord",			0x0000, 0x0000, 0x0000, 0x0DCD },
	{ 129,	"DE", "HR-1 Regional",			0x0000, 0x0000, 0x0000, 0x0DCE },
	{ 130,	"DE", "Hessen 3",			0x49FF, 0x0000, 0x0000, 0x0DCF },
	{ 131,	"DE", "NDR-1 Dreiländerweit",		0x0000, 0x0000, 0x0000, 0x0DD0 },
	{ 132,	"DE", "NDR-1 Hamburg",			0x0000, 0x0000, 0x0000, 0x0DD1 },
	{ 133,	"DE", "NDR-1 Niedersachsen",		0x0000, 0x0000, 0x0000, 0x0DD2 },
	{ 134,	"DE", "NDR-1 Schleswig-Holstein",	0x0000, 0x0000, 0x0000, 0x0DD3 },
	{ 135,	"DE", "Nord-3 (NDR/SFB/RB)",		0x0000, 0x0000, 0x0000, 0x0DD4 },
	{ 136,	"DE", "NDR-3 Dreiländerweit",		0x49D4, 0x0000, 0x0000, 0x0DD5 },
	{ 137,	"DE", "NDR-3 Hamburg",			0x0000, 0x0000, 0x0000, 0x0DD6 },
	{ 138,	"DE", "NDR-3 Niedersachsen",		0x0000, 0x0000, 0x0000, 0x0DD7 },
	{ 139,	"DE", "NDR-3 Schleswig-Holstein",	0x0000, 0x0000, 0x0000, 0x0DD8 },
	{ 140,	"DE", "RB-1 Regional",			0x0000, 0x0000, 0x0000, 0x0DD9 },
	{ 141,	"DE", "RB-3",				0x49D9, 0x0000, 0x0000, 0x0DDA },
	{ 142,	"DE", "SFB-1 Regional",			0x0000, 0x0000, 0x0000, 0x0DDB },
	{ 143,	"DE", "SFB-3",				0x49DC, 0x0000, 0x0000, 0x0DDC },
	{ 144,	"DE", "SDR/SWF Baden-Württemberg",	0x0000, 0x0000, 0x0000, 0x0DDD },
	{ 145,	"DE", "SWF-1 Rheinland-Pfalz",		0x0000, 0x0000, 0x0000, 0x0DDE },
	{ 146,	"DE", "SR-1 Regional",			0x49DF, 0x0000, 0x0000, 0x0DDF },
	{ 147,	"DE", "Südwest 3 (SDR/SR/SWF)",		0x0000, 0x0000, 0x0000, 0x0DE0 },
	{ 148,	"DE", "SW 3 Baden-Württemberg",		0x49E1, 0x0000, 0x0000, 0x0DE1 },
	{ 149,	"DE", "SW 3 Saarland",			0x0000, 0x0000, 0x0000, 0x0DE2 },
	{ 150,	"DE", "SW 3 Baden-Württemb. Süd",	0x0000, 0x0000, 0x0000, 0x0DE3 },
	{ 151,	"DE", "SW 3 Rheinland-Pfalz",		0x49E4, 0x0000, 0x0000, 0x0DE4 },
	{ 152,	"DE", "WDR-1 Regionalprogramm",		0x0000, 0x0000, 0x0000, 0x0DE5 },
	{ 153,	"DE", "WDR-3",				0x49E6, 0x0000, 0x0000, 0x0DE6 },
	{ 154,	"DE", "WDR-3 Bielefeld",		0x0000, 0x0000, 0x0000, 0x0DE7 },
	{ 155,	"DE", "WDR-3 Dortmund",			0x0000, 0x0000, 0x0000, 0x0DE8 },
	{ 156,	"DE", "WDR-3 Düsseldorf",		0x0000, 0x0000, 0x0000, 0x0DE9 },
	{ 157,	"DE", "WDR-3 Köln",			0x0000, 0x0000, 0x0000, 0x0DEA },
	{ 158,	"DE", "WDR-3 Münster",			0x0000, 0x0000, 0x0000, 0x0DEB },
	{ 159,	"DE", "SDR-1 Regional",			0x0000, 0x0000, 0x0000, 0x0DEC },
	{ 160,	"DE", "SW 3 Baden-Württemberg Nord",	0x0000, 0x0000, 0x0000, 0x0DED },
	{ 161,	"DE", "SW 3 Mannheim",			0x0000, 0x0000, 0x0000, 0x0DEE },
	{ 162,	"DE", "SDR/SWF BW und Rhld-Pfalz",	0x0000, 0x0000, 0x0000, 0x0DEF },
	{ 163,	"DE", "SWF-1 / Regionalprogramm",	0x0000, 0x0000, 0x0000, 0x0DF0 },
	{ 164,	"DE", "NDR-1 Mecklenb.-Vorpommern",	0x0000, 0x0000, 0x0000, 0x0DF1 },
	{ 165,	"DE", "NDR-3 Mecklenb.-Vorpommern",	0x0000, 0x0000, 0x0000, 0x0DF2 },
	{ 166,	"DE", "MDR-1 Sachsen",			0x0000, 0x0000, 0x0000, 0x0DF3 },
	{ 167,	"DE", "MDR-3 Sachsen",			0x0000, 0x0000, 0x0000, 0x0DF4 },
	{ 168,	"DE", "MDR Dresden",			0x0000, 0x0000, 0x0000, 0x0DF5 },
	{ 169,	"DE", "MDR-1 Sachsen-Anhalt",		0x0000, 0x0000, 0x0000, 0x0DF6 },
	{ 170,	"DE", "WDR Dortmund",			0x0000, 0x0000, 0x0000, 0x0DF7 },
	{ 171,	"DE", "MDR-3 Sachsen-Anhalt",		0x0000, 0x0000, 0x0000, 0x0DF8 },
	{ 172,	"DE", "MDR Magdeburg",			0x0000, 0x0000, 0x0000, 0x0DF9 },
	{ 173,	"DE", "MDR-1 Thüringen",		0x0000, 0x0000, 0x0000, 0x0DFA },
	{ 174,	"DE", "MDR-3 Thüringen",		0x0000, 0x0000, 0x0000, 0x0DFB },
	{ 175,	"DE", "MDR Erfurt",			0x0000, 0x0000, 0x0000, 0x0DFC },
	{ 176,	"DE", "MDR-1 Regional",			0x0000, 0x0000, 0x0000, 0x0DFD },
	{ 177,	"DE", "MDR-3",				0x49FE, 0x0000, 0x0000, 0x0DFE },

	{ 178,	"CH", "TeleZüri",			0x0000, 0x0000, 0x0000, 0x0481 },
	{ 179,	"CH", "Teleclub Abo-Fernsehen",		0x0000, 0x0000, 0x0000, 0x0482 },
	{ 180,	"CH", "Zürich 1",			0x0000, 0x0000, 0x0000, 0x0483 },
	{ 181,	"CH", "TeleBern",			0x0000, 0x0000, 0x0000, 0x0484 },
	{ 182,	"CH", "Tele M1",			0x0000, 0x0000, 0x0000, 0x0485 },
	{ 183,	"CH", "Star TV",			0x0000, 0x0000, 0x0000, 0x0486 },
	{ 184,	"CH", "Pro 7",				0x0000, 0x0000, 0x0000, 0x0487 },
	{ 185,	"CH", "TopTV",				0x0000, 0x0000, 0x0000, 0x0488 },

	{ 186,	"CH", "SRG Schweizer Fernsehen SF 1",	0x4101, 0x24C1, 0x3441, 0x04C1 },
	{ 187,	"CH", "SSR Télévis. Suisse TSR 1",	0x4102, 0x24C2, 0x3442, 0x04C2 },
	{ 188,	"CH", "SSR Televis. svizzera TSI 1",	0x4103, 0x24C3, 0x3443, 0x04C3 },
	/* not used 0x04C4 */
	/* not used 0x04C5 */
	/* not used 0x04C6 */
	{ 189,	"CH", "SRG Schweizer Fernsehen SF 2",	0x4107, 0x24C7, 0x3447, 0x04C7 },
	{ 190,	"CH", "SSR Télévis. Suisse TSR 2",	0x4108, 0x24C8, 0x3448, 0x04C8 },
	{ 191,	"CH", "SSR Televis. svizzera TSI 2",	0x4109, 0x24C9, 0x3449, 0x04C9 },
	{ 192,	"CH", "SRG SSR Sat Access",		0x410A, 0x24CA, 0x344A, 0x04CA },

	{ 193,	"AT", "ORF 1",				0x4301, 0x0000, 0x0000, 0x0AC1 },
	{ 194,	"AT", "ORF 2",				0x4302, 0x0000, 0x0000, 0x0AC2 },
	{ 195,	"AT", "ORF 3",				0x0000, 0x0000, 0x0000, 0x0AC3 },
	{ 196,	"AT", "ORF Burgenland",			0x0000, 0x0000, 0x0000, 0x0ACB },
	{ 197,	"AT", "ORF Kärnten",			0x0000, 0x0000, 0x0000, 0x0ACC },
	{ 198,	"AT", "ORF Niederösterreich",		0x0000, 0x0000, 0x0000, 0x0ACD },
	{ 199,	"AT", "ORF Oberösterreich",		0x0000, 0x0000, 0x0000, 0x0ACE },
	{ 200,	"AT", "ORF Salzburg",			0x0000, 0x0000, 0x0000, 0x0ACF },
	{ 201,	"AT", "ORF Steiermark",			0x0000, 0x0000, 0x0000, 0x0AD0 },
	{ 202,	"AT", "ORF Tirol",			0x0000, 0x0000, 0x0000, 0x0AD1 },
	{ 203,	"AT", "ORF Vorarlberg",			0x0000, 0x0000, 0x0000, 0x0AD2 },
	{ 204,	"AT", "ORF Wien",			0x0000, 0x0000, 0x0000, 0x0AD3 },
	/* not in TR 101 231: 0x430C, 0xADE */
	{ 205,	"AT", "ATV+",				0x430C, 0x0000, 0x0000, 0x0ADE },

	/* Table A.1 continued */

	{ 206,	"GR", "ET-1",				0x3001, 0x2101, 0x3101, 0x0000 },
	{ 207,	"GR", "NET",				0x3002, 0x2102, 0x3102, 0x0000 },
	{ 208,	"GR", "ET-3",				0x3003, 0x2103, 0x3103, 0x0000 },

	{ 209,	"HU", "Duna Televizio",			0x3636, 0x0000, 0x0000, 0x0000 },
	{ 210,	"HU", "MTV1",				0x3601, 0x0000, 0x0000, 0x0000 },
	{ 211,	"HU", "MTV1 Budapest",			0x3611, 0x0000, 0x0000, 0x0000 },
	{ 212,	"HU", "MTV1 Debrecen",			0x3651, 0x0000, 0x0000, 0x0000 },
	{ 213,	"HU", "MTV1 Miskolc",			0x3661, 0x0000, 0x0000, 0x0000 },
	{ 214,	"HU", "MTV1 Pécs",			0x3621, 0x0000, 0x0000, 0x0000 },
	{ 215,	"HU", "MTV1 Szeged",			0x3631, 0x0000, 0x0000, 0x0000 },
	{ 216,	"HU", "MTV1 Szombathely",		0x3641, 0x0000, 0x0000, 0x0000 },
	{ 217,	"HU", "MTV2",				0x3602, 0x0000, 0x0000, 0x0000 },
	{ 218,	"HU", "tv2",				0x3622, 0x0000, 0x0000, 0x0000 },

	{ 219,	"IS", "Rikisutvarpid-Sjonvarp",		0x3541, 0x0000, 0x0000, 0x0000 },

	{ 220,	"IE", "Network 2",			0x3532, 0x4202, 0x3202, 0x0000 },
	{ 221,	"IE", "RTE1",				0x3531, 0x4201, 0x3201, 0x0000 },
	{ 222,	"IE", "Teilifis na Gaeilge",		0x3533, 0x4203, 0x3203, 0x0000 },
	{ 223,	"IE", "TV3",				0x3333, 0x0000, 0x0000, 0x0000 },

	{ 224,	"IT", "Arte",				0x390A, 0x0000, 0x0000, 0x0000 },
	{ 225,	"IT", "Canale 5",			0xFA05, 0x0000, 0x0000, 0x0000 },
	{ 226,	"IT", "Italia 1",			0xFA06, 0x0000, 0x0000, 0x0000 },
	{ 227,	"IT", "RAI 1",				0x3901, 0x0000, 0x0000, 0x0000 },
	{ 228,	"IT", "RAI 2",				0x3902, 0x0000, 0x0000, 0x0000 },
	{ 229,	"IT", "RAI 3",				0x3903, 0x0000, 0x0000, 0x0000 },
	{ 230,	"IT", "Rete 4",				0xFA04, 0x0000, 0x0000, 0x0000 },
	{ 231,	"IT", "Rete A",				0x3904, 0x0000, 0x0000, 0x0000 },
	{ 232,	"IT", "RTV38",				0x3938, 0x0000, 0x0000, 0x0000 },
	{ 233,	"IT", "Tele+1",				0x3997, 0x0000, 0x0000, 0x0000 },
	{ 234,	"IT", "Tele+2",				0x3998, 0x0000, 0x0000, 0x0000 },
	{ 235,	"IT", "Tele+3",				0x3999, 0x0000, 0x0000, 0x0000 },
	{ 236,	"IT", "TMC",				0xFA08, 0x0000, 0x0000, 0x0000 },
	{ 237,	"IT", "TRS TV",				0x3910, 0x0000, 0x0000, 0x0000 },

	{ 238,	"LU", "RTL Télé Letzebuerg",		0x4000, 0x0000, 0x0000, 0x0000 },

	{ 239,	"NL", "Nederland 1",			0x3101, 0x4801, 0x3801, 0x0000 },
	{ 240,	"NL", "Nederland 2",			0x3102, 0x4802, 0x3802, 0x0000 },
	{ 241,	"NL", "Nederland 3",			0x3103, 0x4803, 0x3803, 0x0000 },
	{ 242,	"NL", "RTL 4",				0x3104, 0x4804, 0x3804, 0x0000 },
	{ 243,	"NL", "RTL 5",				0x3105, 0x4805, 0x3805, 0x0000 },
	{ 244,	"NL", "Veronica",			0x3106, 0x4806, 0x3806, 0x0000 },
	{ 379,  "NL", "The BOX",			0x3120, 0x4820, 0x3820, 0x0000 },
	{ 245,	"NL", "NRK1",				0x4701, 0x0000, 0x0000, 0x0000 },
	{ 246,	"NL", "NRK2",				0x4703, 0x0000, 0x0000, 0x0000 },
	{ 247,	"NL", "TV 2",				0x4702, 0x0000, 0x0000, 0x0000 },
	{ 248,	"NL", "TV Norge",			0x4704, 0x0000, 0x0000, 0x0000 },

	{ 249,	"PL", "TV Polonia",			0x4810, 0x0000, 0x0000, 0x0000 },
	{ 250,	"PL", "TVP1",				0x4801, 0x0000, 0x0000, 0x0000 },
	{ 251,	"PL", "TVP2",				0x4802, 0x0000, 0x0000, 0x0000 },

	{ 252,	"PT", "RTP1",				0x3510, 0x0000, 0x0000, 0x0000 },
	{ 253,	"PT", "RTP2",				0x3511, 0x0000, 0x0000, 0x0000 },
    	{ 254,	"PT", "RTPAF",				0x3512, 0x0000, 0x0000, 0x0000 },
	{ 255,	"PT", "RTPAZ",				0x3514, 0x0000, 0x0000, 0x0000 },
	{ 256,	"PT", "RTPI",				0x3513, 0x0000, 0x0000, 0x0000 },
	{ 257,	"PT", "RTPM",				0x3515, 0x0000, 0x0000, 0x0000 },

	{ 258,	"SM", "RTV",				0x3781, 0x0000, 0x0000, 0x0000 },

	{ 259,	"SK", "STV1",				0x42A1, 0x35A1, 0x3521, 0x0000 },
	{ 260,	"SK", "STV2",				0x42A2, 0x35A2, 0x3522, 0x0000 },
	{ 261,	"SK", "STV1 Kosice",			0x42A3, 0x35A3, 0x3523, 0x0000 },
	{ 262,	"SK", "STV2 Kosice",			0x42A4, 0x35A4, 0x3524, 0x0000 },
	{ 263,	"SK", "STV1 B. Bystrica",		0x42A5, 0x35A5, 0x3525, 0x0000 },
	{ 264,	"SK", "STV2 B. Bystrica",		0x42A6, 0x35A6, 0x3526, 0x0000 },

	{ 265,	"SI", "SLO1",				0xAAE1, 0x0000, 0x0000, 0x0000 },
	{ 266,	"SI", "SLO2",				0xAAE2, 0x0000, 0x0000, 0x0000 },
	{ 267,	"SI", "KC",				0xAAE3, 0x0000, 0x0000, 0x0000 },
	{ 268,	"SI", "TLM",				0xAAE4, 0x0000, 0x0000, 0x0000 },
	{ 269,	"SI", "SLO3",				0xAAF1, 0x0000, 0x0000, 0x0000 },

        { 270,	"ES", "Arte",				0x340A, 0x0000, 0x0000, 0x0000 },
	{ 271,	"ES", "C33",				0xCA33, 0x0000, 0x0000, 0x0000 },
	{ 272,	"ES", "ETB 1",				0xBA01, 0x0000, 0x0000, 0x0000 },
	{ 273,	"ES", "ETB 2",				0x3402, 0x0000, 0x0000, 0x0000 },
	{ 274,	"ES", "TV3",				0xCA03, 0x0000, 0x0000, 0x0000 },
	{ 275,	"ES", "TVE1",				0x3E00, 0x0000, 0x0000, 0x0000 },
	{ 276,	"ES", "TVE2",				0xE100, 0x0000, 0x0000, 0x0000 },
	{ 277,	"ES", "Canal+",				0xA55A, 0x0000, 0x0000, 0x0000 },
	/* not in TR 101 231: 0xA55A (valid?) */

	{ 278,	"SE", "SVT 1",				0x4601, 0x4E01, 0x3E01, 0x0000 },
	{ 279,	"SE", "SVT 2",				0x4602, 0x4E02, 0x3E02, 0x0000 },
	{ 280,	"SE", "SVT Test Transmissions",		0x4600, 0x4E00, 0x3E00, 0x0000 },
	{ 281,	"SE", "TV 4",				0x4640, 0x4E40, 0x3E40, 0x0000 },

	{ 289,	"TR", "ATV",				0x900A, 0x0000, 0x0000, 0x0000 },
	{ 290,	"TR", "AVRASYA",			0x9006, 0x4306, 0x3306, 0x0000 },
	{ 291,	"TR", "BRAVO TV",			0x900E, 0x0000, 0x0000, 0x0000 },
	{ 292,	"TR", "Cine 5",				0x9008, 0x0000, 0x0000, 0x0000 },
	{ 293,	"TR", "EKO TV",				0x900D, 0x0000, 0x0000, 0x0000 },
	{ 294,	"TR", "EURO D",				0x900C, 0x0000, 0x0000, 0x0000 },
	{ 295,	"TR", "FUN TV",				0x9010, 0x0000, 0x0000, 0x0000 },
	{ 296,	"TR", "GALAKSI TV",			0x900F, 0x0000, 0x0000, 0x0000 },
	{ 297,	"TR", "KANAL D",			0x900B, 0x0000, 0x0000, 0x0000 },
	{ 298,	"TR", "Show TV",			0x9007, 0x0000, 0x0000, 0x0000 },
	{ 299,	"TR", "Super Sport",			0x9009, 0x0000, 0x0000, 0x0000 },
	{ 300,	"TR", "TEMPO TV",			0x9011, 0x0000, 0x0000, 0x0000 },
	{ 301,	"TR", "TGRT",				0x9014, 0x0000, 0x0000, 0x0000 },
	{ 302,	"TR", "TRT-1",				0x9001, 0x4301, 0x3301, 0x0000 },
	{ 303,	"TR", "TRT-2",				0x9002, 0x4302, 0x3302, 0x0000 },
	{ 304,	"TR", "TRT-3",				0x9003, 0x4303, 0x3303, 0x0000 },
	{ 305,	"TR", "TRT-4",				0x9004, 0x4304, 0x3304, 0x0000 },
	{ 306,	"TR", "TRT-INT",			0x9005, 0x4305, 0x3305, 0x0000 },
	/* ?? TRT-INT transmits 0x9001 */

	{ 307,	"GB", "ANGLIA TV",			0xFB9C, 0x2C1C, 0x3C1C, 0x0000 },
	{ 308,	"GB", "BBC News 24",			0x4469, 0x2C69, 0x3C69, 0x0000 },
	{ 309,	"GB", "BBC Prime",			0x4468, 0x2C68, 0x3C68, 0x0000 },
	{ 310,	"GB", "BBC World",			0x4457, 0x2C57, 0x3C57, 0x0000 },
	{ 311,	"GB", "BBC1",				0x447F, 0x2C7F, 0x3C7F, 0x0000 },
	{ 312,	"GB", "BBC1 NI",			0x4441, 0x2C41, 0x3C41, 0x0000 },
	{ 313,	"GB", "BBC1 Scotland",			0x447B, 0x2C7B, 0x3C7B, 0x0000 },
	{ 314,	"GB", "BBC1 Wales",			0x447D, 0x2C7D, 0x3C7D, 0x0000 },
	{ 315,	"GB", "BBC2",				0x4440, 0x2C40, 0x3C40, 0x0000 },
	{ 316,	"GB", "BBC2 NI",			0x447E, 0x2C7E, 0x3C7E, 0x0000 },
	{ 317,	"GB", "BBC2 Scotland",			0x4444, 0x2C44, 0x3C44, 0x0000 },
	{ 318,	"GB", "BBC2 Wales",			0x4442, 0x2C42, 0x3C42, 0x0000 },
	{ 319,	"GB", "BORDER TV",			0xB7F7, 0x2C27, 0x3C27, 0x0000 },
	{ 320,	"GB", "BRAVO",				0x4405, 0x5BEF, 0x3B6F, 0x0000 },
	{ 321,	"GB", "CARLTON SELECT",			0x82E1, 0x2C05, 0x3C05, 0x0000 },
	{ 322,	"GB", "CARLTON TV",			0x82DD, 0x2C1D, 0x3C1D, 0x0000 },
	{ 323,	"GB", "CENTRAL TV",			0x2F27, 0x2C37, 0x3C37, 0x0000 },
	{ 324,	"GB", "CHANNEL 4",			0xFCD1, 0x2C11, 0x3C11, 0x0000 },
	{ 325,	"GB", "CHANNEL 5 (1)",			0x9602, 0x2C02, 0x3C02, 0x0000 },
	{ 326,	"GB", "CHANNEL 5 (2)",			0x1609, 0x2C09, 0x3C09, 0x0000 },
	{ 327,	"GB", "CHANNEL 5 (3)",			0x28EB, 0x2C2B, 0x3C2B, 0x0000 },
	{ 328,	"GB", "CHANNEL 5 (4)",			0xC47B, 0x2C3B, 0x3C3B, 0x0000 },
	{ 329,	"GB", "CHANNEL TV",			0xFCE4, 0x2C24, 0x3C24, 0x0000 },
	{ 330,	"GB", "CHILDREN'S CHANNEL",		0x4404, 0x5BF0, 0x3B70, 0x0000 },
	{ 331,	"GB", "CNN International",		0x01F2, 0x5BF1, 0x3B71, 0x0000 },
	{ 332,	"GB", "DISCOVERY",			0x4407, 0x5BF2, 0x3B72, 0x0000 },
	{ 333,	"GB", "DISNEY CHANNEL UK",		0x44D1, 0x5BCC, 0x3B4C, 0x0000 },
	{ 334,	"GB", "FAMILY CHANNEL",			0x4408, 0x5BF3, 0x3B73, 0x0000 },
	{ 335,	"GB", "GMTV",				0xADDC, 0x5BD2, 0x3B52, 0x0000 },
	{ 336,	"GB", "GRAMPIAN TV",			0xF33A, 0x2C3A, 0x3C3A, 0x0000 },
	{ 337,	"GB", "GRANADA PLUS",			0x4D5A, 0x5BF4, 0x3B74, 0x0000 },
	{ 338,	"GB", "GRANADA Timeshare",		0x4D5B, 0x5BF5, 0x3B75, 0x0000 },
	{ 339,	"GB", "GRANADA TV",			0xADD8, 0x2C18, 0x3C18, 0x0000 },
	{ 340,	"GB", "HISTORY CHANNEL",		0xFCF4, 0x5BF6, 0x3B76, 0x0000 },
	{ 341,	"GB", "HTV",				0x5AAF, 0x2C3F, 0x3C3F, 0x0000 },
	{ 342,	"GB", "ITV NETWORK",			0xC8DE, 0x2C1E, 0x3C1E, 0x0000 },
	{ 343,	"GB", "LEARNING CHANNEL",		0x4406, 0x5BF7, 0x3B77, 0x0000 },
	{ 344,	"GB", "Live TV",			0x4409, 0x5BF8, 0x3B78, 0x0000 },
	{ 345,	"GB", "LWT",				0x884B, 0x2C0B, 0x3C0B, 0x0000 },
	{ 346,	"GB", "MERIDIAN",			0x10E4, 0x2C34, 0x3C34, 0x0000 },
	{ 347,	"GB", "MOVIE CHANNEL",			0xFCFB, 0x2C1B, 0x3C1B, 0x0000 },
	{ 348,	"GB", "MTV",				0x4D54, 0x2C14, 0x3C14, 0x0000 },
	{ 380,  "GB", "National Geographic Channel",	0x320B, 0x0000, 0x0000, 0x0000 },
	{ 349,	"GB", "NBC Europe",			0x8E71, 0x2C31, 0x3C31, 0x0E86 },
	/* not in TR 101 231: 0x0E86 */
	{ 350,	"GB", "Nickelodeon UK",			0xA460, 0x0000, 0x0000, 0x0000 },
	{ 351,	"GB", "Paramount Comedy Channel UK",	0xA465, 0x0000, 0x0000, 0x0000 },
	{ 352,	"GB", "QVC UK",				0x5C44, 0x0000, 0x0000, 0x0000 },
	{ 353,	"GB", "RACING CHANNEL",			0xFCF3, 0x2C13, 0x3C13, 0x0000 },
	{ 354,	"GB", "Sianel Pedwar Cymru",		0xB4C7, 0x2C07, 0x3C07, 0x0000 },
	{ 355,	"GB", "SCI FI CHANNEL",			0xFCF5, 0x2C15, 0x3C15, 0x0000 },
	{ 356,	"GB", "SCOTTISH TV",			0xF9D2, 0x2C12, 0x3C12, 0x0000 },
	{ 357,	"GB", "SKY GOLD",			0xFCF9, 0x2C19, 0x3C19, 0x0000 },
	{ 358,	"GB", "SKY MOVIES PLUS",		0xFCFC, 0x2C0C, 0x3C0C, 0x0000 },
	{ 359,	"GB", "SKY NEWS",			0xFCFD, 0x2C0D, 0x3C0D, 0x0000 },
	{ 360,	"GB", "SKY ONE",			0xFCFE, 0x2C0E, 0x3C0E, 0x0000 },
	{ 361,	"GB", "SKY SOAPS",			0xFCF7, 0x2C17, 0x3C17, 0x0000 },
	{ 362,	"GB", "SKY SPORTS",			0xFCFA, 0x2C1A, 0x3C1A, 0x0000 },
	{ 363,	"GB", "SKY SPORTS 2",			0xFCF8, 0x2C08, 0x3C08, 0x0000 },
	{ 364,	"GB", "SKY TRAVEL",			0xFCF6, 0x5BF9, 0x3B79, 0x0000 },
	{ 365,	"GB", "SKY TWO",			0xFCFF, 0x2C0F, 0x3C0F, 0x0000 },
	{ 366,	"GB", "SSVC",				0x37E5, 0x2C25, 0x3C25, 0x0000 },
	{ 367,	"GB", "TNT / Cartoon Network",		0x44C1, 0x0000, 0x0000, 0x0000 },
	{ 368,	"GB", "TYNE TEES TV",			0xA82C, 0x2C2C, 0x3C2C, 0x0000 },
	{ 369,	"GB", "UK GOLD",			0x4401, 0x5BFA, 0x3B7A, 0x0000 },
	{ 370,	"GB", "UK LIVING",			0x4402, 0x2C01, 0x3C01, 0x0000 },
	{ 371,	"GB", "ULSTER TV",			0x833B, 0x2C3D, 0x3C3D, 0x0000 },
	{ 372,	"GB", "VH-1",				0x4D58, 0x2C20, 0x3C20, 0x0000 },
	{ 373,	"GB", "VH-1 German",			0x4D59, 0x2C21, 0x3C21, 0x0000 },
	{ 374,	"GB", "WESTCOUNTRY TV",			0x25D0, 0x2C30, 0x3C30, 0x0000 },
	{ 375,	"GB", "WIRE TV",			0x4403, 0x2C3C, 0x3C3C, 0x0000 },
	{ 376,	"GB", "YORKSHIRE TV",			0xFA2C, 0x2C2D, 0x3C2D, 0x0000 },

	{ 0, "",  0,  0, 0, 0, 0 }
};

/*
  0xxxxxxx custom
  11xxxxxx temporary
  101xxxxx call sign
  1001xxxx cni
  10000000 table
 */
#define VBI_NUID_TABLE(n) ((vbi_nuid)(n) | (((vbi_nuid) -128)) << 56)
#define VBI_NUID_IS_TEMP(nuid)		(-1 == (((vbi_nuid)(nuid)) >> 62))
#define VBI_NUID_IS_CALL_SIGN(nuid)	(-3 == (((vbi_nuid)(nuid)) >> 61))
#define VBI_NUID_IS_CNI(nuid)		(-15 == (((vbi_nuid)(nuid)) >> 60))
#define VBI_NUID_IS_TABLE(nuid)		(-128 == (((vbi_nuid)(nuid)) >> 56))

static unsigned int
cni_pdc_a_to_vps		(unsigned int		cni)
{
	unsigned int n;

	/* Relation guessed from values observed
	   in DE and AT. Is this defined anywhere? */

	switch (cni >> 12) {
	case 0x1A: /* Austria */
	case 0x1D: /* Germany */
		if (!vbi_is_bcd (cni & 0xFFF))
			return 0;

		n = vbi_bcd2dec (cni & 0xFFF);

		switch (n) {
		case 100 ... 163:
			cni = ((cni >> 4) & 0xF00) + n + 0xC0 - 100;
			break;

		case 200 ... 263: /* in DE */
			cni = ((cni >> 4) & 0xF00) + n + 0x80 - 200;
			break;

		default:
			return 0;
		}

		break;

	default:
		return 0;
	}

	return cni;
}

static unsigned int
cni_vps_to_pdc_a		(unsigned int		cni)
{
	switch (cni >> 8) {
	case 0xA: /* Austria */
	case 0xD: /* Germany */
		switch (cni & 0xFF) {
		case 0xC0 ... 0xFF:
			cni = ((cni << 4) & 0xF000) + 0x10000
				+ vbi_dec2bcd ((cni & 0xFF) - 0xC0 + 100);
			break;

		case 0x80 ... 0xBF:
			cni = ((cni << 4) & 0xF000) + 0x10000
				+ vbi_dec2bcd ((cni & 0xFF) - 0x80 + 200);
			break;

		default:
			return 0;
		}

		break;

	default:
		return 0;
	}

	return cni;
}

static const struct cni_entry *
cni_lookup			(vbi_cni_type		type,
				 unsigned int		cni)
{
	const struct cni_entry *p;

	/* FIXME this is too slow. Should use binary search. */

	if (0 == cni)
		return NULL;

	switch (type) {
	case VBI_CNI_TYPE_8301:
		for (p = cni_table; p->name; ++p)
			if (p->cni1 == cni) {
				return p;
			}
		break;

	case VBI_CNI_TYPE_8302:
		for (p = cni_table; p->name; ++p)
			if (p->cni2 == cni) {
				return p;
			}

		cni &= 0x0FFF;

		/* fall through */

	case VBI_CNI_TYPE_VPS:
		for (p = cni_table; p->name; ++p)
			if (p->cni4 == cni) {
				return p;
			}
		break;

	case VBI_CNI_TYPE_PDC_A:
	{
		unsigned int cni_vps;

		cni_vps = cni_pdc_a_to_vps (cni);

		if (0 == cni_vps)
			return NULL;

		for (p = cni_table; p->name; ++p)
			if (p->cni4 == cni_vps) {
				return p;
			}

		break;
	}

	case VBI_CNI_TYPE_PDC_B:
		for (p = cni_table; p->name; ++p)
			if (p->cni3 == cni) {
				return p;
			}

		/* try code | 0x0080 & 0x0FFF -> VPS ? */

		break;

	default:
		vbi_log_printf (VBI_DEBUG, __FUNCTION__,
				"Unknown CNI type %u\n", type);
		break;
	}

	return NULL;
}

/**
 * @param type
 * @param cni
 *
 * @returns
 */
vbi_nuid
vbi_nuid_from_cni		(vbi_cni_type		type,
				 unsigned int		cni)
{
	const struct cni_entry *p;
	vbi_nuid nuid;

	if (0 == cni)
		return VBI_NUID_NONE;

	if ((p = cni_lookup (type, cni)))
		return VBI_NUID_TABLE (p->id);

	/* Not in table. */

	assert ((unsigned int) type <= 15);

	nuid = ((vbi_nuid)(0x90 + (unsigned int) type) << 56)
		+ (cni & ~(((vbi_nuid) 0xFF) << 56));

	return nuid;
}

/**
 * @param to_type Type of CNI to convert to.
 * @param from_type Type of CNI to convert from.
 * @param cni CNI to convert.
 *
 * Converts a CNI from one type to another.
 *
 * @returns
 * Converted CNI or 0 if conversion is not possible.
 */
unsigned int
vbi_convert_cni			(vbi_cni_type		to_type,
				 vbi_cni_type		from_type,
				 unsigned int		cni)
{
	const struct cni_entry *p;

	if (!(p = cni_lookup (from_type, cni)))
		return 0;

	switch (to_type) {
	case VBI_CNI_TYPE_VPS:
		return p->cni4;
		break;

	case VBI_CNI_TYPE_8301:
		return p->cni1;
		break;

	case VBI_CNI_TYPE_8302:
		return p->cni2;
		break;

	case VBI_CNI_TYPE_PDC_A:
		return cni_vps_to_pdc_a (p->cni4);

	case VBI_CNI_TYPE_PDC_B:
		return p->cni3;
		break;

	default:
		vbi_log_printf (VBI_DEBUG, __FUNCTION__,
				"Unknown CNI type %u\n", to_type);
		return 0;
	}
}

void
vbi_network_delete		(vbi_network *		n)
{
	if (NULL == n)
		return;

	free (n->name);
	free (n->call_sign);

	CLEAR (*n);

	free (n);
}

vbi_network *
vbi_network_new			(vbi_nuid		nuid)
{
	vbi_network *n;

	if (!(n = calloc (1, sizeof (*n)))) {
		vbi_log_printf (VBI_DEBUG, __FUNCTION__,
				"Out of memory");
		return NULL;
	}

	n->nuid = nuid;

	if (VBI_NUID_NONE == nuid)
		return n;

	if (VBI_NUID_IS_TABLE (nuid)) {
		const struct cni_entry *p;
		unsigned int id;

		id = nuid & ~(((vbi_nuid) 0xFF) << 56);

		for (p = cni_table; p->name; ++p)
			if (p->id == id)
				break;

		if (!p->name)
			goto failure;

		n->name = _vbi_strdup_locale_utf8 (p->name);

		if (!n->name)
			goto failure;

		n->country_code = p->country;

		n->cni_vps = p->cni4;
		n->cni_8301 = p->cni1;
		n->cni_8302 = p->cni2;
		n->cni_pdc_a = cni_vps_to_pdc_a (p->cni4);
		n->cni_pdc_b = p->cni3;
	} else if (VBI_NUID_IS_CALL_SIGN (nuid)) {
		char buffer[8];
		unsigned int i;
		unsigned int l;

		l = ((nuid >> 56) & 31) + 1;
		l = MIN (l, 7U);

		for (i = 0; i < l; ++i)
			buffer[i] = 0xFF & (nuid >> (i * 8));

		buffer[i] = 0;

		n->call_sign = _vbi_strdup_locale_utf8 (buffer);

		if (!n->call_sign)
			goto failure;

		/* See http://www.fcc.gov/cgb/statid.html */
		switch (buffer[0]) {
		case 'A':
			switch (buffer[1]) {
			case 'A' ... 'F':
				n->country_code = "US";
				break;
			}
			break;

		case 'K':
		case 'W':
		case 'N':
			n->country_code = "US";
			break;

		case 'C':
			switch (buffer[1]) {
			case 'F' ... 'K':
			case 'Y' ... 'Z':
				n->country_code = "CA";
				break;
			}
			break;

		case 'V':
			switch (buffer[1]) {
			case 'A' ... 'G':
			case 'O':
			case 'X' ... 'Y':
				n->country_code = "CA";
				break;
			}
			break;

		case 'X':
			switch (buffer[1]) {
			case 'J' ... 'O':
				n->country_code = "CA";
				break;
			}
			break;
		}
	} else if (VBI_NUID_IS_CNI (nuid)) {
		vbi_cni_type type = (nuid >> 56) & 15;

		switch (type) {
		case VBI_CNI_TYPE_VPS:
			n->cni_vps = nuid & ~(((vbi_nuid) 0xFF) << 56);
			break;

		case VBI_CNI_TYPE_8301:
			n->cni_8301 = nuid & ~(((vbi_nuid) 0xFF) << 56);
			break;

		case VBI_CNI_TYPE_8302:
			n->cni_8302 = nuid & ~(((vbi_nuid) 0xFF) << 56);
			break;

		case VBI_CNI_TYPE_PDC_A:
			n->cni_pdc_a = nuid & ~(((vbi_nuid) 0xFF) << 56);
			break;

		case VBI_CNI_TYPE_PDC_B:
			n->cni_pdc_b = nuid & ~(((vbi_nuid) 0xFF) << 56);
			break;

		default:
			vbi_log_printf (VBI_DEBUG, __FUNCTION__,
					"Unknown CNI type %u\n", type);
		}
	}

	return n;

 failure:
	vbi_network_delete (n);

	return NULL;
}

/**
 * @internal
 * @param call_sign
 *
 * @returns
 */
vbi_nuid
vbi_nuid_from_call_sign		(const char *		call_sign,
				 unsigned int		length)
{
	vbi_nuid nuid;
	unsigned int i;

	assert (NULL != call_sign);

	if (0 == length) {
		length = strlen (call_sign);

		if (0 == length)
			return VBI_NUID_NONE;
	}

	nuid = ((vbi_nuid)(0xA0 + MIN (length, 32u) - 1)) << 56;

	length = MIN (length, 7u);

	for (i = 0; i < length; ++i)
		nuid |= ((vbi_nuid)(0xFF & call_sign[i])) << (i * 8);

	return nuid;
}

/**
 * @internal
 * @param call_sign
 *
 * @returns
 */
vbi_nuid
vbi_temporary_nuid		(void)
{
	vbi_nuid nuid;

	nuid = rand(); /* FIXME too simple */

	nuid |= ((vbi_nuid) -1) << 62;

	return nuid;
}
