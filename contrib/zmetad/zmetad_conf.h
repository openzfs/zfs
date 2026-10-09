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

#ifndef	_ZMETAD_CONF_H
#define	_ZMETAD_CONF_H

#include <sys/types.h>

#include "zmetad.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Load key = value settings from the zmetad configuration file into
 * "cfg".  The caller must have initialized "cfg" (config_init or
 * equivalent) first: only keys present in the file are overwritten,
 * so the precedence order built-in defaults < conf file < CLI flags
 * is produced by loading the file between the two.
 *
 * Returns 0 when the file was loaded, 1 when the file does not exist
 * (not an error: the daemon runs on defaults), and -1 on a hard
 * error (unreadable file, or an invalid line) with a human-readable
 * reason in "err" (up to "errlen" bytes).
 */
int zmetad_conf_load(zmetad_config_t *cfg, const char *path,
    char *err, size_t errlen);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZMETAD_CONF_H */
