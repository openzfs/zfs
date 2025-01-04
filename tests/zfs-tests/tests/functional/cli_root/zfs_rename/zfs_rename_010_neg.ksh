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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	The recursive flag -r can only be used for snapshots and not for
#	volumes/filesystems.
#
# STRATEGY:
#	1. Loop pool, fs, container and volume.
#	2. Verify none of them can be rename by rename -r.
#

verify_runnable "both"

log_assert "The recursive flag -r can only be used for snapshots."

set -A datasets $TESTPOOL		$TESTPOOL/$TESTCTR \
	$TESTPOOL/$TESTCTR/$TESTFS1	$TESTPOOL/$TESTFS
if is_global_zone; then
	datasets[${#datasets[@]}]=$TESTPOOL/$TESTVOL
fi

for opts in "-r" "-r -p"; do
	typeset -i i=0
	while ((i < ${#datasets[@]})); do
		log_mustnot zfs rename $opts ${datasets[$i]} \
			${datasets[$i]}-new

		# Check datasets, make sure none of them was renamed.
		typeset -i j=0
		while ((j < ${#datasets[@]})); do
			if datasetexists ${datasets[$j]}-new ; then
				log_fail "${datasets[$j]}-new should not exists."
			fi
			((j += 1))
		done

		((i += 1))
	done
done

log_pass "The recursive flag -r can only be used for snapshots passed."
