/*
 *  libzvbi - Export modules
 *
 *  Copyright (C) 2001, 2002 Michael H. Schimek
 *
 *  Based on code from AleVT 1.5.1
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

/* $Id: export.c,v 1.3 2002-01-15 03:19:53 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <iconv.h>

#include "export.h"
#include "vbi.h" /* vbi_asprintf */

static vbi_bool initialized;
static vbi_export_class *vbi_export_modules;

/**
 * vbi_register_export_module:
 * @new: Static pointer to initialized #vbi_export_class structure.
 * 
 * Registers a new export module. Only export modules call this function.
 **/
void
vbi_register_export_module(vbi_export_class *new)
{
	vbi_export_class **xcp;

#if 0
	fprintf(stderr, "libzvbi:vbi_register_export_module(\"%s\")\n",
	       new->public.keyword);
#endif

	for (xcp = &vbi_export_modules; *xcp; xcp = &(*xcp)->next)
		if (strcmp(new->public.keyword, (*xcp)->public.keyword) < 0)
			break;

	new->next = *xcp;
	*xcp = new;
}

/* AUTOREG not reliable, sigh. */
static void
initialize(void)
{
	extern vbi_export_class vbi_export_class_ppm;
	extern vbi_export_class vbi_export_class_png;
	extern vbi_export_class vbi_export_class_html;
	extern vbi_export_class vbi_export_class_tmpl;
	extern vbi_export_class vbi_export_class_text;
	extern vbi_export_class vbi_export_class_vtx;

	static vbi_export_class *modules[] = {
		&vbi_export_class_ppm,
#ifdef HAVE_LIBPNG
		&vbi_export_class_png,
#endif
		&vbi_export_class_html,
		&vbi_export_class_text,
		&vbi_export_class_vtx,
		NULL,
		&vbi_export_class_tmpl,
	};
	vbi_export_class **xcp;

	if (!vbi_export_modules)
		for (xcp = modules; *xcp; xcp++)
			vbi_register_export_module(*xcp);

	initialized = TRUE;
}

/**
 * vbi_ucs2be:
 * 
 * Helper function for export modules, since iconv seems
 * to be undecided what it really wants.
 * 
 * Return value: 
 * 1 if iconv "UCS-2" is BE on this machine, 0 if LE,
 * -1 if unknown.
 **/
int
vbi_ucs2be(void)
{
	iconv_t cd;
	char c = 'b', *cp = &c;
	char uc[2] = { 'a', 'a' }, *up = uc;
	size_t in = sizeof(c), out = sizeof(uc);
	int endianess = -1;

	if ((cd = iconv_open("UCS2", /* from */ "iso-8859-1")) != (iconv_t) -1) {
		iconv(cd, (void *) &cp, &in, (void *) &up, &out);

		if (uc[0] == 0 && uc[1] == 'b')
			endianess = 1;
		else if (uc[1] == 0 && uc[0] == 'b')
			endianess = 0;

		iconv_close(cd);
	}

	return endianess;
}

/*
 *  This is old stuff, we'll see if it's still needed.
 */

#if 0 

static char *
hexnum(char *buf, unsigned int num)
{
    char *p = buf + 5;

    num &= 0xffff;
    *--p = 0;
    do
    {
	*--p = "0123456789abcdef"[num % 16];
	num /= 16;
    } while (num);
    return p;
}

static char *
adjust(char *p, char *str, char fill, int width, int deq)
{
    int c, l = width - strlen(str);

    while (l-- > 0)
	*p++ = fill;
    while ((c = *str++)) {
	if (deq && strchr(" /?*", c))
    	    c = '_';
	*p++ = c;
    }
    return p;
}

char *
vbi_export_mkname(vbi_export *e, char *fmt,
	int pgno, int subno, char *usr)
{
    char bbuf[1024];
    char *s = bbuf;

    while ((*s = *fmt++))
	if (*s++ == '%')
	{
	    char buf[32], buf2[32];
	    int width = 0;

	    s--;
	    while (*fmt >= '0' && *fmt <= '9')
		width = width*10 + *fmt++ - '0';

	    switch (*fmt++)
	    {
		case '%':
		    s = adjust(s, "%", '%', width, 0);
		    break;
		case 'e':	// extension
		    s = adjust(s, e->mod->extension, '.', width, 1);
		    break;
		case 'n':	// network label
		    s = adjust(s, e->network.label, ' ', width, 1);
		    break;
		case 'p':	// pageno[.subno]
		    if (subno)
			s = adjust(s,strcat(strcat(hexnum(buf, pgno),
				"-"), hexnum(buf2, subno)), ' ', width, 0);
		    else
			s = adjust(s, hexnum(buf, pgno), ' ', width, 0);
		    break;
		case 'S':	// subno
		    s = adjust(s, hexnum(buf, subno), '0', width, 0);
		    break;
		case 'P':	// pgno
		    s = adjust(s, hexnum(buf, pgno), '0', width, 0);
		    break;
		case 's':	// user strin
		    s = adjust(s, usr, ' ', width, 0);
		    break;
		//TODO: add date, ...
	    }
	}
    s = strdup(bbuf);
    if (! s)
	vbi_export_error_printf(e, "out of memory");
    return s;
}

#endif /* OLD STUFF */

static vbi_option_info
generic_options[] = {
	VBI_OPTION_STRING_INITIALIZER
	  ("creator", NULL, PACKAGE " " VERSION, NULL),
	VBI_OPTION_STRING_INITIALIZER
	  ("network", NULL, "", NULL),
	VBI_OPTION_BOOL_INITIALIZER
	  ("reveal", NULL, FALSE, NULL)
};

#define GENERIC (sizeof(generic_options) / sizeof(generic_options[0]))

static void
reset_error(vbi_export *e)
{
	if (e->errstr) {
		free(e->errstr);
		e->errstr = NULL;
	}
}

/**
 * vbi_export_info_enum:
 * @index: Index into the export module list, 0 ... n.
 *
 * Enumerates all available export modules. You should start at index 0,
 * incrementing.
 *
 * Some modules may depend on machine features or the presence of certain
 * libraries, thus the list can vary from session to session.
 *
 * Return value:
 * Static pointer to a #vbi_export_info structure (no need to be freed),
 * %NULL if the index is out of bounds.
 **/
vbi_export_info *
vbi_export_info_enum(int index)
{
	vbi_export_class *xc;

	if (!initialized)
		initialize();

	for (xc = vbi_export_modules; xc && index > 0; xc = xc->next, index--);

	return xc ? &xc->public : NULL;
}

/**
 * vbi_export_info_keyword:
 * @keyword: Export module identifier as in #vbi_export_info and
 *           vbi_export_new().
 * 
 * Similar to vbi_export_info_enum(), but this function attempts to find an
 * export module by keyword.
 * 
 * Return value:
 * Static pointer to a #vbi_export_info structure, %NULL if the named export
 * module has not been found.
 **/
vbi_export_info *
vbi_export_info_keyword(const char *keyword)
{
	vbi_export_class *xc;

	if (!keyword)
		return NULL;

	if (!initialized)
		initialize();

	for (xc = vbi_export_modules; xc; xc = xc->next)
		if (strcmp(keyword, xc->public.keyword) == 0)
			return &xc->public;

	return NULL;
}

/**
 * vbi_export_info_export:
 * @export: Pointer to a #vbi_export object previously allocated with
 *   vbi_export_new().
 *
 * Returns the export module info for the given @export object.
 *
 * Return value: A static #vbi_export_info pointer or %NULL if @export
 * is %NULL.
 **/
vbi_export_info *
vbi_export_info_export(vbi_export *export)
{
	if (!export)
		return NULL;

	return &export->class->public;
}

static void
reset_options(vbi_export *e)
{
	vbi_option_info *oi;
	int i;

	for (i = 0; (oi = vbi_export_option_info_enum(e, i)); i++)
		switch (oi->type) {
		case VBI_OPTION_BOOL:
		case VBI_OPTION_INT:
			if (oi->menu.num)
				vbi_export_option_set(e, oi->keyword, oi->menu.num[oi->def.num]);
			else
				vbi_export_option_set(e, oi->keyword, oi->def.num);
			break;

		case VBI_OPTION_REAL:
			if (oi->menu.dbl)
				vbi_export_option_set(e, oi->keyword, oi->menu.dbl[oi->def.num]);
			else
				vbi_export_option_set(e, oi->keyword, oi->def.dbl);
			break;

		case VBI_OPTION_STRING:
			if (oi->menu.str)
				vbi_export_option_set(e, oi->keyword, oi->menu.str[oi->def.num]);
			else
				vbi_export_option_set(e, oi->keyword, oi->def.str);
			break;

		case VBI_OPTION_MENU:
			vbi_export_option_set(e, oi->keyword, oi->def.num);
			break;

		default:
			fprintf(stderr, __PRETTY_FUNCTION__
				": unknown export option type %d\n", oi->type);
			exit(EXIT_FAILURE);
		}
}

static vbi_bool
option_string(vbi_export *e, const char *s2)
{
	vbi_option_info *oi;
	char *s, *s1, *keyword, *string, quote;
	vbi_bool r;

	if (!(s = s1 = vbi_export_strdup(e, NULL, s2)))
		return FALSE;

	do {
		while (isspace(*s))
			s++;

		if (*s == ',' || *s == ';') {
			s++;
			continue;
		}

		if (!*s) {
			free(s1);
			return TRUE;
		}

		keyword = s;

		while (isalnum(*s) || *s == '-' || *s == '_')
			s++;

		if (!*s)
			goto invalid;

		*s++ = 0;

		while (isspace(*s) || *s == '=')
			s++;

		if (!*s) {
 invalid:
			vbi_export_error_printf(e, _("Invalid option string \"%s\"."), s2);
			break;
		}

		if (!(oi = vbi_export_option_info_keyword(e, keyword)))
			break;

		switch (oi->type) {
		case VBI_OPTION_BOOL:
		case VBI_OPTION_INT:
		case VBI_OPTION_MENU:
			r = vbi_export_option_set(e, keyword, (int) strtol(s, &s, 0));
			break;

		case VBI_OPTION_REAL:
			r = vbi_export_option_set(e, keyword, (double) strtod(s, &s));
			break;

		case VBI_OPTION_STRING:
			quote = 0;
			if (*s == '\'' || *s == '\"')
				quote = *s++;
			string = s;

			while (*s && *s != quote
			       && (quote || (*s != ',' && *s != ';')))
				s++;
			if (*s)
				*s++ = 0;

			r = vbi_export_option_set(e, keyword, string);
			break;

		default:
			fprintf(stderr, __PRETTY_FUNCTION__
				": unknown export option type %d\n", oi->type);
			exit(EXIT_FAILURE);
		}

	} while (r);

	free(s1);

	return FALSE;
}

/**
 * vbi_export_new:
 * @keyword: Export module identifier as in vbi_export_info.
 * @errstr: If not %NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * 
 * Creates a new export module instance to export a #vbi_page in
 * the respective module format. As a special service you can
 * initialize options by appending to the @keyword like this:
 * 
 * <informalexample><programlisting>
 * vbi_export_new("keyword; quality=75.5, comment=\"example\"");  
 * </programlisting></informalexample>
 * 
 * Return value:
 * Pointer to a newly allocated #vbi_export object which must be
 * freed by calling vbi_export_delete(). %NULL is returned and
 * the @errstr may be set (or %NULL) if some problem occurred.
 **/
vbi_export *
vbi_export_new(const char *keyword, char **errstr)
{
	unsigned char key[256];
	vbi_export_class *xc;
	vbi_export *e;
	int keylen;

	if (!initialized)
		initialize();

	if (!keyword)
		keyword = "";

	for (keylen = 0; keyword[keylen] && keylen < (sizeof(key) - 1)
		     && keyword[keylen] != ';' && keyword[keylen] != ','; keylen++)
		     key[keylen] = keyword[keylen];
	key[keylen] = 0;

	for (xc = vbi_export_modules; xc; xc = xc->next)
		if (strcmp(key, xc->public.keyword) == 0)
			break;

	if (!xc) {
		vbi_asprintf(errstr, _("Unknown export module '%s'."), key);
		return NULL;
	}

	if (!xc->new)
		e = calloc(1, sizeof(*e));
	else
		e = xc->new();

	if (!e) {
		vbi_asprintf(errstr, _("Cannot initialize export module '%s', "
				       "probably lack of memory."),
			     xc->public.label ? xc->public.label : keyword);
		return NULL;
	}

	e->class = xc;
	e->errstr = NULL;

	e->name = NULL;

	reset_options(e);

	if (keyword[keylen] && !option_string(e, keyword + keylen + 1)) {
		errstr && (*errstr = strdup(vbi_export_errstr(e)));
		vbi_export_delete(e);
		return NULL;
	}

	errstr && (errstr = NULL);

	return e;
}

/**
 * vbi_export_delete:
 * @export: Pointer to a #vbi_export object previously allocated with
 *	     vbi_export_new(). Can be %NULL.
 * 
 * This function frees all resources associated with the #vbi_export
 * object.
 **/
void
vbi_export_delete(vbi_export *export)
{
	vbi_export_class *xc;

	if (!export)
		return;

	if (export->errstr)
		free(export->errstr);

	if (export->network)
		free(export->network);
	if (export->creator)
		free(export->creator);

	xc = export->class;

	if (xc->new && xc->delete)
		xc->delete(export);
	else
		free(export);
}

/**
 * vbi_export_option_info_enum:
 * @export: Pointer to a initialized #vbi_export object.
 * @index: Index in the option table 0 ... n.
 *
 * Enumerates the options available for the given export module. You
 * should start at index 0, incrementing.
 *
 * Return value: Static pointer to a #vbi_option_info structure,
 * %NULL if @index is out of bounds.
 **/
vbi_option_info *
vbi_export_option_info_enum(vbi_export *export, int index)
{
	vbi_export_class *xc;

	if (!export)
		return NULL;

	reset_error(export);

	if (index < GENERIC)
		return generic_options + index;

	xc = export->class;

	if (xc->option_enum)
		return xc->option_enum(export, index - GENERIC);
	else
		return NULL;
}

/**
 * vbi_export_option_info_keyword:
 * @export: Pointer to a initialized #vbi_export object.
 * @keyword: Keyword of the option as in #vbi_option_info.
 *
 * Similar to vbi_export_option_info_enum(), but tries to find the
 * option info based on the given keyword.
 *
 * Return value: Static pointer to a #vbi_option_info structure,
 * %NULL if the keyword wasn't found.
 **/
vbi_option_info *
vbi_export_option_info_keyword(vbi_export *export, const char *keyword)
{
	vbi_export_class *xc;
	vbi_option_info *oi;
	int i;

	if (!export || !keyword)
		return NULL;

	reset_error(export);

	for (i = 0; i < GENERIC; i++)
		if (strcmp(keyword, generic_options[i].keyword) == 0)
			return generic_options + i;

	xc = export->class;

	if (!xc->option_enum)
		return NULL;

	for (i = 0; (oi = xc->option_enum(export, i)); i++)
		if (strcmp(keyword, oi->keyword) == 0)
			return oi;

	vbi_export_unknown_option(export, keyword);

	return NULL;
}

/**
 * vbi_export_option_get:
 * @export: Pointer to a initialized #vbi_export object.
 * @keyword: Keyword identifying the option, as in #vbi_option_info.
 * @value: A place to store the current option value.
 *
 * This function queries the current value of the named option.
 * When the option is of type #VBI_OPTION_STRING @value.str must be
 * freed with free() when you don't need it any longer. When the
 * option is of type #VBI_OPTION_MENU then @value.num contains the
 * selected entry.
 *
 * Return value:
 * %TRUE on success, otherwise @value remained unchanged.
 **/
vbi_bool
vbi_export_option_get(vbi_export *export, const char *keyword,
		      vbi_option_value *value)
{
	vbi_export_class *xc;
	vbi_bool r = TRUE;

	if (!export || !keyword || !value)
		return FALSE;

	reset_error(export);

	if (strcmp(keyword, "reveal") == 0) {
		value->num = export->reveal;
	} else if (strcmp(keyword, "network") == 0) {
		if (!(value->str = vbi_export_strdup(export, NULL,
						     export->network ? : "")))
			return FALSE;
	} else if (strcmp(keyword, "creator") == 0) {
		if (!(value->str = vbi_export_strdup(export, NULL, export->creator)))
			return FALSE;
	} else {
		xc = export->class;

		if (xc->option_get)
			r = xc->option_get(export, keyword, value);
		else
			r = FALSE;
	}

	return r;
}

/**
 * vbi_export_option_set:
 * @export: Pointer to a initialized #vbi_export object.
 * @keyword: Keyword identifying the option, as in #vbi_option_info.
 * @Varargs: New value to set.
 *
 * Sets the value of the named option. Make sure the value is casted
 * to the correct type (int, double, char *). Typical usage may be:
 *
 * <informalexample><programlisting>
 * vbi_export_option_set(export, "quality", 75.5);
 * </programlisting></informalexample>
 *
 * Mind that options of type #VBI_OPTION_MENU must be set by menu
 * entry (int), all other options by value. If necessary this will
 * be replaced by the closest value possible. Use function
 * vbi_export_option_menu_set() to set all menu options by menu entry.  
 *
 * Return value:
 * %TRUE on success, otherwise the option is not changed.
 **/
vbi_bool
vbi_export_option_set(vbi_export *export, const char *keyword, ...)
{
	vbi_export_class *xc;
	vbi_bool r = TRUE;
	va_list args;

	if (!export || !keyword)
		return FALSE;

	reset_error(export);

	va_start(args, keyword);

	if (strcmp(keyword, "reveal") == 0) {
		export->reveal = !!va_arg(args, int);
	} else if (strcmp(keyword, "network") == 0) {
		char *network = va_arg(args, char *);
		if (!network || !network[0]) {
			if (export->network) {
				free(export->network);
				export->network = NULL;
			}
		} else if (!vbi_export_strdup(export, &export->network, network)) {
			return FALSE;
		}
	} else if (strcmp(keyword, "creator") == 0) {
		char *creator = va_arg(args, char *);
		if (!vbi_export_strdup(export, &export->creator, creator))
			return FALSE;
	} else {
		xc = export->class;

		if (xc->option_set)
			r = xc->option_set(export, keyword, args);
		else
			r = FALSE;
	}

	va_end(args);

	return r;
}

/**
 * vbi_export_option_menu_get:
 * @export: Pointer to a initialized #vbi_export object.
 * @keyword: Keyword identifying the option, as in #vbi_option_info.
 * @entry: A place to store the current menu entry.
 * 
 * Similar to vbi_export_option_get() this function queries the current
 * value of the named option, but returns this value as number of the
 * corresponding menu entry. Naturally this must be an option with
 * menu.
 * 
 * Return value: 
 * %TRUE on success, otherwise @value remained unchanged.
 **/
vbi_bool
vbi_export_option_menu_get(vbi_export *export, const char *keyword,
			   int *entry)
{
	vbi_option_info *oi;
	vbi_option_value val;
	vbi_bool r;
	int i;

	if (!export || !keyword || !entry)
		return FALSE;

	reset_error(export);

	if (!(oi = vbi_export_option_info_keyword(export, keyword)))
		return FALSE;

	if (!vbi_export_option_get(export, keyword, &val))
		return FALSE;

	r = FALSE;

	for (i = 0; i <= oi->max.num; i++) {
		switch (oi->type) {
		case VBI_OPTION_BOOL:
		case VBI_OPTION_INT:
			if (!oi->menu.num)
				return FALSE;
			r = (oi->menu.num[i] == val.num);
			break;

		case VBI_OPTION_REAL:
			if (!oi->menu.dbl)
				return FALSE;
			r = (oi->menu.dbl[i] == val.dbl);
			break;

		case VBI_OPTION_MENU:
			r = (i == val.num);
			break;

		default:
			fprintf(stderr, __PRETTY_FUNCTION__
				": unknown export option type %d\n", oi->type);
			exit(EXIT_FAILURE);
		}

		if (r) {
			*entry = i;
			break;
		}
	}

	return r;
}

/**
 * vbi_export_option_menu_set:
 * @export: Pointer to a initialized #vbi_export object.
 * @keyword: Keyword identifying the option, as in #vbi_option_info.
 * @entry: Menu entry to be selected.
 * 
 * Similar to vbi_export_option_set() this function sets the value of
 * the named option, however it does so by number of the corresponding
 * menu entry. Naturally this must be an option with menu.
 *
 * Return value: 
 * %TRUE on success, otherwise the option is not changed.
 **/
vbi_bool
vbi_export_option_menu_set(vbi_export *export, const char *keyword,
			   int entry)
{
	vbi_option_info *oi;

	if (!export || !keyword || entry < 0)
		return FALSE;

	reset_error(export);

	if (!(oi = vbi_export_option_info_keyword(export, keyword)))
		return FALSE;

	if (entry < 0 || entry > oi->max.num)
		return FALSE;

	switch (oi->type) {
	case VBI_OPTION_BOOL:
	case VBI_OPTION_INT:
		if (!oi->menu.num)
			return FALSE;
		return vbi_export_option_set(export, keyword, oi->menu.num[entry]);

	case VBI_OPTION_REAL:
		if (!oi->menu.dbl)
			return FALSE;
		return vbi_export_option_set(export, keyword, oi->menu.dbl[entry]);

	case VBI_OPTION_MENU:
		return vbi_export_option_set(export, keyword, entry);

	default:
		fprintf(stderr, __PRETTY_FUNCTION__
			": unknown export option type %d\n", oi->type);
		exit(EXIT_FAILURE);
	}
}

/**
 * vbi_export_stdio:
 * @export: Pointer to a initialized #vbi_export object.
 * @fp: Buffered i/o stream to write to.
 * @pg: Page to be exported.
 * 
 * This function writes the @pg contents, converted to the respective
 * export module format, to the stream @fp. You are responsible for
 * opening and closing the stream, don't forget to check for i/o
 * errors after closing. Note this function may write incomplete files
 * when an error occurs.
 *
 * You can call this function as many times as you want, it does not
 * change #vbi_export state or the #vbi_page.
 * 
 * Return value: 
 * %TRUE on success.
 **/
vbi_bool
vbi_export_stdio(vbi_export *export, FILE *fp, vbi_page *pg)
{
	vbi_bool success;

	if (!export || !fp || !pg)
		return FALSE;

	reset_error(export);
	clearerr(fp);

	success = export->class->export(export, fp, pg);

	if (success && ferror(fp)) {
		vbi_export_write_error(export);
		success = FALSE;
	}

	return success;
}

/**
 * vbi_export_file:
 * @export: Pointer to a initialized #vbi_export object.
 * @name: File to be created.
 * @pg: Page to be exported.
 * 
 * This function writes the @pg contents, converted to the respective
 * export module format, into a newly created file (as with fopen()) of
 * the given @name. When an error occured the incomplete file will be
 * deleted.
 * 
 * You can call this function as many times as you want, it does not
 * change #vbi_export state or the #vbi_page.
 * 
 * Return value: 
 * %TRUE on success.
 **/
vbi_bool
vbi_export_file(vbi_export *export, const char *name,
		vbi_page *pg)
{
	struct stat st;
	vbi_bool success;
	FILE *fp;

	if (!export || !name || !pg)
		return FALSE;

	reset_error(export);

	if (!(fp = fopen(name, "w"))) {
		vbi_export_error_printf(export, _("Cannot create file '%s': Error %d, %s."),
					name, errno, strerror(errno));
		return FALSE;
	}

	export->name = (char *) name;

	success = export->class->export(export, fp, pg);

	if (success && ferror(fp)) {
		vbi_export_write_error(export);
		success = FALSE;
	}

	if (fclose(fp))
		if (success) {
			vbi_export_write_error(export);
			success = FALSE;
		}

	if (!success && stat(name, &st) == 0 && S_ISREG(st.st_mode))
		remove(name);

	export->name = NULL;

	return success;
}

/**
 * vbi_export_error_printf:
 * @export: Pointer to a initialized #vbi_export object.
 * @templ: See printf().
 * @Varargs: See printf(). 
 * 
 * Store an error description in the @export object. Including the current
 * error description (to append or prepend) is safe. Only export modules
 * call this function.
 **/
void
vbi_export_error_printf(vbi_export *export, const char *templ, ...)
{
	char buf[512];
	va_list ap;

	if (!export)
		return;

	va_start(ap, templ);
	vsnprintf(buf, sizeof(buf) - 1, templ, ap);
	va_end(ap);

	reset_error(export);

	export->errstr = strdup(buf);
}

/**
 * vbi_export_write_error:
 * @export: Pointer to a initialized #vbi_export object.
 * 
 * Similar to vbi_export_error_printf this function stores an error
 * description in the @export object, after examining the errno
 * variable and choosing an appropriate message. Only export
 * modules call this function.
 **/
void
vbi_export_write_error(vbi_export *export)
{
	char *t, buf[256];

	if (!export)
		return;

	if (export->name)
		snprintf(t = buf, sizeof(buf),
			_("Error while writing file '%s'"), export->name);
	else
 		t = _("Error while writing file");

	if (errno) {
		vbi_export_error_printf(export, "%s: Error %d, %s", t,
					errno, strerror(errno));
	} else {
		vbi_export_error_printf(export, "%s.", t);
	}
}

void
vbi_export_unknown_option(vbi_export *e, const char *keyword)
{
	vbi_export_error_printf(e, _("Export module '%s' has no option '%s'."),
				e->class->public.label ?
				_(e->class->public.label) : e->class->public.keyword,
				keyword);
}

void
vbi_export_invalid_option(vbi_export *e, const char *keyword, ...)
{
	vbi_export_error_printf(e, _("Invalid argument for option '%s' of export module '%s'."),
				keyword, e->class->public.label ?
				_(e->class->public.label) : e->class->public.keyword);
}

char *
vbi_export_strdup(vbi_export *e, char **d, const char *s)
{
	char *new = strdup(s ? s : "");

	if (!new) {
		vbi_export_error_printf(e, _("Out of memory in export module '%s'."),
					e->class->public.label ?
					_(e->class->public.label)
					: e->class->public.keyword);
		errno = ENOMEM;
		return NULL;
	}

	if (d) {
		if (*d)
			free(*d);
		*d = new;
	}

	return new;
}

/**
 * vbi_export_errstr:
 * @export: Pointer to a initialized #vbi_export object.
 * 
 * Return value: 
 * After an export function failed, this function returns a pointer
 * to a more detailed error description. Do not free this string. It
 * remains valid until the next call of an export function.
 **/
char *
vbi_export_errstr(vbi_export *export)
{
	if (!export || !export->errstr)
		return _("Unknown error.");

	return export->errstr;
}
