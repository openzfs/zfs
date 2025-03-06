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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#	Getting dataset mountpoint should work correctly.
#

verify_runnable "global"

fs=$TESTPOOL/testmount
snap=$fs@$TESTSNAP
clone=$TESTPOOL/$TESTCLONE
mnt1=/$fs/mnt1
mnt2=/$fs/mnt2

function cleanup
{
	destroy_dataset $clone
	destroy_dataset $fs "-R"
	log_must rm -rf $mnt1
	log_must rm -rf $mnt2
}

log_onexit cleanup

log_must zfs create $fs
create_snapshot $fs $TESTSNAP
create_clone $snap $clone

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$fs", "mountpoint")
        assert(ans == '/$fs')
        assert(src == 'default')
EOF

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$snap", "mountpoint")
	assert(ans == nil)
	assert(src == nil)
EOF

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$clone", "mountpoint")
	assert(ans == '/$clone')
	assert(src == 'default')
EOF

log_must mkdir $mnt1
log_must mkdir $mnt2

log_must zfs set mountpoint=$mnt1 $fs
log_must zfs set mountpoint=$mnt2 $clone

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$fs", "mountpoint")
	assert(ans == '$mnt1')
	assert(src == '$fs')
EOF

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$snap", "mountpoint")
	assert(ans == nil)
	assert(src == nil)
EOF

log_must_program $TESTPOOL - <<-EOF
	ans, src = zfs.get_prop("$clone", "mountpoint")
	assert(ans == '$mnt2')
	assert(src == '$clone')
EOF

log_pass "Getting dataset mountpoint should work correctly."
