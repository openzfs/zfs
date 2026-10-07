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
# A pwritev2() with RWF_NOAPPEND on a file opened with O_APPEND must
# write at the offset it was given.
#
# STRATEGY:
# 1. Append blocks to a file opened with O_APPEND.
# 2. Write one byte with pwritev2(RWF_NOAPPEND) inside the file.
# 3. Verify the byte is at that offset and the file did not grow.
#

verify_runnable "global"

log_assert "pwritev2(RWF_NOAPPEND) writes at its offset on an O_APPEND file"

mntpt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
filename=$mntpt/noappend_file.txt
bs=131072

function cleanup
{
	rm -f $filename
}
log_onexit cleanup

file_append -f $filename -e $bs -b $bs -n 1 -N 3
ret=$?
if [[ $ret -eq 3 ]]; then
	log_unsupported "RWF_NOAPPEND is not supported here"
fi
log_must test $ret -eq 0

log_pass "pwritev2(RWF_NOAPPEND) writes at its offset on an O_APPEND file"
