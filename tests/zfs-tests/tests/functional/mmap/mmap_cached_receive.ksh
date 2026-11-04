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
#	Pages cached through mmap() must not survive a forced receive into a
#	mounted filesystem.
#
# STRATEGY:
#	1. Create a file, snapshot it and send it to a new filesystem.
#	2. Fault the received file in through a mapping.
#	3. Overwrite the file on the source, snapshot it again and do an
#	   incremental "zfs receive -F" into the still mounted destination.
#	4. Verify that both read() and mmap() on the destination return the
#	   new contents.
#
#	Replication does this to a replica that stays mounted. On Linux
#	zfs_rezget() did not drop the page cache, so the destination kept
#	serving the previous generation of the file (openzfs #10931).
#

verify_runnable "global"

typeset src=$TESTPOOL/$TESTFS
typeset dst=$TESTPOOL/mmap_cached_receive
typeset srcfile=$TESTDIR/mmap_cached_receive
typeset dstfile

function cleanup
{
	datasetexists $dst && destroy_dataset $dst "-r"
	datasetexists $src@a && destroy_dataset $src@a
	datasetexists $src@b && destroy_dataset $src@b
	rm -f $srcfile
}

log_assert "A forced receive must not leave stale mmap-cached pages behind"
log_onexit cleanup

log_must dd if=/dev/urandom of=$srcfile bs=1M count=8
log_must zfs snapshot $src@a
log_must eval "zfs send $src@a | zfs receive $dst"
log_must ismounted $dst
dstfile=$(get_prop mountpoint $dst)/mmap_cached_receive

log_must mmap_stale_check touch $dstfile

log_must dd if=/dev/urandom of=$srcfile bs=1M count=8 conv=notrunc
log_must zfs snapshot $src@b
log_must eval "zfs send -i $src@a $src@b | zfs receive -F $dst"

typeset expect=$(mmap_stale_check csum $srcfile)
typeset got=$(mmap_stale_check csum $dstfile)
[[ $got == $expect ]] ||
    log_fail "read() after receive returned stale data ($got != $expect)"
got=$(mmap_stale_check msum $dstfile)
[[ $got == $expect ]] ||
    log_fail "mmap() after receive returned stale data ($got != $expect)"

log_pass "A forced receive does not leave stale mmap-cached pages behind"
