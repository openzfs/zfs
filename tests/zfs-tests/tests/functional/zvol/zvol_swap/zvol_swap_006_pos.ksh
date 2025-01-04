#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	A volume can be added as several segments, but overlapping segments
#	are not allowed.
#
# STRATEGY:
#	1. Figure out three groups swaplow and swaplen.
#	2. Verify different volume segments can be added correctly.
#	3. Verify overlapping swap volume are not allowed.
#

verify_runnable "global"

function cleanup
{
	typeset -i i=0

	while ((count > 0)); do
		log_must swap -d $swapname ${swap_opt[$i]}

		((i += 2))
		((count -= 1))
	done
}

log_assert "Verify volume can be add as several segments, but overlapping " \
	"are not allowed."
log_onexit cleanup

# swap -a won't allow the use of multiple segments of the same volume unless
# libdiskmgmt is disabled with the environment variable below.
typeset -x NOINUSE_CHECK=1

typeset vol=$TESTPOOL/$TESTVOL
typeset -i pageblocks volblocks
((pageblocks = $(getconf PAGESIZE) / 512))
((volblocks = $(get_prop volsize $vol) / 512))

log_note "Verify volume can be add as several segments."

#
#		swaplow			swaplen
set -A swap_opt	$((pageblocks))	    \
		$((RANDOM % (50 * pageblocks) + 2 * pageblocks)) \
		$((volblocks / 3))  \
		$((RANDOM % (50 * pageblocks) + 2 * pageblocks)) \
		$((volblocks / 2))  \
		$((RANDOM % (50 * pageblocks) + 2 * pageblocks)) \
		$(((volblocks*2) / 3))  \
		$((RANDOM % (50 * pageblocks) + 2 * pageblocks))

swapname=${ZVOL_DEVDIR}/$vol
typeset -i i=0 count=0

if is_swap_inuse $swapname ; then
	log_must swap -d $swapname
fi

while ((i < ${#swap_opt[@]})); do
	log_must swap -a $swapname ${swap_opt[$i]} ${swap_opt[((i+1))]}

	((i += 2))
	((count += 1))
done

log_note "Verify overlapping swap volume are not allowed"
i=0
while ((i < ${#swap_opt[@]})); do
	log_mustnot swap -a $swapname ${swap_opt[$i]}

	((i += 2))
done

log_pass "Verify volume can be added as several segments passed."
