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
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _ZONE_H
#define	_ZONE_H



#include <sys/types.h>
#include <sys/zone.h>
#include <sys/priv.h>
#include <tsol/label.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define GLOBAL_ZONEID 0
#define GLOBAL_ZONEID_NAME "global"

/*
 * Functions for mapping between id and name for active zones.
 */
extern zoneid_t		getzoneid(void);
extern zoneid_t		getzoneidbyname(const char *);
extern ssize_t		getzonenamebyid(zoneid_t, char *, size_t);

#if 0

/*
 * NOTE
 *
 * The remaining contents of this file are private to the implementation
 * of Solaris and are subject to change at any time without notice,
 * Applications using these interfaces may fail to run on future releases.
 */

extern int zonept(int, zoneid_t);
extern int zone_get_id(const char *, zoneid_t *);

/* System call API */
extern zoneid_t	zone_create(const char *, const char *,
    const struct priv_set *, const char *, size_t, const char *, size_t, int *,
    int, int, const bslabel_t *, int);
extern int	zone_boot(zoneid_t);
extern int	zone_destroy(zoneid_t);
extern ssize_t	zone_getattr(zoneid_t, int, void *, size_t);
extern int	zone_setattr(zoneid_t, int, void *, size_t);
extern int	zone_enter(zoneid_t);
extern int	zone_list(zoneid_t *, uint_t *);
extern int	zone_shutdown(zoneid_t);
extern int	zone_version(int *);
extern int	zone_add_datalink(zoneid_t, char *);
extern int	zone_remove_datalink(zoneid_t, char *);
extern int	zone_check_datalink(zoneid_t *, char *);
extern int	zone_list_datalink(zoneid_t, int *, char *);

#endif

#ifdef	__cplusplus
}
#endif

#endif /* _ZONE_H */
