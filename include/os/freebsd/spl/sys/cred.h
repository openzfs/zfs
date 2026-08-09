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
