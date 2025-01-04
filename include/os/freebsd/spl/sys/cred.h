// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef _SYS_CRED_H
#define	_SYS_CRED_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The credential is an opaque kernel private data structure defined in
 * <sys/cred_impl.h>.
 */

typedef struct ucred cred_t;

#define	CRED()		curthread->td_ucred

/*
 * kcred is used when you need all privileges.
 */
#define	kcred	(thread0.td_ucred)

#define	KUID_TO_SUID(x)		(x)
#define	KGID_TO_SGID(x)		(x)
#define	crgetuid(cr)		((cr)->cr_uid)
#define	crgetruid(cr)		((cr)->cr_ruid)
#define	crgetgid(cr)		((cr)->cr_gid)
#define	crgetgroups(cr)		((cr)->cr_groups)
#define	crgetngroups(cr)	((cr)->cr_ngroups)
#define	crgetzoneid(cr) 	((cr)->cr_prison->pr_id)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRED_H */
