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
# Copyright (c) 2026 Tigerblue77
#

#
# DESCRIPTION:
#	'zinject -r' takes a byte range of an object, has the kernel
#	translate it into block IDs, and injects errors in that range only.
#
# STRATEGY:
#	1. Write a file of 8 records of 128K.
#	2. Inject I/O errors over the bytes of record 1.
#	3. Check that the handler is listed with level 0 and range [1, 1].
#	4. Check that record 1 fails to read and that records 0 and 2 read.
#	5. Check that a start offset alone gives the same range.
#	6. Check that the same bytes at level 1 give the range [0, 0].
#	7. Check that a level the object does not have is refused.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "zinject -r injects errors over a byte range of an object."

TESTFILE=/$TESTPOOL/zinject_range.file

function cleanup
{
	zinject -c all
	default_cleanup_noexit
}

log_onexit cleanup

# A data handler is listed as
#	ID POOL OBJSET OBJECT TYPE LVL DVAs RANGE MATCH INJECT
# where RANGE is "[start, end]": two fields.  One handler is registered
# at a time.
function verify_handler
{
	typeset expected="$1"
	typeset got

	got=$(zinject | awk '/^ *[0-9]/ { print $6, $8, $9 }')
	[[ "$got" == "$expected" ]] || \
	    log_fail "expected level and range '$expected', got '$got'"
}

function inject_count
{
	zinject | awk '/^ *[0-9]/ { print $NF }'
}

function read_record
{
	dd if=$TESTFILE of=/dev/null bs=128k skip=$1 count=1
}

default_mirror_setup_noexit $DISKS

# Without a cache, every read below goes through the ZIO layer.
log_must zfs set primarycache=none $TESTPOOL
log_must zfs set recordsize=128k $TESTPOOL
log_must dd if=/dev/urandom of=$TESTFILE bs=128k count=8
log_must zpool sync $TESTPOOL

log_must zinject -t data -e io -r 131072,262143 $TESTFILE
verify_handler "0 [1, 1]"

log_must read_record 0
log_mustnot read_record 1
log_must read_record 2

typeset -i injected=$(inject_count)
(( injected > 0 )) || log_fail "no error was injected (inject=$injected)"
log_must zinject -c all

log_must read_record 1

log_must zinject -t data -e io -r 131072 $TESTFILE
verify_handler "0 [1, 1]"
log_must zinject -c all

# The 8 records are below a single level 1 block.
log_must zinject -t data -e io -l 1 -r 131072,262143 $TESTFILE
verify_handler "1 [0, 0]"
log_must zinject -c all

log_mustnot_expect "block level exceeds max level of object" \
    zinject -t data -e io -l 2 -r 131072,262143 $TESTFILE

log_pass "zinject -r injects errors over a byte range of an object."
