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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Verify that read-only properties are immutable.
# Note that we can only check properties that have no possibility of
# changing while we are running (which excludes e.g. "available").
#
# STRATEGY:
# 1. Create pool, fs, vol, fs@snap & vol@snap.
# 2. Get the original property value and set value to those properties.
# 3. Check return value.
# 4. Compare the current property value with the original one.
#

verify_runnable "both"

set -A values filesystem volume snapshot -3 0 1 50K 10G 80G \
	2005/06/17 30K 20x yes no \
	on off default pool/fs@snap $TESTDIR
set -A dataset $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTCTR/$TESTFS1 $TESTPOOL/$TESTFS@$TESTSNAP \
	$TESTPOOL/$TESTVOL@$TESTSNAP
typeset ro_props="type used creation referenced refer compressratio \
	mounted origin"
typeset snap_ro_props="volsize recordsize recsize quota reservation reserv mountpoint \
	sharenfs checksum compression compress atime devices exec readonly rdonly \
	setuid version"
if is_freebsd; then
	snap_ro_props+=" jailed"
else
	snap_ro_props+=" zoned"
fi

function cleanup
{
	datasetexists $TESTPOOL/$TESTVOL@$TESTSNAP && \
		destroy_snapshot $TESTPOOL/$TESTVOL@$TESTSNAP
	datasetexists $TESTPOOL/$TESTFS@$TESTSNAP && \
		destroy_snapshot $TESTPOOL/$TESTFS@$TESTSNAP
}

log_assert "Verify that read-only properties are immutable."
log_onexit cleanup

# Create filesystem and volume's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP
sync_pool $TESTPOOL
sleep 5

typeset -i i=0
typeset -i j=0
typeset cur_value=""
typeset props=""

while (( i < ${#dataset[@]} )); do
	props=$ro_props

	dst_type=$(get_prop type ${dataset[i]})
	if [[ $dst_type == 'snapshot' ]]; then
		props="$ro_props $snap_ro_props"
	fi

	for prop in $props; do
		cur_value=$(get_prop $prop ${dataset[i]})

		j=0
		while (( j < ${#values[@]} )); do
			#
			# If the current property value is equal to values[j],
			# just expect it failed. Otherwise, set it to dataset,
			# expecting it failed and the property value is not
			# equal to values[j].
			#
			if [[ $cur_value == ${values[j]} ]]; then
				log_mustnot zfs set $prop=${values[j]} \
					${dataset[i]}
			else
				set_n_check_prop ${values[j]} $prop \
					${dataset[i]} false
			fi
			(( j += 1 ))
		done
	done
	(( i += 1 ))
done

log_pass "Setting uneditable properties should failed. It passed."
