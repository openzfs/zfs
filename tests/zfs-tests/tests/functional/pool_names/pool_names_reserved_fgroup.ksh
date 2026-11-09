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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/tests/functional/pool_names/pool_names.kshlib

#
# DESCRIPTION:
# The dRAID failure group keywords fgroup, fdomain, failure_group and
# failure_domain are reserved as pool names, which earlier releases
# accepted.  New pools cannot take these names, but a pool that already has
# one must still import under it and stay usable.
#
# STRATEGY:
# For each name:
# 1. Verify that zpool create refuses it, and that zpool import refuses it
#    as a new name for an exported pool.
# 2. Create an exported pool with that name on disk, as an earlier release
#    would have.
# 3. Import the pool under its own name, check its status and name, clear
#    and scrub it, export it, import it again and destroy it.
#

verify_runnable "global"

DISKDIR=$TEST_BASE_DIR/pool_names_reserved
VDEV=$DISKDIR/vdev
VDEV2=$DISKDIR/vdev2
NAMES="fgroup fdomain failure_group failure_domain"

function cleanup
{
	typeset name

	for name in $NAMES; do
		poolexists $name && destroy_pool $name
		poolexists x${name#?} && destroy_pool x${name#?}
	done
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $DISKDIR
}

log_assert "A pool named after a failure group keyword stays usable"
log_onexit cleanup

log_must mkdir -p $DISKDIR
log_must truncate -s $MINVDEVSIZE $VDEV2
log_must zpool create -m none $TESTPOOL1 $VDEV2
log_must zpool export $TESTPOOL1

for name in $NAMES; do
	log_must truncate -s $MINVDEVSIZE $VDEV
	log_mustnot zpool create -m none $name $VDEV
	log_mustnot zpool import -d $VDEV2 $TESTPOOL1 $name
	log_mustnot poolexists $name

	create_exported_pool_named $name $VDEV
	log_must zpool import -d $VDEV $name
	log_must zpool status $name
	log_must test "$(get_pool_prop name $name)" = "$name"
	log_must zpool clear $name
	log_must zpool scrub -w $name
	log_must zpool export $name
	log_must zpool import -d $VDEV $name
	log_must zpool destroy $name
	log_must rm -f $VDEV
done

log_pass "A pool named after a failure group keyword stays usable"
