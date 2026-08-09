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
# Copyright (c) 2016, loli10K. All rights reserved.
# Copyright (c) 2021, Pablo Correa Gómez. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_send/zfs_send.cfg

#
# DESCRIPTION:
#	Verify 'zfs send' will avoid sending replication send
#	streams when we're missing snapshots in the dataset
#	hierarchy, unless -s|--skip-missing provided
#
# STRATEGY:
#	1. Create a parent and child fs and then only snapshot the parent
#	2. Verify sending with replication will fail
#	3. Verify sending with skip-missing will print a warning but succeed
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAP && destroy_dataset $SNAP -f

	datasetexists $PARENT && destroy_dataset $PARENT -rf

	[[ -e $WARNF ]] && log_must rm -f $WARNF
}

log_assert "Verify 'zfs send -Rs' works as expected."
log_onexit cleanup

PARENT=$TESTPOOL/parent
CHILD=$PARENT/child
SNAP=$PARENT@snap
WARNF=$TEST_BASE_DIR/warn.2

log_note "Verify 'zfs send -R' fails to generate replication stream"\
	 " for datasets created before"

log_must zfs create $PARENT
log_must zfs create $CHILD
log_must zfs snapshot $SNAP
log_mustnot eval "zfs send -R $SNAP > /dev/null"

log_note "Verify 'zfs send -Rs' warns about missing snapshots, "\
	 "but still succeeds"

log_must eval "zfs send -Rs $SNAP 2> $WARNF > /dev/null"
log_must eval "[[ -s $WARNF ]]"

log_pass "Verify 'zfs send -Rs' works as expected."
