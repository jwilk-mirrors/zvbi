/*
 *  libzvbi - Device interfaces
 *
 *  Copyright (C) 2002, 2004 Michael H. Schimek
 *  Copyright (C) 2003-2004 Tom Zoerner
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

/* $Id: io.c,v 1.10 2004-06-18 14:13:08 mschimek Exp $ */

#include <assert.h>
#include <errno.h>
#include <unistd.h>		/* close() */
#include <sys/ioctl.h>		/* ioctl() */

#include "io.h"

/* Preliminary hack for tests. */
vbi_bool vbi_capture_force_read_mode = FALSE;

/**
 * @addtogroup Device Device interface
 * @ingroup Raw
 * @brief Access to VBI capture devices.
 */

/**
 * @param capture Initialized vbi_capture context.
 * @param data Store the raw vbi data here. Use vbi_capture_parameters() to
 *   determine the buffer size.
 * @param timestamp On success the capture instant in seconds and fractions
 *   since 1970-01-01 00:00 of the video frame will be stored here.
 * @param timeout Wait timeout, will be read only.
 * 
 * Read a raw vbi frame from the capture device.
 * 
 * @return
 * -1 on error, examine @c errno for details. The function also fails if
 * vbi data is not available in raw format. 0 on timeout, 1 on success.
 */
int
vbi_capture_read_raw(vbi_capture *capture, void *data,
		     double *timestamp, struct timeval *timeout)
{
	vbi_capture_buffer buffer, *bp = &buffer;
	int r;

	assert (capture != NULL);
	assert (timestamp != NULL);
	assert (timeout != NULL);

	buffer.data = data;

	if ((r = capture->read(capture, &bp, NULL, timeout)) > 0)
		*timestamp = buffer.timestamp;

	return r;
}

/**
 * @param capture Initialized vbi capture context.
 * @param data Stores the sliced vbi data here. Use vbi_capture_parameters() to
 *   determine the buffer size.
 * @param lines Stores number of vbi lines decoded and stored in @a data,
 *   which can be zero, here.
 * @param timestamp On success the capture instant in seconds and fractions
 *   since 1970-01-01 00:00 will be stored here.
 * @param timeout Wait timeout, will be read only.
 * 
 * Read a sliced vbi frame, that is an array of vbi_sliced structures,
 * from the capture device. 
 * 
 * @return
 * -1 on error, examine @c errno for details. 0 on timeout, 1 on success.
 */
int
vbi_capture_read_sliced(vbi_capture *capture, vbi_sliced *data, int *lines,
			double *timestamp, struct timeval *timeout)
{
	vbi_capture_buffer buffer, *bp = &buffer;
	int r;

	assert (capture != NULL);
	assert (lines != NULL);
	assert (timestamp != NULL);
	assert (timeout != NULL);

	buffer.data = data;

	if ((r = capture->read(capture, NULL, &bp, timeout)) > 0) {
		*lines = ((unsigned int) buffer.size) / sizeof(vbi_sliced);
		*timestamp = buffer.timestamp;
	}

	return r;
}

/**
 * @param capture Initialized vbi capture context.
 * @param raw_data Stores the raw vbi data here. Use vbi_capture_parameters() to
 *   determine the buffer size.
 * @param sliced_data Stores the sliced vbi data here. Use vbi_capture_parameters() to
 *   determine the buffer size.
 * @param lines Stores number of vbi lines decoded and stored in @a data,
 *   which can be zero, here.
 * @param timestamp On success the capture instant in seconds and fractions
 *   since 1970-01-01 00:00 will be stored here.
 * @param timeout Wait timeout, will be read only.
 * 
 * Read a raw vbi frame from the capture device, decode to sliced data
 * and also read the sliced vbi frame, that is an array of vbi_sliced
 * structures, from the capture device.
 * 
 * @return
 * -1 on error, examine @c errno for details. The function also fails if vbi data
 * is not available in raw format. 0 on timeout, 1 on success.
 */
int
vbi_capture_read(vbi_capture *capture, void *raw_data,
		 vbi_sliced *sliced_data, int *lines,
		 double *timestamp, struct timeval *timeout)
{
	vbi_capture_buffer rbuffer, *rbp = &rbuffer;
	vbi_capture_buffer sbuffer, *sbp = &sbuffer;
	int r;

	assert (capture != NULL);
	assert (lines != NULL);
	assert (timestamp != NULL);
	assert (timeout != NULL);

	rbuffer.data = raw_data;
	sbuffer.data = sliced_data;

	if ((r = capture->read(capture, &rbp, &sbp, timeout)) > 0) {
		*lines = ((unsigned int) sbuffer.size) / sizeof(vbi_sliced);
		*timestamp = sbuffer.timestamp;
	}

	return r;
}

/**
 * @param capture Initialized vbi capture context.
 * @param buffer Store pointer to a vbi_capture_buffer here.
 * @param timeout Wait timeout, will be read only.
 * 
 * Read a raw vbi frame from the capture device, returning a
 * pointer to the image in @a buffer->data, which has @a buffer->size.
 * The data remains valid until the next
 * vbi_capture_pull_raw() call and must be read only.
 * 
 * @return
 * -1 on error, examine @c errno for details. The function also fails
 * if vbi data is not available in raw format. 0 on timeout, 1 on success.
 */
int
vbi_capture_pull_raw(vbi_capture *capture, vbi_capture_buffer **buffer,
		     struct timeval *timeout)
{
	assert (capture != NULL);
	assert (buffer != NULL);
	assert (timeout != NULL);

	*buffer = NULL;

	return capture->read(capture, buffer, NULL, timeout);
}

/**
 * @param capture Initialized vbi capture context.
 * @param buffer Store pointer to a vbi_capture_buffer here.
 * @param timeout Wait timeout, will be read only.
 * 
 * Read a sliced vbi frame, that is an array of vbi_sliced,
 * from the capture device, returning a pointer to the array as @a buffer->data.
 * @a buffer->size is the size of the array, that is the number of lines decoded,
 * which can be zero, <u>times the size of structure vbi_sliced</u>. The data
 * remains valid until the next vbi_capture_pull_sliced() call and must be read only.
 * 
 * @return
 * -1 on error, examine @c errno for details. 0 on timeout, 1 on success.
 */
int
vbi_capture_pull_sliced(vbi_capture *capture, vbi_capture_buffer **buffer,
			struct timeval *timeout)
{
	assert (capture != NULL);
	assert (buffer != NULL);
	assert (timeout != NULL);

	*buffer = NULL;

	return capture->read(capture, NULL, buffer, timeout);
}

/**
 * @param capture Initialized vbi capture context.
 * @param raw_buffer Store pointer to a vbi_capture_buffer here.
 * @param sliced_buffer Store pointer to a vbi_capture_buffer here.
 * @param timeout Wait timeout, will be read only.
 * 
 * Read a raw vbi frame from the capture device and decode to sliced
 * data. Both raw and sliced data is returned, a pointer to the raw image
 * as raw_buffer->data and a pointer to an array of vbi_sliced as
 * sliced_buffer->data. Note sliced_buffer->size is the size of the array
 * in bytes. That is the number of lines decoded, which can be zero,
 * times the size of the vbi_sliced structure.
 *
 * The raw and sliced data remains valid
 * until the next vbi_capture_pull_raw() call and must be read only.
 * 
 * @return
 * -1 on error, examine @c errno for details. The function also fails if vbi data
 * is not available in raw format. 0 on timeout, 1 on success.
 */
int
vbi_capture_pull(vbi_capture *capture, vbi_capture_buffer **raw_buffer,
		 vbi_capture_buffer **sliced_buffer, struct timeval *timeout)
{
	assert (capture != NULL);
	assert (timeout != NULL);

	if (raw_buffer)
		*raw_buffer = NULL;
	if (sliced_buffer)
		*sliced_buffer = NULL;

	return capture->read(capture, raw_buffer, sliced_buffer, timeout);
}

/**
 * @param capture Initialized vbi capture context.
 * 
 * Describe the captured data. Raw vbi frames consist of
 * vbi_raw_decoder.count[0] + vbi_raw_decoder.count[1] lines in
 * vbi_raw_decoder.sampling_format, each vbi_raw_decoder.bytes_per_line.
 * Sliced vbi arrays consist of zero to
 * vbi_raw_decoder.count[0] + vbi_raw_decoder.count[1] vbi_sliced
 * structures.
 * 
 * @return
 * Pointer to a vbi_raw_decoder structure, read only.
 **/
vbi_raw_decoder *
vbi_capture_parameters(vbi_capture *capture)
{
	assert (capture != NULL);

	return capture->parameters(capture);
}

/**
 * @param capture Initialized vbi capture context, can be @c NULL.
 * 
 * @return
 * The file descriptor used to read from the device. If not
 * applicable or the @a capture context is invalid -1
 * will be returned.
 */
int
vbi_capture_fd(vbi_capture *capture)
{
	if (capture)
		return capture->get_fd(capture);
	else
		return -1;
}

/**
 * @internal
 */
void
vbi_capture_set_log_fp		(vbi_capture *		capture,
				 FILE *			fp)
{
	assert (NULL != capture);

	capture->sys_log_fp = fp;
}

/**
 * @param capture Initialized vbi capture context, can be @c NULL.
 * 
 * Free all resources associated with the @a capture context.
 */
void
vbi_capture_delete(vbi_capture *capture)
{
	if (capture)
		capture->_delete(capture);
}


/**
 * @internal
 * @brief Substract time spent waiting in select from a given max.
 *   timeout struct.
 *
 * @param tv_start Actual time before select() was called.
 * @param timeout Timeout value given to select, will be reduced by the
 *   difference since start time.
 *
 * This functions is intended for functions which call select() repeatedly
 * with a given overall timeout.  After each select() call the time already
 * spent in waiting has to be substracted from the timeout. (Note that we
 * don't use the Linux select(2) feature to return the time not slept in
 * the timeout struct, because that's not portable.)
 * 
 * @return
 * Modifes @a timeout value.
 */
/* XXX should we use Linux feature if available? */
void
vbi_capture_io_update_timeout	(const struct timeval *	tv_start,
				 struct timeval *	timeout)
{
	struct timeval delta;
	struct timeval tv_stop;
        int errno_saved;

        errno_saved = errno;
	gettimeofday(&tv_stop, NULL);
        errno = errno_saved;

	/* first calculate difference between start and current time */
	delta.tv_sec = tv_stop.tv_sec - tv_start->tv_sec;
	if (tv_stop.tv_usec < tv_start->tv_usec) {
		delta.tv_usec = 1000000 + tv_stop.tv_usec - tv_start->tv_usec;
		delta.tv_sec += 1;
	} else {
		delta.tv_usec = tv_stop.tv_usec - tv_start->tv_usec;
	}

	assert((delta.tv_sec >= 0) && (delta.tv_usec >= 0));

	/* substract delta from the given max. timeout */
	timeout->tv_sec -= delta.tv_sec;
	if (timeout->tv_usec < delta.tv_usec) {
		timeout->tv_usec = 1000000 + timeout->tv_usec - delta.tv_usec;
		timeout->tv_sec -= 1;
	} else {
		timeout->tv_usec -= delta.tv_usec;
	}

	/* check if timeout was underrun -> set rest timeout to zero */
	if ( (timeout->tv_sec < 0) || (timeout->tv_usec < 0) ) {
		timeout->tv_sec  = 0;
		timeout->tv_usec = 0;
	}
}

/**
 * @internal
 * @brief Waits in select() for the given file handle to become readable.
 *
 * @param fd file handle
 * @param timeout maximum time to wait; when the function returns the
 *   value is reduced by the time spent waiting.
 *
 * If the syscall is interrupted by an interrupt, the select() call
 * is repeated with a timeout reduced by the time already spent
 * waiting.
 *
 * @return
 * See select(2).
 */
int
vbi_capture_io_select( int fd, struct timeval * timeout )
{
	struct timeval tv_start;
	struct timeval tv;
	fd_set fds;
	int ret;

	while (1) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		tv = *timeout; /* Linux kernel overwrites this */
		gettimeofday(&tv_start, NULL);

		ret = select(fd + 1, &fds, NULL, NULL, &tv);

		vbi_capture_io_update_timeout(&tv_start, timeout);

		if ((ret < 0) && (errno == EINTR))
			continue;

		return ret;
	}
}

/* Helper functions to log the communication between the library and drivers.
   FIXME remove fp arg, call user log function instead (0.3). */

#define MODE_GUESS	0
#define MODE_ENUM	1
#define MODE_SET_FLAGS	2
#define MODE_ALL_FLAGS	3

/**
 * @internal
 * @param mode
 *   - GUESS if value is enumeration or flags (used by structpr.pl)
 *   - ENUM interpret value as an enumerated item
 *   - SET_FLAGS interpret value as a set of flags, print set ones
 *   - ALL_FLAGS interpret value as a set of flags, print all
 * @param value
 * @param ... vector of symbol (const char *) and value
 *   (unsigned long) pairs.  Last parameter must be NULL.
 */
void
fprint_symbolic			(FILE *			fp,
				 int			mode,
				 unsigned long		value,
				 ...)
{
	unsigned int i, j = 0;
	unsigned long v;
	const char *s;
	va_list ap;

	if (mode == 0) {
		unsigned int n[2] = { 0, 0 };

		va_start (ap, value);

		while ((s = va_arg (ap, const char *))) {
			v = va_arg (ap, unsigned long);
			n[0 == (v & (v - 1))]++; /* single bit? */
		}

		mode = MODE_ENUM + (n[1] > n[0]);

		va_end (ap); 
	}

	va_start (ap, value);

	for (i = 0; (s = va_arg (ap, const char *)); ++i) {
		v = va_arg (ap, unsigned long);

		if (v == value
		    || MODE_ALL_FLAGS == mode
		    || (MODE_SET_FLAGS == mode && 0 != (v & value))) {
			if (j++ > 0)
				fputc ('|', fp);
			if (MODE_ALL_FLAGS == mode && 0 == (v & value))
				fputc ('!', fp);
			fputs (s, fp);
			value &= ~v;
		}
	}

	if (0 == value && 0 == j)
		fputc ('0', fp);
	else if (value)
		fprintf (fp, "%s0x%lx", j ? "|" : "", value);

	va_end (ap); 
}

void
fprint_unknown_ioctl		(FILE *			fp,
				 unsigned int		cmd,
				 void *			arg)
{
	fprintf (fp, "<unknown cmd 0x%x %c%c arg=%p size=%u>",
		 cmd, IOCTL_READ (cmd) ? 'R' : '-',
		 IOCTL_WRITE (cmd) ? 'W' : '-',
		 arg, IOCTL_ARG_SIZE (cmd)); 
}

/**
 * @internal
 * Drop-in for open(). Logs the request on fp if given.
 */
int
device_open			(FILE *			fp,
				 const char *		pathname,
				 int			flags,
				 mode_t			mode)
{
	int fd;

	fd = open (pathname, flags, mode);

	if (fp)	{
		int saved_errno;

		saved_errno = errno;

		fprintf (fp, "%d = open (\"%s\", ", fd, pathname);
		fprint_symbolic (fp, MODE_SET_FLAGS, flags,
				 "RDONLY", O_RDONLY,
				 "WRONLY", O_WRONLY,
				 "RDWR", O_RDWR,
				 "CREAT", O_CREAT,
				 "EXCL", O_EXCL,
				 "TRUNC", O_TRUNC,
				 "APPEND", O_APPEND,
				 "NONBLOCK", O_NONBLOCK,
				 0);
		fprintf (fp, ", 0%o)", mode);

		if (-1 == fd) {
			fprintf (fp, ", errno=%d, %s\n",
				 saved_errno, strerror (saved_errno));
		} else {
			fputc ('\n', fp);
		}

		errno = saved_errno;
	}

	return fd;
}

/**
 * @internal
 * Drop-in for close(). Logs the request on fp if given.
 */
int
device_close			(FILE *			fp,
				 int			fd)
{
	int err;

	err = close (fd);

	if (err) {
		int saved_errno;

		saved_errno = errno;

		if (-1 == err) {
			fprintf (fp, "%d = close (%d), errno=%d, %s\n",
				 err, fd, saved_errno, strerror (saved_errno));
		} else {
			fprintf (fp, "%d = close (%d)\n", err, fd);
		}

		errno = saved_errno;
	}

	return err;
}

/**
 * @internal
 * Drop-in for ioctl(). Logs the request on fp if given. You must supply
 * a function printing the arguments, structpr.pl generates one for you
 * from a header file.
 */
int
device_ioctl			(FILE *			fp,
				 ioctl_log_fn *		log_fn,
				 int			fd,
				 unsigned int		cmd,
				 void *			arg)
{
	int buf[256];
	int err;

	if (fp && IOCTL_WRITE (cmd)) {
		assert (sizeof (buf) >= IOCTL_ARG_SIZE (cmd));
		memcpy (buf, arg, IOCTL_ARG_SIZE (cmd));
	}

	do err = ioctl (fd, cmd, arg);
	while (-1 == err && EINTR == errno);

	if (fp && log_fn) {
		int saved_errno;

		saved_errno = errno;

		fprintf (fp, "%d = ", err);

		log_fn (fp, cmd, 0, NULL);

		fputc ('(', fp);
      
		if (IOCTL_WRITE (cmd))
			log_fn (fp, cmd, IOCTL_READ (cmd) ? 2 : 0, &buf);

		if (-1 == err) {
			fprintf (fp, "), errno = %d, %s\n",
				 errno, strerror (errno));
		} else {
			if (IOCTL_READ (cmd)) {
				fputs (") -> (", fp);
				log_fn (fp, cmd, IOCTL_WRITE (cmd) ?
					1 : 0, arg);
			}

			fputs (")\n", fp);
		}

		errno = saved_errno;
	}

	return err;
}
