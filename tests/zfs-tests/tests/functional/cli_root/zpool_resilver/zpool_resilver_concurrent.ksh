#!/bin/ksh -p
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
# Copyright (c) 2023 Hewlett Packard Enterprise Development LP.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	Verify 'zpool clear' doesn't cause concurrent resilvers
#
# STRATEGY:
#	1. Create N(10) virtual disk files.
#	2. Create draid pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Force-fault 2 vdevs and verify distributed spare is kicked in.
#	5. Free the distributed spare by replacing the faulty drive.
#	6. Run zpool clear and verify that it does not initiate 2 resilvers
#	   concurrently while distributed spare gets kicked in.
#

verify_runnable "global"

typeset -ir devs=10
typeset -ir nparity=1
typeset -ir ndata=8
typeset -ir dspare=1

function cleanup
{
	poolexists "$TESTPOOL" && destroy_pool "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$BASEDIR/vdev$i"
	done

	for dir in $BASEDIR; do
		if [[ -d $dir ]]; then
			log_must rm -rf $dir
		fi
	done

	zed_stop
	zed_cleanup
}

log_assert "Verify zpool clear on draid pool doesn't cause concurrent resilvers"
log_onexit cleanup

setup_test_env $TESTPOOL draid${nparity}:${ndata}d:${dspare}s $devs

# ZED needed for sequential resilver
zed_setup
log_must zed_start

log_must zpool offline -f $TESTPOOL $BASEDIR/vdev5
log_must wait_vdev_state  $TESTPOOL draid1-0-0 "ONLINE" 60
log_must zpool wait -t resilver $TESTPOOL
log_must zpool offline -f $TESTPOOL $BASEDIR/vdev6

log_must zpool labelclear -f $BASEDIR/vdev5
log_must zpool labelclear -f $BASEDIR/vdev6

log_must zpool replace -w $TESTPOOL $BASEDIR/vdev5
sync_pool $TESTPOOL

log_must zpool events -c
log_must zpool clear $TESTPOOL
log_must wait_vdev_state  $TESTPOOL draid1-0-0 "ONLINE" 60
log_must zpool wait -t resilver $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

nof_resilver=$(zpool events | grep -c resilver_start)
if [ $nof_resilver = 1 ] ; then
	log_must verify_pool $TESTPOOL
	log_pass "zpool clear on draid pool doesn't cause concurrent resilvers"
else
	log_fail "FAIL: sequential and healing resilver initiated concurrently"
fi
