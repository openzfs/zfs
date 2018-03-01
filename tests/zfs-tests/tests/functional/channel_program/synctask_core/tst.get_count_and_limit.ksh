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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Getting filesystem and snapshot count/limit props should work correctly.
#

verify_runnable "global"

fs=$TESTPOOL/testchild
snap=$fs@$TESTSNAP

function cleanup
{
	datasetexists $fs && log_must zfs destroy -R $fs
	log_must rm -rf $fs/foo
	log_must rm -rf $fs/bar
}

log_onexit cleanup

log_must zfs create $fs
log_must zfs create $fs/foo
create_snapshot $fs $TESTSNAP

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$fs", "snapshot_limit")
	assert(ans == -1)
	assert(src == 'default')

	ans, src = zfs.get_prop("$fs", "snapshot_count")
	assert(ans == nil)
	assert(src == nil)
EOF

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$fs", "filesystem_limit")
	assert(ans == -1)
	assert(src == 'default')

	ans, src = zfs.get_prop("$fs", "filesystem_count")
	assert(ans == nil)
	assert(src == nil)
EOF

log_must zfs set snapshot_limit=10 $fs

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$fs", "snapshot_limit")
	assert(ans == 10)
	assert(src == '$fs')

	ans, src = zfs.get_prop("$fs", "snapshot_count")
	assert(ans == 1)
	assert(src == nil)
EOF

log_must zfs set filesystem_limit=8 $fs

log_must zfs create $fs/bar

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$fs", "filesystem_limit")
	assert(ans == 8)
	assert(src == '$fs')

	ans, src = zfs.get_prop("$fs", "filesystem_count")
	assert(ans == 2)
	assert(src == nil)
EOF

log_pass "Getting filesystem and snapshot count/limit props should work correctly."
