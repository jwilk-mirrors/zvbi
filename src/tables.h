/*
 *  libzvbi - Tables
 *
 *  Copyright (C) 1999, 2000, 2001, 2002 Michael H. Schimek
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

/* $Id: tables.h,v 1.3.2.2 2004-02-13 02:11:20 mschimek Exp $ */

#ifndef TABLES_H
#define TABLES_H

#include <inttypes.h>

#include "event.h" /* vbi_rating_auth, vbi_prog_classf */

/* Public */

/**
 * @addtogroup Event
 * @{
 */
extern const char *	vbi_rating_string(vbi_rating_auth auth, int id);
extern const char *	vbi_prog_type_string(vbi_prog_classf classf, int id);
/** @} */

/* Private */

#endif /* TABLES_H */
