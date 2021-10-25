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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#       zfs.exists should accurately report whether a dataset exists, and
#       report an error if a dataset is in another pool.

verify_runnable "global"

# create $TESTSNAP and $TESTCLONE
create_snapshot
create_clone

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS@$TESTSNAP && \
	    destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP -R
}

log_must_program $TESTPOOL $ZCP_ROOT/lua_core/tst.exists.zcp \
    $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS@$TESTSNAP \
    $TESTPOOL/$TESTCLONE

log_mustnot_checkerror_program "not in the target pool" \
    $TESTPOOL - <<-EOF
	return zfs.exists('rpool')
EOF

log_pass "zfs.exists() gives correct results"
