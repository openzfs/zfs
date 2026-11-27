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
#	The fileblocksize property gates new extent size settings and
#	directory inheritance.
#
# STRATEGY:
#	1. Check the default (on) and that child datasets inherit the value.
#	2. With fileblocksize=off, new settings fail with EOPNOTSUPP and
#	   directories don't pass theirs on.
#	3. Files already pinned keep their block size, and settings can still
#	   be cleared.
#	4. Setting it back on restores inheritance.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset fs=$TESTPOOL/$TESTFS

function cleanup
{
	zfs inherit fileblocksize $fs
	datasetexists $fs/child && destroy_dataset $fs/child
	rm -rf $TESTDIR/inh $TESTDIR/empty
}

log_onexit cleanup
log_assert "fileblocksize=off refuses new settings and stops inheritance"

[[ "$(get_prop fileblocksize $fs)" == "on" ]] ||
    log_fail "fileblocksize default is not on"
log_must zfs set fileblocksize=off $fs
log_must zfs create $fs/child
[[ "$(get_prop fileblocksize $fs/child)" == "off" ]] ||
    log_fail "child dataset doesn't inherit fileblocksize"
log_must zfs set fileblocksize=on $fs

log_must mkdir $TESTDIR/inh
log_must extsize -s 8192 $TESTDIR/inh
write_mib $TESTDIR/inh/before
log_must touch $TESTDIR/empty $TESTDIR/inh/emptypin

log_must zfs set fileblocksize=off $fs
extsize_fails EOPNOTSUPP -s 4096 $TESTDIR/empty
log_must mkdir $TESTDIR/inh/newdir
extsize_fails EOPNOTSUPP -s 4096 $TESTDIR/inh/newdir
verify_ext $TESTDIR/inh/newdir none 0
write_mib $TESTDIR/inh/during
verify_ext $TESTDIR/inh/during none 0
verify_blksz $TESTDIR/inh/during 131072
log_must dd if=/dev/urandom of=$TESTDIR/inh/before bs=128k count=8 seek=8 \
    conv=notrunc 2>/dev/null
verify_blksz $TESTDIR/inh/before 8192
# the same value is not a change; clearing is always allowed
log_must extsize -s 8192 $TESTDIR/inh/before
log_must extsize -c $TESTDIR/inh/emptypin
verify_ext $TESTDIR/inh/emptypin none 0
verify_ext $TESTDIR/inh extszinherit 8192

log_must zfs set fileblocksize=on $fs
write_mib $TESTDIR/inh/after
verify_blksz $TESTDIR/inh/after 8192

log_pass "fileblocksize=off refuses new settings and stops inheritance"
