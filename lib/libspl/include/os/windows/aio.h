// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef _SPL_AIO_H
#define	_SPL_AIO_H

#include <sys/types.h>

#define	LIO_NOWAIT	0
#define	LIO_WAIT	1

#define	LIO_NOP		0
#define	LIO_READ	0x01    /* Must match value of FREAD in sys/file.h */
#define	LIO_WRITE	0x02    /* Must match value of FWRITE in sys/file.h */

typedef struct aiocb {
	int	aio_fildes;
	volatile void	*aio_buf;	/* buffer location */
	size_t		aio_nbytes;	/* length of transfer */
	off_t		aio_offset;	/* file offset */
	int		aio_reqprio;	/* request priority offset */
	// struct sigevent	aio_sigevent;	/* notification type */
	int		aio_lio_opcode;	/* listio operation */
	// aio_result_t	aio_resultp;	/* results */
	int		aio_state;	/* state flag for List I/O */
	int		aio__pad[1];	/* extension padding */
} aiocb_t;


static inline int lio_listio(int mode, struct aiocb *aiocb_list[],
    int nitems, void * sevp)
{
	errno = EIO;
	return (-1);
}

static inline int
aio_error(const struct aiocb *aiocbp)
{
	return (EOPNOTSUPP);
}

static inline ssize_t
aio_return(const struct aiocb *aiocbp)
{
	return (EOPNOTSUPP);
}

#endif
