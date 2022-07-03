#!/bin/ksh -p
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
