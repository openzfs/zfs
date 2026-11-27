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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

#
# DESCRIPTION:
#	zfs send and receive keep pinned block sizes and directory
#	inheritance, in full and incremental streams.
#
# STRATEGY:
#	1. Pin a file and set inheritance on a directory, snapshot, send.
#	2. Check the received file and directory, and a new file there.
#	3. Add a file and a directory, send an incremental, check them.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset src=$TESTPOOL/$TESTFS/src
typeset dst=$TESTPOOL/$TESTFS/dst

function cleanup
{
	datasetexists $dst && destroy_dataset $dst -r
	datasetexists $src && destroy_dataset $src -r
}

log_onexit cleanup
log_assert "send and receive keep fixed block sizes and inheritance"

log_must zfs create $src
typeset s=$(get_prop mountpoint $src)
log_must touch $s/pinned
log_must extsize -s 4096 $s/pinned
write_mib $s/pinned
log_must mkdir $s/inh
log_must extsize -s 32768 $s/inh
write_mib $s/inh/f
log_must zfs snapshot $src@a
log_must eval "zfs send $src@a | zfs receive $dst"

typeset d=$(get_prop mountpoint $dst)
verify_ext $d/pinned extsize 4096
verify_blksz $d/pinned 4096
verify_ext $d/inh extszinherit 32768
verify_blksz $d/inh/f 32768
log_must zfs set readonly=off $dst
write_mib $d/inh/new
verify_blksz $d/inh/new 32768
log_must rm $d/inh/new
log_must zfs rollback $dst@a

log_must mkdir $s/inh/sub
write_mib $s/inh/sub/g
log_must zfs snapshot $src@b
log_must eval "zfs send -i a $src@b | zfs receive -F $dst"
verify_ext $d/inh/sub extszinherit 32768
verify_blksz $d/inh/sub/g 32768

log_pass "send and receive keep fixed block sizes and inheritance"
