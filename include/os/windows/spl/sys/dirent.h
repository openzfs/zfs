/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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

#ifndef _SPL_DIRENT_H
#define	_SPL_DIRENT_H

#include <sys/extdirent.h>

#define	MAXNAMLEN	255

/*
 * File types
 */
#define	DT_UNKNOWN	0
#define	DT_FIFO		1
#define	DT_CHR		2
#define	DT_DIR		4
#define	DT_BLK		6
#define	DT_REG		8
#define	DT_LNK		10
#define	DT_SOCK		12
#define	DT_WHT		14

struct dirent {
	uint64_t d_ino;		/* file number of entry */
	uint64_t d_seekoff;	/* seek offset (optional, used by servers) */
	uint16_t d_reclen;	/* length of this record */
	uint16_t d_namlen;	/* length of string in d_name */
	uint8_t	 d_type;	/* file type, see below */
	char	 d_name[MAXPATHLEN]; /* entry name (up to MAXPATHLEN bytes) */
};

#ifndef IFTODT
#define	IFTODT(mode)    (((mode) & 0170000) >> 12)
#endif
#define	DTTOIF(dirtype) ((dirtype) << 12)


#endif /* SPL_DIRENT_H */
