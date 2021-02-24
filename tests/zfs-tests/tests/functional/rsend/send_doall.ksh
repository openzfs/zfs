#!/bin/ksh

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
# Copyright (c) 2019 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify send_doall stream is properly received
#
# Strategy:
# 1) Create a set of snapshots.
# 2) Send these snapshots (from origin to the last one) to a file using send_doall.
# 3) Receive the file to newfs to test if the stream is properly handled.
#

verify_runnable "both"

log_assert "Verify send_doall stream is correct"

function cleanup
{
	rm -f $BACKDIR/fs@*
	destroy_dataset $POOL/fs "-rR"
	destroy_dataset $POOL/newfs "-rR"
}

log_onexit cleanup

log_must zfs create $POOL/fs
log_must zfs create $POOL/fs/child

# Create 3 files and a snapshot between each file creation.
for i in {1..3}; do
	file="/$POOL/fs/file$i"
	log_must mkfile 16384 $file

	file="/$POOL/fs/child/file$i"
	log_must mkfile 16384 $file

	log_must zfs snapshot -r $POOL/fs@snap$i
done

# Snapshot the pool and send it to the new dataset.
log_must eval "send_doall $POOL/fs@snap3 >$BACKDIR/fs@snap3"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs@snap3"

zfs list $POOL/newfs/child
if [[ $? -eq 0 ]]; then
	log_fail "Children dataset should not have been received"
fi

log_pass "Verify send_doall stream is correct"
