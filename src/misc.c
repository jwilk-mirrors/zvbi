/*
 *  Copyright (C) 2001-2004 Michael H. Schimek
 *  Copyright (C) 2000-2003 I�aki Garc�a Etxebarria
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

/* $Id: misc.c,v 1.4.2.4 2006-05-14 14:14:11 mschimek Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "misc.h"

vbi3_log_fn *		vbi3_global_log_fn;
void *			vbi3_global_log_user_data;
vbi3_log_mask		vbi3_global_log_mask;

/**
 * @internal
 * strlcpy() is a BSD/GNU extension.
 */
size_t
_vbi3_strlcpy			(char *			dst,
				 const char *		src,
				 size_t			len)
{
	char *dst1;
	char *end;
	char c;

	assert (NULL != dst);
	assert (NULL != src);
	assert (len > 0);

	dst1 = dst;

	end = dst + len - 1;

	while (dst < end && (c = *src++))
		*dst++ = c;

	*dst = 0;

	return dst - dst1;
}

/**
 * @internal
 * strndup() is a BSD/GNU extension.
 */
char *
_vbi3_strndup			(const char *		s,
				 size_t			len)
{
	size_t n;
	char *r;

	if (NULL == s)
		return NULL;

	n = strlen (s);
	len = MIN (len, n);

	r = malloc (len + 1);

	if (r) {
		memcpy (r, s, len);
		r[len] = 0;
	}

	return r;
}

/**
 * @internal
 * vasprintf() is a GNU extension.
 */
int
_vbi3_vasprintf			(char **		dstp,
				 const char *		templ,
				 va_list		ap)
{
	char *buf;
	unsigned long size;
	int temp;

	assert (NULL != dstp);
	assert (NULL != templ);

	temp = errno;

	buf = NULL;
	size = 64;

	for (;;) {

		char *buf2;
		long len;

		if (!(buf2 = realloc (buf, size)))
			break;

		buf = buf2;

		len = vsnprintf (buf, size, templ, ap);

		if (len < 0) {
			/* Not enough. */
			size *= 2;
		} else if ((unsigned long) len < size) {
			*dstp = buf;
			errno = temp;
			return len;
		} else {
			/* Size needed. */
			size = len + 1;
		}
	}

	free (buf);
	*dstp = NULL;
	errno = temp;

	return -1;
}

/**
 * @internal
 * asprintf() is a GNU extension.
 */
int
_vbi3_asprintf			(char **		dstp,
				 const char *		templ,
				 ...)
{
	va_list ap;
	int len;

	va_start (ap, templ);

	len = vasprintf (dstp, templ, ap);

	va_end (ap);

	return len;
}

vbi3_bool
_vbi3_keyword_lookup		(int *			value,
				 const char **		inout_s,
				 const _vbi3_key_value_pair *table,
				 unsigned int		n_pairs)
{
	const char *s;
	unsigned int i;

	assert (NULL != value);
	assert (NULL != inout_s);
	assert (NULL != *inout_s);
	assert (NULL != table);

	s = *inout_s;

	while (isspace (*s))
		++s;

	if (isdigit (*s)) {
		long val;
		char *end;

		val = strtol (s, &end, 10);

		for (i = 0; NULL != table[i].key; ++i) {
			if (val == table[i].value) {
				*value = val;
				*inout_s = end;
				return TRUE;
			}
		}
	} else {
		for (i = 0; i < n_pairs; ++i) {
			size_t len = strlen (table[i].key);

			if (0 == strncasecmp (s, table[i].key, len)
			    && !isalnum (s[len])) {
				*value = table[i].value;
				*inout_s = s + len;
				return TRUE;
			}
		}
	}

	return FALSE;
}

void
vbi3_log_on_stderr		(vbi3_log_mask		level,
				 const char *		context,
				 const char *		message,
				 void *			user_data)
{
	vbi3_log_mask max_level;

	if (0 == strncmp (context, "vbi_", 4)) {
		context += 4;
	} else if (0 == strncmp (context, "vbi3_", 5)) {
		context += 5;
	}

	if (NULL != user_data) {
		max_level = * (vbi3_log_mask *) user_data;
		if (level > max_level)
			return;
	}

	fprintf (stderr, "libzvbi:%s: %s\n", context, message);
}

/** @internal */
void
vbi3_log_vprintf		(vbi3_log_fn		log_fn,
				 void *			user_data,
				 vbi3_log_mask		mask,
				 const char *		context,
				 const char *		templ,
				 va_list		ap)
{
	int saved_errno;
	char *buffer;

	assert (NULL != templ);

	if (NULL == log_fn)
		return;

	saved_errno = errno;

	vasprintf (&buffer, templ, ap);
	if (NULL != buffer) {
		log_fn (mask, context, buffer, user_data);

		free (buffer);
		buffer = NULL;
	}

	errno = saved_errno;
}

/** @internal */
void
vbi3_log_printf			(vbi3_log_fn		log_fn,
				 void *			user_data,
				 vbi3_log_mask		mask,
				 const char *		context,
				 const char *		templ,
				 ...)
{
	va_list ap;

	va_start (ap, templ);

	vbi3_log_vprintf (log_fn, user_data, mask, context, templ, ap);

	va_end (ap);
}
