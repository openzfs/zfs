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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ACL_IMPL_H
#define	_SYS_ACL_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * acl flags
 *
 * ACL_AUTO_INHERIT, ACL_PROTECTED and ACL_DEFAULTED
 * flags can also be stored in this field.
 */
#define	ACL_IS_TRIVIAL	0x10000
#define	ACL_IS_DIR	0x20000

typedef enum acl_type {
	ACLENT_T = 0,
	ACE_T = 1
} acl_type_t;

struct acl_info {
	acl_type_t acl_type;		/* style of acl */
	int acl_cnt;			/* number of acl entries */
	int acl_entry_size;		/* sizeof acl entry */
	int acl_flags;			/* special flags about acl */
	void *acl_aclp;			/* the acl */
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ACL_IMPL_H */
