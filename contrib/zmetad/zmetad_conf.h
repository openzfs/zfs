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
 * CDDL HEADER END
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
