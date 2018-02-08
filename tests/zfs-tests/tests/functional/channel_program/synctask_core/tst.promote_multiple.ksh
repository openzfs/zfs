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
#       Multiple interacting promotions in a single txg should succeed.
#

verify_runnable "global"

fs=$TESTPOOL/$TESTFS/testchild
clone1=$TESTPOOL/$TESTFS/testchild_clone1
clone2=$TESTPOOL/$TESTFS/testchild_clone2
snap1=$fs@testchild_snap1
snap2=$clone1@testchild_snap2

function cleanup
{
    for to_destroy in $fs $clone1 $clone2; do
        datasetexists $to_destroy && log_must zfs destroy -R $to_destroy
    done
}

log_onexit cleanup

#
# Create a chain of clones and snapshots:
#
# snap1 -----------> fs
#     \--> snap2 --> clone1
#              \---> clone2
#
# Then promote clone2 twice, resulting in:
#
# snap1 --> snap2 --> clone2
#     \         \---> clone1
#      \------------> fs
#
# We then attempt to destroy clone1, which should succeed since it no
# longer has any dependents.
#
log_must zfs create $fs
log_must zfs snapshot $snap1
log_must zfs clone $snap1 $clone1
log_must zfs snapshot $snap2
log_must zfs clone $snap2 $clone2

log_must zfs unmount -f $clone1

log_must_program_sync $TESTPOOL - <<-EOF
    assert(zfs.sync.promote("$clone2") == 0)
    assert(zfs.sync.promote("$clone2") == 0)
    assert(zfs.sync.destroy("$clone1") == 0)
EOF

log_pass "Multiple promotes and destroying a demoted fs in one txg works."
