#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg

#
# DESCRIPTION:
# 'zpool split' should use the provided devices to split the pool
#
# STRATEGY:
# 1. Create various (mirror-only) pools
# 2. Verify 'zpool split' can provide a list of devices to be included in the
#    new pool. At most one disk from each mirror can be specified.
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	rm -fd $FILEDEV_PREFIX* $altroot
}

function setup_mirror # <conf>
{
	for filedev in "${fd[@]}"; do
		truncate -s $SPA_MINDEVSIZE "$filedev"
	done
	log_must zpool create -f $TESTPOOL $conf
}

log_assert "'zpool split' should use the provided devices to split the pool"
log_onexit cleanup

typeset altroot="$TESTDIR/altroot-$TESTPOOL2"
typeset FILEDEV_PREFIX="$TEST_BASE_DIR/filedev"
typeset -A fd
fd[01]="$FILEDEV_PREFIX-01"
fd[02]="$FILEDEV_PREFIX-02"
fd[03]="$FILEDEV_PREFIX-03"
fd[11]="$FILEDEV_PREFIX-11"
fd[12]="$FILEDEV_PREFIX-12"
fd[13]="$FILEDEV_PREFIX-13"

# Base pool configurations
typeset poolconfs=("mirror ${fd[01]} ${fd[02]}"
    "mirror ${fd[01]} ${fd[02]} ${fd[03]}"
    "mirror ${fd[01]} ${fd[02]} mirror ${fd[11]} ${fd[12]}"
    "mirror ${fd[01]} ${fd[02]} ${fd[03]} mirror ${fd[11]} ${fd[12]}"
    "mirror ${fd[01]} ${fd[02]} mirror ${fd[11]} ${fd[12]} ${fd[13]}"
    "mirror ${fd[01]} ${fd[02]} ${fd[03]} mirror ${fd[11]} ${fd[12]} ${fd[13]}"
)
# "good" device specifications
typeset gooddevs=("${fd[01]}"
    "${fd[02]}"
    "${fd[02]} ${fd[11]}"
    "${fd[12]}"
    "${fd[02]}"
    "${fd[03]} ${fd[12]}"
)
# "bad" device specifications
typeset baddevs=("${fd[01]} ${fd[02]}"
    "${fd[02]} ${fd[03]}"
    "${fd[02]} baddev"
    "baddev ${fd[11]}"
    "${fd[11]} ${fd[12]} ${fd[13]}"
    "${fd[01]} ${fd[02]} ${fd[13]}"
)

typeset -i i=0;
while [ $i -lt "${#poolconfs[@]}" ]
do
	typeset conf=${poolconfs[$i]}
	setup_mirror $conf
	log_mustnot zpool split $TESTPOOL $TESTPOOL2 ${baddevs[$i]}
	log_must zpool split -R $altroot $TESTPOOL $TESTPOOL2 ${gooddevs[$i]}
	# Verify "good" devices ended up in the new pool
	log_must poolexists $TESTPOOL2
	for filedev in ${gooddevs[$i]}; do
		log_must check_vdev_state $TESTPOOL2 $filedev "ONLINE"
	done
	cleanup
	((i = i + 1))
done

log_pass "'zpool split' can use the provided devices to split the pool"
