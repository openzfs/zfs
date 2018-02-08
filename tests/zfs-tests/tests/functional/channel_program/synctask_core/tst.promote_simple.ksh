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
# Copyright (c) 2016, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#       Promoting a clone should work correctly.
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
clone=$TESTPOOL/$TESTFS/testchild_clone
snap=$fs@$TESTSNAP

function cleanup
{
    for to_destroy in $fs $clone; do
        datasetexists $to_destroy && log_must zfs destroy -R $to_destroy
    done
}

log_onexit cleanup

log_must zfs create $fs
log_must zfs snapshot $snap
log_must zfs clone $snap $clone

log_must_program_sync $TESTPOOL - <<-EOF
    assert(zfs.sync.promote("$clone") == 0)
EOF

log_pass "Promoting a clone with a channel program works."
