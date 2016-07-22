/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_PRIV_H
#define	_LIBSPL_PRIV_H

#include <sys/types.h>

/* Couldn't find this definition in OpenGrok */
#define	PRIV_SYS_CONFIG	"sys_config"

/*
 * priv_op_t indicates a privilege operation type
 */
typedef enum priv_op {
	PRIV_ON,
	PRIV_OFF,
	PRIV_SET
} priv_op_t;

static inline boolean_t priv_ineffect(const char *priv) { return B_TRUE; }

#endif
