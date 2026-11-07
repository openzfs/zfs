#!/bin/ksh
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

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Bookmarking a received redacted snapshot creates a redaction bookmark,
# which needs the redaction_bookmarks feature.  Verify that it is refused
# when the feature is not enabled, instead of panicking the host.
#
# Strategy:
# 1. Create a redacted send stream.
# 2. Receive it into a pool with only bookmarks and redacted_datasets
#    enabled.
# 3. Verify that bookmarking the received snapshot fails and leaves
#    redaction_bookmarks disabled.
# 4. Enable redaction_bookmarks and verify that the bookmark is created.
#

typeset ds_name="bookmark_feature"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name ''

function cleanup
{
	destroy_pool $POOL2
	create_pool $POOL2 $DISK2
	log_must zfs snapshot $POOL2@init
	redacted_cleanup $sendfs
}

log_onexit cleanup

log_must zfs redact $sendfs@snap book1 $clone@snap
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"

destroy_pool $POOL2
log_must zpool create -d $POOL2 $DISK2
log_must zpool set feature@bookmarks=enabled $POOL2
log_must zpool set feature@redacted_datasets=enabled $POOL2
log_must eval "zfs recv $recvfs <$stream"
log_must test "$(get_pool_prop feature@redaction_bookmarks $POOL2)" = \
    "disabled"

log_mustnot zfs bookmark $recvfs@snap $recvfs#book
log_must zpool sync $POOL2
log_mustnot bkmarkexists $recvfs#book
log_must test "$(get_pool_prop feature@redaction_bookmarks $POOL2)" = \
    "disabled"

log_must zpool set feature@redaction_bookmarks=enabled $POOL2
log_must zfs bookmark $recvfs@snap $recvfs#book
log_must bkmarkexists $recvfs#book
log_must test "$(get_pool_prop feature@redaction_bookmarks $POOL2)" = \
    "active"

log_pass "Bookmarking a redacted snapshot requires redaction_bookmarks."
