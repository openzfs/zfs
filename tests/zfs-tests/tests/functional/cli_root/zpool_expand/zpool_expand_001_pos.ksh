#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# DESCRIPTION:
# Once zpool set autoexpand=on poolname, zpool can autoexpand by
# Dynamic LUN Expansion
#
#
# STRATEGY:
# 1) Create a pool
# 2) Create volume on top of the pool
# 3) Create pool by using the zvols and set autoexpand=on
# 4) Expand the vol size by 'zfs set volsize'
# 5) Check that the pool size was expanded
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	for i in 1 2 3; do
		destroy_dataset $VFS/vol$i
	done
}

log_onexit cleanup

log_assert "zpool can be autoexpanded after set autoexpand=on on LUN expansion"

for i in 1 2 3; do
	log_must $ZFS create -V $org_size $VFS/vol$i
done

[[ -n "$LINUX" ]] && sleep 1

for type in " " mirror raidz raidz2; do
	log_must $ZPOOL create -o autoexpand=on $TESTPOOL1 $type \
	    $ZVOL_DEVDIR/$VFS/vol1  $ZVOL_DEVDIR/$VFS/vol2 \
	    $ZVOL_DEVDIR/$VFS/vol3

	typeset autoexp=$(get_pool_prop autoexpand $TESTPOOL1)
	if [[ $autoexp != "on" ]]; then
		log_fail "zpool $TESTPOOL1 autoexpand should on but is $autoexp"
	fi

	typeset prev_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_prev_size=$(get_prop avail $TESTPOOL1)

	for i in 1 2 3; do
		log_must $ZFS set volsize=$exp_size $VFS/vol$i
	done

	$SYNC
	$SLEEP 10
	$SYNC

	typeset expand_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_expand_size=$(get_prop avail $TESTPOOL1)

	log_note "$TESTPOOL1 $type has previous size: $prev_size and " \
	    "expanded size: $expand_size"
	# compare available pool size from zfs
	if [[ $zfs_expand_size > $zfs_prev_size ]]; then
	# check for zpool history for the pool size expansion
		if [[ $type == " " ]]; then
			typeset	size_addition=$($ZPOOL history -il $TESTPOOL1 |\
			    $GREP "pool '$TESTPOOL1' size:" | \
			    $GREP "vdev online" | \
			    $GREP "(+${EX_1GB}" | wc -l)

			if [[ $size_addition -ne $i ]]; then
				log_fail "pool $TESTPOOL1 is not autoexpand " \
				    "after LUN expansion"
			fi
		elif [[ $type == "mirror" ]]; then
			$ZPOOL history -il $TESTPOOL1 | \
			    $GREP "pool '$TESTPOOL1' size:" | \
			    $GREP "vdev online" | \
			    $GREP "(+${EX_1GB})" >/dev/null 2>&1

			if [[ $? -ne 0 ]] ; then
				log_fail "pool $TESTPOOL1 is not autoexpand " \
				    "after LUN expansion"
			fi
		else
			$ZPOOL history -il $TESTPOOL1 | \
			    $GREP "pool '$TESTPOOL1' size:" | \
			    $GREP "vdev online" | \
			    $GREP "(+${EX_3GB})" >/dev/null 2>&1

			if [[ $? -ne 0 ]]; then
				log_fail "pool $TESTPOOL is not autoexpand " \
				    "after LUN expansion"
			fi
		fi
	else
		log_fail "pool $TESTPOOL1 is not autoexpanded after LUN " \
		    "expansion"
	fi

	destroy_pool $TESTPOOL1
	for i in 1 2 3; do
		log_must $ZFS set volsize=$org_size $VFS/vol$i
	done

done
log_pass "zpool can be autoexpanded after set autoexpand=on on LUN expansion"
