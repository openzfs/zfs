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
#	When renaming a set of snapshots, if a snapshot already exists with
#	the new name, then none of the snapshots is renamed.
#
# STRATEGY:
#	1. Create a snapshot for a set of datasets.
#	2. Create a new snapshot for one of datasets.
#	3. Attempt to "zfs rename -r" with the second snapshot's name.
#	4. Verify none of the snapshots is renamed.
#

verify_runnable "both"

function cleanup
{
	for poolname in $(get_all_pools); do
		for snap in $(zfs list -H -t snapshot -o name -r $poolname); do
			log_must zfs destroy $snap
		done
	done
}

log_assert "Verify zfs rename -r failed when the snapshot name already exists."
log_onexit cleanup

set -A datasets $TESTPOOL		$TESTPOOL/$TESTCTR \
	$TESTPOOL/$TESTCTR/$TESTFS1	$TESTPOOL/$TESTFS
if is_global_zone; then
	datasets[${#datasets[@]}]=$TESTPOOL/$TESTVOL
fi

log_must zfs snapshot -r ${TESTPOOL}@snap
typeset -i i=0
while ((i < ${#datasets[@]})); do
	# Create one more snapshot
	log_must zfs snapshot ${datasets[$i]}@snap2
	log_mustnot zfs rename -r ${TESTPOOL}@snap ${TESTPOOL}@snap2
	log_must zfs destroy ${datasets[$i]}@snap2

	# Check datasets, make sure none of them have snap2.
	typeset -i j=0
	while ((j < ${#datasets[@]})); do
		if datasetexists ${datasets[$j]}@snap2 ; then
			log_fail "${datasets[$j]}@snap2 should not exist."
		fi
		((j += 1))
	done

	((i += 1))
done

log_pass "zfs rename -r failed when the snapshot name already exists."
