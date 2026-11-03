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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026 by Matthew Ahrens. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
# User, group and project accounting is correct after a raw or non-raw
# incremental receive that changes the owners and sizes of files, or frees
# them, in an encrypted dataset that already has accounting.
#
# STRATEGY:
# For a raw send, then a non-raw send:
# 1. Create an encrypted source with files owned by $QUSER1, snapshot it
#    and send it to the target
# 2. On the target, change owners, group and project, free a file,
#    rewrite a file larger, create a file, and snapshot
# 3. Receive the target's snapshot back into the source with recv -F,
#    unloading the source's key first for a raw send
# 4. Mount the source; verify that its user, group and project space
#    match the target's
# 5. Free a file whose owner changed on both datasets and verify that the
#    accounting still matches after a remount
#

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV $SRC_REPORT $TGT_REPORT
}

#
# Print one dataset's user, group and, where supported, project space as
# "<type> <name> <used> <objused>" lines.
#
function space_report
{
	typeset ds=$1

	zfs userspace -Hp -o type,name,used,objused $ds | tr ' ' '_'
	zfs groupspace -Hp -o type,name,used,objused $ds | tr ' ' '_'
	if is_linux; then
		zfs projectspace -Hp -o name,used,objused $ds | \
		    awk '{print "Project", $0}'
	fi
}

#
# Succeed if two space reports have the same entries and object counts,
# and space within a margin: a raw receive rebuilds indirect blocks, which
# may then compress to a different size.
#
function space_match
{
	awk -v margin=$((1024 * 1024)) '
	    NR == FNR { used[$1 " " $2] = $3; obj[$1 " " $2] = $4; next }
	    {
		k = $1 " " $2
		if (!(k in used) || obj[k] != $4) exit 1
		d = used[k] - $3
		if (d > margin || -d > margin) exit 1
		delete used[k]
	    }
	    END { for (k in used) exit 1 }' "$1" "$2"
}

#
# Wait for the source's accounting to match the target's, which it does
# once the upgrade started by mounting the source has finished.
#
function wait_space_match
{
	typeset i

	for i in {1..60}; do
		sync_pool $POOLNAME
		space_report $ENC_SOURCE > $SRC_REPORT 2>&1
		space_report $ENC_TARGET > $TGT_REPORT 2>&1
		space_match $SRC_REPORT $TGT_REPORT && return 0
		sleep 1
	done
	log_note "source:"
	cat $SRC_REPORT
	log_note "target:"
	cat $TGT_REPORT
	log_fail "Accounting of $ENC_SOURCE differs from $ENC_TARGET"
}

log_onexit cleanup

FILEDEV="$TEST_BASE_DIR/userspace_send_encrypted_changes"
POOLNAME="testpool$$"
ENC_SOURCE="$POOLNAME/source"
ENC_TARGET="$POOLNAME/target"
SRC_REPORT="$TEST_BASE_DIR/userspace_send_encrypted_changes.src"
TGT_REPORT="$TEST_BASE_DIR/userspace_send_encrypted_changes.tgt"

log_assert "Accounting is correct after an incremental receive of" \
	"changed files into an encrypted dataset with accounting."

#
# Run the strategy with "zfs send" options $1 ("-w" for a raw send).
#
function test_send
{
	typeset sendopts=$1
	typeset f srcmnt tgtmnt

	log_note "zfs send $sendopts"
	log_must eval "echo 'password' | zfs create -o encryption=on" \
	    "-o keyformat=passphrase -o keylocation=prompt $ENC_SOURCE"

	srcmnt=$(get_prop mountpoint $ENC_SOURCE)
	for f in chown chgrp project free rewrite keep; do
		log_must mkfile 4m $srcmnt/$f
	done
	log_must chown $QUSER1:$QGROUP $srcmnt/*
	sync_pool $POOLNAME
	log_must zfs snapshot $ENC_SOURCE@base

	log_must eval "zfs send $sendopts $ENC_SOURCE@base |" \
	    "zfs recv $ENC_TARGET"
	if [[ -n $sendopts ]]; then
		log_must eval "echo 'password' | zfs load-key $ENC_TARGET"
		log_must zfs mount $ENC_TARGET
	fi
	tgtmnt=$(get_prop mountpoint $ENC_TARGET)

	log_must chown $QUSER2 $tgtmnt/chown
	log_must chgrp 0 $tgtmnt/chgrp
	if is_linux; then
		log_must zfs project -p 101 $tgtmnt/project
	fi
	log_must rm $tgtmnt/free
	log_must dd if=/dev/urandom of=$tgtmnt/rewrite bs=1M count=8
	log_must mkfile 2m $tgtmnt/new
	log_must chown $QUSER2:$QGROUP $tgtmnt/new
	sync_pool $POOLNAME
	log_must zfs snapshot $ENC_TARGET@s1

	if [[ -n $sendopts ]]; then
		log_must zfs umount $ENC_SOURCE
		log_must zfs unload-key $ENC_SOURCE
	fi
	log_must eval "zfs send $sendopts -i @base $ENC_TARGET@s1 |" \
	    "zfs recv -F $ENC_SOURCE"
	if [[ -n $sendopts ]]; then
		log_must eval "echo 'password' | zfs load-key $ENC_SOURCE"
		log_must zfs mount $ENC_SOURCE
	fi
	wait_space_match

	log_must rm $srcmnt/chown $tgtmnt/chown
	log_must zfs umount $ENC_SOURCE
	log_must zfs mount $ENC_SOURCE
	wait_space_match

	log_must zfs destroy -r $ENC_SOURCE
	log_must zfs destroy -r $ENC_TARGET
}

truncate -s 1g $FILEDEV
log_must zpool create -O compression=off -o feature@encryption=enabled \
	$POOLNAME $FILEDEV

test_send "-w"
test_send ""

log_pass "Accounting is correct after an incremental receive of" \
	"changed files into an encrypted dataset with accounting."
