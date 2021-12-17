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
