/*
 * CDDL HEADER START
 *
 *  The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or  http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
