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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify the functionality of the redaction_bookmarks and redacted_datasets
# features.
#
# Strategy:
# 1. Create a pool with all features disabled.
# 2. Verify redacted send fails.
# 3. Enable redaction_bookmarks and verify redacted sends works.
# 4. Verify receipt of a redacted stream fails.
# 5. Enable recacted_datasets and verify zfs receive works.
#

typeset ds_name="disabled"
typeset sendfs="$POOL/$ds_name"
typeset sendfs1="$POOL2/${ds_name}1"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset clone1="$POOL2/${ds_name}_clone1"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name ''

function cleanup
{
	destroy_pool $POOL2
	create_pool $POOL2 $DISK2
	log_must zfs snapshot $POOL2@init
	redacted_cleanup $sendfs $recvfs
}

log_onexit cleanup

destroy_pool $POOL2
log_must zpool create -d $POOL2 $DISK2

log_must zfs create $sendfs1
log_must zfs snapshot $sendfs1@snap
log_must zfs clone $sendfs1@snap $clone1
log_must zfs snapshot $clone1@snap

log_mustnot zfs redact $sendfs1@snap book1 $clone1@snap
log_must zpool set feature@redaction_bookmarks=enabled $POOL2
log_must zfs redact $sendfs1@snap book1 $clone1@snap

log_must zfs redact $sendfs@snap book1 $clone@snap
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
log_mustnot eval "zfs recv $recvfs <$stream"
log_must zpool set feature@redacted_datasets=enabled $POOL2
log_must eval "zfs recv $recvfs <$stream"

log_pass "The redacted send/recv features work correctly."
