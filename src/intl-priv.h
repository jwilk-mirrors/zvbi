/*
 *  libzvbi - gettext stuff
 *
 *  Copyright (C) 2004 Michael H. Schimek
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

/* $Id: intl-priv.h,v 1.1.2.1 2004-03-31 00:41:34 mschimek Exp $ */

#ifndef INTL_PRIV_H
#define INTL_PRIV_H

#include "../config.h"

#include "intl.h"

#ifdef ENABLE_NLS
#  include <libintl.h>
#  include <locale.h>
#  define _(String) dgettext (vbi_intl_domainname, String)
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else /* Stubs that do something close enough.  */
#  define gettext(Msgid) ((const char *) (Msgid))
#  define dgettext(Domainname, Msgid) ((const char *) (Msgid))
#  define dcgettext(Domainname, Msgid, Category) ((const char *) (Msgid))
#  define ngettext(Msgid1, Msgid2, N) \
     ((N) == 1 ? (const char *) (Msgid1) : (const char *) (Msgid2))
#  define dngettext(Domainname, Msgid1, Msgid2, N) \
     ((N) == 1 ? (const char *) (Msgid1) : (const char *) (Msgid2))
#  define dcngettext(Domainname, Msgid1, Msgid2, N, Category) \
     ((N) == 1 ? (const char *) (Msgid1) : (const char *) (Msgid2))
#  define textdomain(Domainname) ((const char *) (Domainname))
#  define bindtextdomain(Domainname, Dirname) ((const char *) (Dirname))
#  define bind_textdomain_codeset(Domainname, Codeset) \
     ((const char *) (Codeset))
#  define _(String) (String)
#  define N_(String) (String)
#endif

#endif /* INTL_PRIV_H */
