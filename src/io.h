/*
 *  libzvbi - Device interfaces
 *
 *  Copyright (C) 2002 Michael H. Schimek
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

/* $Id: io.h,v 1.15 2004-10-05 23:45:52 mschimek Exp $ */

#ifndef IO_H
#define IO_H

#include "decoder.h"

/* Public */

#include <sys/time.h> /* struct timeval */

/**
 * @ingroup Device
 */
typedef struct vbi_capture_buffer {
	void *			data;
	int			size;
	double			timestamp;
} vbi_capture_buffer;

/**
 * @ingroup Device
 * @brief Opaque device interface handle.
 **/
typedef struct vbi_capture vbi_capture;

/**
 * @ingroup Device
 * @brief Properties of capture file handle
 */
typedef enum {
       /**
        * Is set when select(2) can be used to wait for
        * new data on the capture device file handle.
        */
        VBI_FD_HAS_SELECT  = 1<<0,
       /**
        * Is set when the capture device supports
        * "user-space DMA".  In this case it's more efficient
        * to use one of the "pull" functions to read raw data
        * because otherwise the data has to be copied once
        * more into the passed buffer.
        */
        VBI_FD_HAS_MMAP    = 1<<1,
       /**
        * Is not set when the capture device file handle is
        * not the actual device.  In this case it can only be
        * used for select(2) and not for ioctl(2)
        */
        VBI_FD_IS_DEVICE   = 1<<2,
} VBI_CAPTURE_FD_FLAGS;

/**
 * @addtogroup Device
 * @{
 */
extern vbi_capture *	vbi_capture_v4l2_new(const char *dev_name, int buffers,
					     unsigned int *services, int strict,
					     char **errorstr, vbi_bool trace);
extern vbi_capture *	vbi_capture_v4l2k_new(const char *	dev_name,
					      int		fd,
					      int		buffers,
					      unsigned int *	services,
					      int		strict,
					      char **		errorstr,
					      vbi_bool		trace);
extern vbi_capture *	vbi_capture_v4l_new(const char *dev_name, int scanning,
					    unsigned int *services, int strict,
					    char **errorstr, vbi_bool trace);
extern vbi_capture *	vbi_capture_v4l_sidecar_new(const char *dev_name, int given_fd,
						    unsigned int *services,
						    int strict, char **errorstr, 
						    vbi_bool trace);
extern vbi_capture *	vbi_capture_bktr_new (const char *	dev_name,
					      int		scanning,
					      unsigned int *	services,
					      int		strict,
					      char **		errstr,
					      vbi_bool		trace);
extern int		vbi_capture_dvb_filter(vbi_capture *cap, int pid);
extern vbi_capture*	vbi_capture_dvb_new(char *dev, int scanning,
				        unsigned int *services, int strict,
				        char **errstr, vbi_bool trace);

struct vbi_proxy_client;
 
extern vbi_capture *
vbi_capture_proxy_new( struct vbi_proxy_client * vpc,
                        int buffers, int scanning,
                        unsigned int *p_services, int strict,
                        char **pp_errorstr );

extern int		vbi_capture_read_raw(vbi_capture *capture, void *data,
					     double *timestamp, struct timeval *timeout);
extern int		vbi_capture_read_sliced(vbi_capture *capture, vbi_sliced *data, int *lines,
						double *timestamp, struct timeval *timeout);
extern int		vbi_capture_read(vbi_capture *capture, void *raw_data,
					 vbi_sliced *sliced_data, int *lines,
					 double *timestamp, struct timeval *timeout);
extern int		vbi_capture_pull_raw(vbi_capture *capture, vbi_capture_buffer **buffer,
					     struct timeval *timeout);
extern int		vbi_capture_pull_sliced(vbi_capture *capture, vbi_capture_buffer **buffer,
						struct timeval *timeout);
extern int		vbi_capture_pull(vbi_capture *capture, vbi_capture_buffer **raw_buffer,
					 vbi_capture_buffer **sliced_buffer, struct timeval *timeout);
extern vbi_raw_decoder *vbi_capture_parameters(vbi_capture *capture);
extern int		vbi_capture_fd(vbi_capture *capture);
extern unsigned int     vbi_capture_update_services(vbi_capture *capture,
                                                    vbi_bool reset, vbi_bool commit,
                                                    unsigned int services, int strict,
                                                    char ** errorstr);
extern int              vbi_capture_get_scanning(vbi_capture *capture);
extern void             vbi_capture_flush(vbi_capture *capture);
extern void		vbi_capture_delete(vbi_capture *capture);

extern vbi_bool         vbi_capture_set_video_path(vbi_capture *capture, const char * p_dev_video);
extern VBI_CAPTURE_FD_FLAGS vbi_capture_get_fd_flags(vbi_capture *capture);
/** @} */

/* Private */

#ifndef DOXYGEN_SHOULD_IGNORE_THIS

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>		/* open() */

extern const char _zvbi_intl_domainname[];

#ifndef _
#  ifdef ENABLE_NLS
#    include <libintl.h>
#    define _(String) dgettext (_zvbi_intl_domainname, String)
#    ifdef gettext_noop
#      define N_(String) gettext_noop (String)
#    else
#      define N_(String) (String)
#    endif
#  else /* Stubs that do something close enough.  */
#    define gettext(Msgid) ((const char *) (Msgid))
#    define dgettext(Domainname, Msgid) ((const char *) (Msgid))
#    define dcgettext(Domainname, Msgid, Category) ((const char *) (Msgid))
#    define ngettext(Msgid1, Msgid2, N) \
       ((N) == 1 ? (const char *) (Msgid1) : (const char *) (Msgid2))
#    define dngettext(Domainname, Msgid1, Msgid2, N) \
       ((N) == 1 ? (const char *) (Msgid1) : (const char *) (Msgid2))
#    define dcngettext(Domainname, Msgid1, Msgid2, N, Category) \
       ((N) == 1 ? (const char *) (Msgid1) : (const char *) (Msgid2))
#    define textdomain(Domainname) ((const char *) (Domainname))
#    define bindtextdomain(Domainname, Dirname) ((const char *) (Dirname))
#    define bind_textdomain_codeset(Domainname, Codeset) ((const char *) (Codeset))
#    define _(String) (String)
#    define N_(String) (String)
#  endif
#endif

#if defined (_IOC_SIZE) /* Linux */

#define IOCTL_ARG_SIZE(cmd)	_IOC_SIZE (cmd)
#define IOCTL_READ(cmd)		(_IOC_DIR (cmd) & _IOC_READ)
#define IOCTL_WRITE(cmd)	(_IOC_DIR (cmd) & _IOC_WRITE)
#define IOCTL_READ_WRITE(cmd)	(_IOC_DIR (cmd) == (_IOC_READ | _IOC_WRITE))
#define IOCTL_NUMBER(cmd)	_IOC_NR (cmd)

#elif defined (IOCPARM_LEN) /* FreeBSD */

#define IOCTL_ARG_SIZE(cmd)	IOCPARM_LEN (cmd)
#define IOCTL_READ(cmd)		((cmd) & IOC_OUT)
#define IOCTL_WRITE(cmd)	((cmd) & IOC_IN)
#define IOCTL_READ_WRITE(cmd)	(((cmd) & IOC_DIRMASK) == (IOC_IN | IOC_OUT))
#define IOCTL_NUMBER(cmd)	((cmd) & 0xFF)

#else /* Don't worry, only used for debugging */

#define IOCTL_ARG_SIZE(cmd)	0
#define IOCTL_READ(cmd)		0
#define IOCTL_WRITE(cmd)	0
#define IOCTL_READ_WRITE(cmd)	0
#define IOCTL_NUMBER(cmd)	0

#endif

typedef void
ioctl_log_fn			(FILE *			fp,
				 unsigned int		cmd,
				 int			rw,
				 void *			arg);
extern void
fprint_symbolic			(FILE *			fp,
				 int			mode,
				 unsigned long		value,
				 ...);
extern void
fprint_unknown_ioctl		(FILE *			fp,
				 unsigned int		cmd,
				 void *			arg);
extern int
device_open			(FILE *			fp,
				 const char *		pathname,
				 int			flags,
				 mode_t			mode);
extern int
device_close			(FILE *			fp,
				 int			fd);
extern int
device_ioctl			(FILE *			fp,
				 ioctl_log_fn *		fn,
				 int			fd,
				 unsigned int		cmd,
				 void *			arg);
extern void *
device_mmap			(FILE *			fp,
				 void *			start,
				 size_t			length,
				 int			prot,
				 int			flags,
				 int			fd,
				 off_t			offset);
extern int
device_munmap			(FILE *			fp,
				 void *			start,
				 size_t			length);

extern void
vbi_capture_set_log_fp		(vbi_capture *		capture,
				 FILE *			fp);

/* Preliminary hack for tests. */
extern vbi_bool vbi_capture_force_read_mode;

#endif /* !DOXYGEN_SHOULD_IGNORE_THIS */

/**
 * @ingroup Devmod
 */
struct vbi_capture {
	vbi_bool		(* read)(vbi_capture *, vbi_capture_buffer **,
					 vbi_capture_buffer **, struct timeval *);
	vbi_raw_decoder *	(* parameters)(vbi_capture *);
        unsigned int            (* update_services)(vbi_capture *,
                                         vbi_bool, vbi_bool,
                                         unsigned int, int, char **);
        int                     (* get_scanning)(vbi_capture *);
	void			(* flush)(vbi_capture *);
	int			(* get_fd)(vbi_capture *);
	VBI_CAPTURE_FD_FLAGS	(* get_fd_flags)(vbi_capture *);
	vbi_bool 		(* set_video_path)(vbi_capture *, const char *);
	void			(* _delete)(vbi_capture *);

	/* Log all system calls if non-NULL. */
	FILE *			sys_log_fp;
};

extern void
vbi_capture_io_update_timeout	(struct timeval *	timeout,
				 const struct timeval *	tv_start);
extern int
vbi_capture_io_select		(int			fd,
				 struct timeval *	timeout);

#endif /* IO_H */
