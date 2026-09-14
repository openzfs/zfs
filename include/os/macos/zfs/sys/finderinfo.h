// SPDX-License-Identifier: APSL-2.0
/*
 * Copyright (c) Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 *
 * Portions derived from Apple's HFS+ on-disk Finder info structures
 * (xnu hfs/hfs_format.h).
 */

#ifndef FINDERINFO_H
#define	FINDERINFO_H


struct FndrExtendedDirInfo {
	u_int32_t document_id;
	u_int32_t date_added;
	u_int16_t extended_flags;
	u_int16_t reserved3;
	u_int32_t write_gen_counter;
} __attribute__((aligned(2), packed));

struct FndrExtendedFileInfo {
	u_int32_t document_id;
	u_int32_t date_added;
	u_int16_t extended_flags;
	u_int16_t reserved2;
	u_int32_t write_gen_counter;
} __attribute__((aligned(2), packed));

/* Finder information */
struct FndrFileInfo {
	u_int32_t fdType;
	u_int32_t fdCreator;
	u_int16_t fdFlags;
	struct {
		int16_t v;
		int16_t h;
	} fdLocation;
	int16_t opaque;
} __attribute__((aligned(2), packed));
typedef struct FndrFileInfo FndrFileInfo;



#endif
