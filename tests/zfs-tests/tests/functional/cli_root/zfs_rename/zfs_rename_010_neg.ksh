#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
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
