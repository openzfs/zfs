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
# Copyright (c) 2012, 2018 by Delphix. All rights reserved.
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# DESCRIPTION:
# After zpool online -e poolname zvol vdevs, zpool can autoexpand by
# Dynamic VDEV Expansion
#
#
# STRATEGY:
# 1) Create 3 files
# 2) Create a pool backed by the files
# 3) Expand the files' size with truncate
# 4) Use zpool reopen to check the expandsize
# 5) Use zpool online -e to online the vdevs
# 6) Check that the pool size was expanded
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	for i in 1 2 3; do
		[ -e ${TEMPFILE}.$i ] && log_must rm ${TEMPFILE}.$i
	done
}

log_onexit cleanup

log_assert "zpool can expand after zpool online -e zvol vdevs on vdev expansion"

for type in " " mirror raidz draid:1s; do
	# Initialize the file devices and the pool
	for i in 1 2 3; do
		log_must truncate -s $org_size ${TEMPFILE}.$i
	done

	log_must zpool create $TESTPOOL1 $type $TEMPFILE.1 \
	    $TEMPFILE.2 $TEMPFILE.3

	typeset autoexp=$(get_pool_prop autoexpand $TESTPOOL1)

	if [[ $autoexp != "off" ]]; then
		log_fail "zpool $TESTPOOL1 autoexpand should be off but is " \
		    "$autoexp"
	fi
	typeset prev_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_prev_size=$(get_prop avail $TESTPOOL1)

	# Increase the size of the file devices
	for i in 1 2 3; do
		log_must truncate -s $exp_size ${TEMPFILE}.$i
	done

	# Reopen the pool and check that the `expandsize` property is set
	log_must zpool reopen $TESTPOOL1
	typeset zpool_expandsize=$(get_pool_prop expandsize $TESTPOOL1)

	if [[ $type == "mirror" ]]; then
		typeset expected_zpool_expandsize=$(($exp_size-$org_size))
	elif [[ $type == "draid:1s" ]]; then
		typeset expected_zpool_expandsize=$((2*($exp_size-$org_size)))
	else
		typeset expected_zpool_expandsize=$((3*($exp_size-$org_size)))
	fi

	if [[ "$zpool_expandsize" = "-" ]]; then
		log_fail "pool $TESTPOOL1 did not detect any " \
		    "expandsize after reopen"
	fi

	if [[ $zpool_expandsize -ne $expected_zpool_expandsize ]]; then
		log_fail "pool $TESTPOOL1 did not detect correct " \
		    "expandsize after reopen: found $zpool_expandsize," \
		    "expected $expected_zpool_expandsize"
	fi

	# Online the devices to add the new space to the pool.  Add an
	# artificial delay between online commands order to prevent them
	# from being merged in to a single history entry.  This makes
	# is easier to verify each expansion for the striped pool case.
	for i in 1 2 3; do
		log_must zpool online -e $TESTPOOL1 ${TEMPFILE}.$i
		sleep 3
	done

	typeset expand_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_expand_size=$(get_prop avail $TESTPOOL1)
	log_note "$TESTPOOL1 $type has previous size: $prev_size and " \
	    "expanded size: $expand_size"

	# compare available pool size from zfs
	if [[ $zfs_expand_size -gt $zfs_prev_size ]]; then
	# check for zpool history for the pool size expansion
		if [[ $type == " " ]]; then
			typeset expansion_size=$(($exp_size-$org_size))
			typeset	size_addition=$(zpool history -il $TESTPOOL1 \
			    | grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep "(+${expansion_size}" | wc -l)

			if [[ $size_addition -ne $i ]]; then
				log_fail "pool $TESTPOOL1 has not expanded " \
				    "after zpool online -e, " \
				    "$size_addition/3 vdevs expanded"
			fi
		elif [[ $type == "mirror" ]]; then
			typeset expansion_size=$(($exp_size-$org_size))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep "(+${expansion_size})" >/dev/null 2>&1

			if [[ $? -ne 0 ]]; then
				log_fail "pool $TESTPOOL1 has not expanded " \
				    "after zpool online -e"
			fi
		elif [[ $type == "draid:1s" ]]; then
			typeset expansion_size=$((2*($exp_size-$org_size)))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep "(+${expansion_size})" >/dev/null 2>&1

			if [[ $? -ne 0 ]] ; then
				log_fail "pool $TESTPOOL1 has not expanded " \
				    "after zpool online -e"
			fi
		else
			typeset expansion_size=$((3*($exp_size-$org_size)))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep "(+${expansion_size})" >/dev/null 2>&1

			if [[ $? -ne 0 ]] ; then
				log_fail "pool $TESTPOOL1 has not expanded " \
				    "after zpool online -e"
			fi
		fi
	else
		log_fail "pool $TESTPOOL1 did not expand after vdev " \
		    "expansion and zpool online -e"
	fi

	# For dRAID pools verify the distributed spare was resized after
	# expansion and it is large enough to be used to replace a pool vdev.
	if [[ $type == "draid:1s" ]]; then
		log_must zpool replace -w $TESTPOOL1 $TEMPFILE.3 draid1-0-0
		verify_pool $TESTPOOL1
	fi

	log_must zpool destroy $TESTPOOL1
done
log_pass "zpool can expand after zpool online -e"
