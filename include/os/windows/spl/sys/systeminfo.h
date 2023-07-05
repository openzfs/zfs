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

#ifndef _SPL_SYSTEMINFO_H
#define	_SPL_SYSTEMINFO_H

#define	HW_INVALID_HOSTID	0xFFFFFFFF	/* an invalid hostid */

/* minimum buffer size needed to hold a decimal or hex hostid string */
#define	HW_HOSTID_LEN		11

const char *spl_panicstr(void);
int spl_system_inshutdown(void);

#endif /* SPL_SYSTEMINFO_H */
