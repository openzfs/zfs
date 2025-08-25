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
# Copyright (c) 2021 by Nutanix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/properties.shlib

#
# Description:
# Verify that longname featureflag is present in the stream.
#
# Strategy:
# 1. Create a filesystem with longnamed files/directories.
# 2. Verify that the sendstream has the longname featureflag is present in the
#    send stream.
# 3. Verify the created streams can be received correctly.
# 4. Verify that the longnamed files/directories are present in the received
#    filesystem.
#

verify_runnable "both"

log_assert "Verify that longnames are handled correctly in send stream."
log_onexit cleanup_pool $POOL $POOL2 $POOL3

typeset sendfs=$POOL/sendfs
typeset recvfs=$POOL2/recvfs
typeset recvfs3=$POOL3/recvfs
typeset stream=$BACKDIR/stream
typeset dump=$BACKDIR/dump

log_must zfs create -o longname=on $sendfs
typeset dir=$(get_prop mountpoint $sendfs)

# Create a longnamed dir and a file in the send dataset
LONGNAME=$(printf 'a%.0s' {1..512})
LONGFNAME="file-$LONGNAME"
LONGDNAME="dir-$LONGNAME"
log_must mkdir $dir/$LONGDNAME
log_must touch $dir/$LONGFNAME

# When POOL3 is created by rsend.kshlib feature@longname is 'enabled'.
# Recreate the POOL3 with feature@longname disabled.
datasetexists $POOL3 && log_must zpool destroy $POOL3
log_must zpool create -o feature@longname=disabled $POOL3 $DISK3

# Generate the streams and zstreamdump output.
log_must zfs snapshot $sendfs@now
log_must eval "zfs send -p $sendfs@now >$stream"
log_must eval "zstream dump -v <$stream >$dump"
log_must eval "zfs recv $recvfs <$stream"
cmp_ds_cont $sendfs $recvfs
log_must stream_has_features $stream longname

# Ensure the the receiving pool has feature@longname activated after receiving.
feat_val=$(zpool get -H -o value feature@longname $POOL2)
log_note "Zpool $POOL2 feature@longname=$feat_val"
if [[ "$feat_val" != "active" ]]; then
	log_fail "pool $POOL2 feature@longname=$feat_val (expected 'active')"
fi

# Receiving of the stream on $POOL3 should fail as longname is not enabled
log_mustnot eval "zfs recv $recvfs3 <$stream"

# Enable feature@longname and retry the receiving the stream.
# It should succeed this time.
log_must eval "zpool set feature@longname=enabled $POOL3"
log_must eval "zfs recv $recvfs3 <$stream"

log_must zfs get longname $recvfs3
prop_val=$(zfs get -H -o value longname $recvfs3)
log_note "dataset $recvfs3 has longname=$prop_val"
if [[ "$prop_val" != "on" ]]; then
	log_fail "$recvfs3 has longname=$prop_val (expected 'on')"
fi

#
# TODO:
# - Add a testcase to cover the case where send-stream does not contain
#   properties (generated without "-p").
#   In this case the target dataset would have longname files/directories which
#   cannot be accessed if the dataset property 'longname=off'.
#

log_pass "Longnames are handled correctly in send/recv"
