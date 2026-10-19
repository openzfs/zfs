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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Pages cached through mmap() must not survive a rollback.
#
# STRATEGY:
#	1. Create a file and snapshot the filesystem.
#	2. Fault the file in through a mapping, then overwrite it so that the
#	   resident pages are refreshed with the new data.
#	3. Roll back to the snapshot.
#	4. Verify that both read() and mmap() return the snapshot contents.
#
#	On Linux zfs_rezget() did not drop the page cache, so the pages of a
#	file whose object, generation and size were unchanged kept serving
#	the pre-rollback data (openzfs #10931).
#

verify_runnable "global"

typeset fs=$TESTPOOL/$TESTFS
typeset snap=$fs@mmap_cached_rollback
typeset file=$TESTDIR/mmap_cached_rollback

function cleanup
{
	datasetexists $snap && destroy_dataset $snap
	rm -f $file
}

log_assert "A rollback must not leave stale mmap-cached pages behind"
log_onexit cleanup

log_must dd if=/dev/urandom of=$file bs=1M count=8
log_must zfs snapshot $snap
typeset expect=$(mmap_stale_check csum $file)

log_must mmap_stale_check touch $file
log_must dd if=/dev/urandom of=$file bs=1M count=8 conv=notrunc
[[ $(mmap_stale_check csum $file) != $expect ]] ||
    log_fail "overwriting the file did not change its contents"

log_must zfs rollback $snap

typeset got=$(mmap_stale_check csum $file)
[[ $got == $expect ]] ||
    log_fail "read() after rollback returned stale data ($got != $expect)"
got=$(mmap_stale_check msum $file)
[[ $got == $expect ]] ||
    log_fail "mmap() after rollback returned stale data ($got != $expect)"

log_pass "A rollback does not leave stale mmap-cached pages behind"
