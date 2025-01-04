#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#
# Copyright (c) 2020 by Mariusz Zaborski <oshogbo@FreeBSD.org>.

#
# DESCRIPTION:
# Verify 'zfs recv' can forcibly unmount filesystem while receiving
# stream.
#
# STRATEGY:
# 1. Create snapshot of file system
# 2. Make a zfs filesystem mountpoint busy
# 3. Receive filesystem with force flag.
# 4. Verify that stream was received or failed on Linux.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

verify_runnable "both"

function cleanup
{
	cd $curpath

	for snap in $init_snap $rst_snap; do
                snapexists $snap && \
                        destroy_snapshot $snap
        done

	datasetexists $rst_root && \
		destroy_dataset $rst_root

	for file in $full_bkup
	do
		[[ -e $file ]] && \
			log_must rm -f $file
	done

	[[ -d $TESTDIR1 ]] && \
		log_must rm -rf $TESTDIR1
}

log_assert "Verify 'zfs recv' can forcibly unmount busy filesystem."
log_onexit cleanup

curpath=`dirname $0`
init_snap=$TESTPOOL/$TESTFS@init_snap
full_bkup=$TEST_BASE_DIR/fullbkup.$$
rst_root=$TESTPOOL/rst_ctr
rst_snap=$rst_root@init_snap

log_note "Verify 'zfs recv' can forcible unmount busy filesystem."

# Preparation
log_must zfs create $rst_root
[[ ! -d $TESTDIR1 ]] && \
	log_must mkdir -p $TESTDIR1
log_must zfs set mountpoint=$TESTDIR1 $rst_root

log_must zfs snapshot $init_snap
log_must eval "zfs send $init_snap > $full_bkup"

# Test
log_must cd $TESTDIR1
if is_linux; then
    # Linux does not support it.
    log_mustnot zfs receive -MF $rst_snap < $full_bkup
else
    log_must zfs receive -MF $rst_snap < $full_bkup
fi

log_pass "The busy filesystem was unmounted or busy as expected."
