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
 * Copyright (c) 2008 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SUNDDI_H
#define	_SYS_SUNDDI_Ha

#include <sys/types.h>

/*
 * Generic Sun DDI definitions.
 */
#define DDI_SUCCESS		(0)   /* successful return */
#define DDI_FAILURE		(-1)  /* unsuccessful return */
#define DDI_NOT_WELL_FORMED	(-2)  /* A dev_info node is not valid */
#define DDI_EAGAIN		(-3)  /* not enough interrupt resources */
#define DDI_EINVAL		(-4)  /* invalid request or arguments */
#define DDI_ENOTSUP		(-5)  /* operation is not supported */
#define DDI_EPENDING		(-6)  /* operation or an event is pending */

/*
 * General-purpose DDI error return value definitions
 */
#define DDI_ENOMEM		1     /* memory not available */
#define DDI_EBUSY		2     /* busy */
#define DDI_ETRANSPORT		3     /* transport down */
#define DDI_ECONTEXT		4     /* context error */

/*
 * DDI_DEV_T_NONE:      When creating, property is not associated with
 *                      particular dev_t.
 * DDI_DEV_T_ANY:       Wildcard dev_t when searching properties.
 */
#define DDI_DEV_T_NONE		((dev_t)-1)
#define DDI_DEV_T_ANY		((dev_t)-2)

/*
 * Property flags:
 */
#define DDI_PROP_DONTPASS	0x0001  /* Don't pass request to parent */
#define DDI_PROP_CANSLEEP	0x0002  /* Memory allocation may sleep */


extern int ddi_strtoul(const char *, char **, int, unsigned long *);
extern int ddi_strtol(const char *, char **, int, long *);
extern int ddi_strtoull(const char *, char **, int, unsigned long long *);
extern int ddi_strtoll(const char *, char **, int, long long *);

extern int ddi_prop_lookup_string(dev_t, dev_info_t *, uint_t, char *, char **);
extern void ddi_prop_free(void *);

#endif	/* _SYS_SUNDDI_H */
