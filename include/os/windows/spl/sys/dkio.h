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

#ifndef _SPL_DKIO_H
#define	_SPL_DKIO_H

struct dk_callback {
	void (*dkc_callback)(void *dkc_cookie, int error);
	void *dkc_cookie;
	int dkc_flag;
};

#define	DKIOC			(0x04 << 8)
#define	DKIOCFLUSHWRITECACHE	(DKIOC | 34)
#define	DKIOCTRIM		(DKIOC | 35)

#endif /* _SPL_DKIO_H */
