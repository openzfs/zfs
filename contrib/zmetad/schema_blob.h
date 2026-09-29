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

#ifndef	_ZMETAD_SCHEMA_BLOB_H
#define	_ZMETAD_SCHEMA_BLOB_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Embedded copy of the canonical event schema document.  Keep in sync
 * with events-schema.json; verified by schema-check.sh.
 */
extern const char *ZMETAD_EMBEDDED_SCHEMA_JSON;

#ifdef	__cplusplus
}
#endif

#endif	/* _ZMETAD_SCHEMA_BLOB_H */
