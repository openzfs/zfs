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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	zdb finds no leak in a pool with deferred data blocks.  A data
#	block that the copy cannot read is deferred, and the MOS keeps it
#	in a ZAP of its own until the record of its txg is released.  zdb
#	must count that ZAP as a referenced MOS object.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. With the copy stopped, write file b.  Export and import, so that
#	   the ARC does not hold b.  Stop the release, so that the deferred
#	   blocks stay in the MOS, and inject read errors on the data of b.
#	3. Restart the copy.  Wait until a block is deferred and the copy
#	   has passed every txg.  Clear the injection, and export.
#	4. Verify that the MOS directory has the deferred-block entry.
#	5. Verify that "zdb -bcc" finds no leaked space and no checksum
#	   error, and that the MOS leak check of "zdb -d" finds no leaked
#	   MOS object.  Verify that "zdb -m" prints a record, and the
#	   per-child entry of the lagging child with its lagging flag.
#	6. Import.  Verify that the async mirror resumes with the deferred
#	   blocks, and that the import refreshed the labels of the lagging
#	   child.  Restart the release, and wait until no block is deferred.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# Wait until the copy has deferred a block and has passed every txg.
#
# $1 timeout in seconds
#
function wait_async_mirror_deferred # [timeout]
{
	typeset -i timeout=${1:-$ASYNC_MIRROR_TIMEOUT}
	typeset -i i

	for (( i = 0; i < timeout; i++ )); do
		(( $(get_async_mirror_stat data_deferred) > 0 )) &&
		    [[ $(get_async_mirror_stat pending_txgs) == 0 ]] &&
		    return 0
		sleep 1
	done
	return 1
}

#
# Print the number of failed refreshes of the lagging child's labels in the
# debug log.
#
function label_refresh_failures
{
	kstat dbgmsg | awk '/async mirror: label refresh failed/ { n++ }
	    END { print n + 0 }'
}

#
# Succeed if the MOS directory of the exported test pool has the entry of
# the deferred blocks.
#
function has_deferred_entry
{
	zdb -e -p $ASYNC_MIRROR_DIR -dddd $TESTPOOL 1 | \
	    grep -q "org.openzfs:async_mirror_deferred"
}

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle

log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 1
async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 16
sync_pool $TESTPOOL
typeset digest=$(xxh128digest /$TESTPOOL/b)
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0

async_mirror_suspend_release 1
log_must zinject -a -t data -e io -T read /$TESTPOOL/b
async_mirror_suspend_copy 0
log_must wait_async_mirror_deferred
log_must zinject -c all
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
sync_pool $TESTPOOL
log_must zpool export $TESTPOOL
log_must has_deferred_entry

log_must eval "zdb -e -p $ASYNC_MIRROR_DIR -bcc $TESTPOOL > /dev/null"
typeset out
out=$(zdb -e -p $ASYNC_MIRROR_DIR -d $TESTPOOL 2>&1)
typeset -i rc=$?
echo "$out" | grep "MOS object" | while read line; do
	log_note "$line"
done
log_mustnot eval "echo \"\$out\" | grep -q 'MOS object'"
log_must test $rc -eq 0
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must eval "zdb -e -p $ASYNC_MIRROR_DIR -m $TESTPOOL | \
    grep -Eq '^[[:space:]]txg [0-9]+ space map [0-9]+ length'"
log_must eval "zdb -e -p $ASYNC_MIRROR_DIR -m $TESTPOOL | \
    grep -q '^Asynchronous Mirror Child $guid:'"
log_must async_mirror_child_has_flag lagging -e -p $ASYNC_MIRROR_DIR

typeset -i refresh_failed=$(label_refresh_failures)
async_mirror_import
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must test "$(get_async_mirror_stat data_deferred)" -gt 0
log_must test $(label_refresh_failures) -eq $refresh_failed
async_mirror_suspend_release 0
log_must wait_async_mirror_idle
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/b)"

log_pass "zdb finds no leak in a pool with deferred data blocks."
