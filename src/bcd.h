/*
 *  libzvbi - BCD arithmetic for Teletext page numbers
 *
 *  Copyright (C) 2001, 2002 Michael H. Schimek
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

/* $Id: bcd.h,v 1.11 2003-11-14 05:29:18 mschimek Exp $ */

#ifndef BCD_H
#define BCD_H

#include "misc.h"

/**
 * @addtogroup BCD BCD arithmetic for Teletext page numbers
 * @ingroup Service
 *
 * Teletext page numbers are expressed as packed binary coded decimal
 * numbers in range 0x100 to 0x8FF. The bcd format encodes one decimal
 * digit in every hex nibble (four bits) of the number. Page numbers
 * containing digits 0xA to 0xF are reserved for various system purposes
 * and not intended for display.
 */

/* Public */

/**
 * @ingroup Service
 * 
 * Teletext or Closed Caption page number. For Teletext pages
 * this is a packed bcd number in range 0x100 ... 0x8FF. Page
 * numbers containing digits 0xA to 0xF are reserved for various
 * system purposes, these pages are not intended for display.
 * 
 * Closed Caption page numbers between 1 ... 8 correspond
 * to the four Caption and Text channels:
 * <table>
 * <tr><td>1</td><td>Caption 1</td><td>
 *   "Primary synchronous caption service [English]"</td></tr>
 * <tr><td>2</td><td>Caption 2</td><td>
 *   "Special non-synchronous data that is intended to
 *   augment information carried in the program"</td></tr>
 * <tr><td>3</td><td>Caption 3</td><td>
 *   "Secondary synchronous caption service, usually
 *    second language [Spanish, French]"</td></tr>
 * <tr><td>4</td><td>Caption 4</td><td>
 *   "Special non-synchronous data similar to Caption 2"</td></tr>
 * <tr><td>5</td><td>Text 1</td><td>
 *   "First text service, data usually not program related"</td></tr>
 * <tr><td>6</td><td>Text 2</td><td>
 *   "Second text service, additional data usually not program related
 *    [ITV data]"</td></tr>
 * <tr><td>7</td><td>Text 3</td><td>
 *   "Additional text channel"</td></tr>
 * <tr><td>8</td><td>Text 4</td><td>
 *   "Additional text channel"</td></tr>
 * </table>
 */
/* XXX unsigned? */
typedef int vbi_pgno;

/**
 * @ingroup Service
 *
 * This is the subpage number only applicable to Teletext pages,
 * a packed bcd number in range 0x00 ... 0x99. On special 'clock' pages
 * (for example listing the current time in different time zones)
 * it can assume values between 0x0000 ... 0x2359 expressing
 * local time. These are not actually subpages.
 */
typedef int vbi_subno;

/**
 * @ingroup Service
 */
#define VBI_ANY_SUBNO 0x3F7F
/**
 * @ingroup Service
 */
#define VBI_NO_SUBNO 0x3F7F

/**
 * @ingroup BCD
 * @param dec Decimal number.
 * 
 * Converts a two's complement binary between 0 ... 999 to a
 * packed bcd number in range  0x000 ... 0x999. Extra digits in
 * the input will be discarded.
 * 
 * @return
 * BCD number.
 */
static_inline unsigned int
vbi_dec2bcd(unsigned int dec)
{
	return (dec % 10) + ((dec / 10) % 10) * 16 + ((dec / 100) % 10) * 256;
}

/**
 * @ingroup BCD
 * @param bcd BCD number.
 * 
 * Converts a packed bcd number between 0x000 ... 0xFFF to a two's
 * complement binary in range 0 ... 999. Extra digits in the input
 * will be discarded.
 * 
 * @return
 * Decimal number. The result is undefined when the bcd number contains
 * hex digits 0xA ... 0xF.
 **/
static_inline unsigned int
vbi_bcd2dec(unsigned int bcd)
{
	return (bcd & 15) + ((bcd >> 4) & 15) * 10 + ((bcd >> 8) & 15) * 100;
}

/**
 * @ingroup BCD
 * @param a BCD number.
 * @param b BCD number.
 * 
 * Adds two packed bcd numbers, returning a packed bcd sum. Arguments
 * and result are in range 0xF000&nbsp;0000 ... 0x0999&nbsp;9999, that
 * is -10**7 ... +10**7 - 1 in decimal notation. To subtract you can
 * add the 10's complement, e. g. -1 = 0xF999&nbsp;9999.
 * 
 * @return
 * Packed bcd number. The result is undefined when any of the arguments
 * contain hex digits 0xA ... 0xF.
 */
static_inline unsigned int
vbi_add_bcd(unsigned int a, unsigned int b)
{
	unsigned int t;

	a += 0x06666666;
	t  = a + b;
	b ^= a ^ t;
	b  = (~b & 0x11111110) >> 3;
	b |= b * 2;

	return t - b;
}

/**
 * @ingroup BCD
 * @param bcd BCD number.
 * 
 * Tests if @a bcd forms a valid BCD number. The argument must be
 * in range 0x0000&nbsp;0000 ... 0x0999&nbsp;9999.
 * 
 * @return
 * @c FALSE if @a bcd contains hex digits 0xA ... 0xF.
 */
static_inline vbi_bool
vbi_is_bcd(unsigned int bcd)
{
	static const unsigned int x = 0x06666666;

	return (((bcd + x) ^ (bcd ^ x)) & 0x11111110) == 0;
}

#endif /* BCD_H */
