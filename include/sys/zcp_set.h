/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2019 Joyent, Inc.
 */

#ifndef _SYS_ZCP_SET_H
#define	_SYS_ZCP_SET_H

#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct zcp_set_prop_arg {
	lua_State	*state;
	const char	*dsname;
	const char	*prop;
	const char	*val;
} zcp_set_prop_arg_t;

int zcp_set_prop_check(void *arg, dmu_tx_t *tx);
void zcp_set_prop_sync(void *arg, dmu_tx_t *tx);

#ifdef  __cplusplus
}
#endif

#endif /* _SYS_ZCP_SET_H */
