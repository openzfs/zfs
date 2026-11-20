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
# A file with the DOS read-only attribute can't be opened for writing,
# but a descriptor opened for writing before the attribute was set stays
# writable (openzfs/zfs#16508).
#
# STRATEGY:
# 1. Create a file and open it for writing on a descriptor.
# 2. Set the DOS read-only attribute.
# 3. Opening the file for writing must fail; reading it must work.
# 4. Writing through the descriptor opened before must work.
# 5. Clear the attribute; opening for writing must work again.
#

verify_runnable "global"

typeset file="$TESTDIR/dos_readonly_open.txt"

function cleanup
{
	exec 3>&-
	[[ -e $file ]] && write_dos_attributes noreadonly $file
	rm -f $file
}

log_onexit cleanup

log_assert "The DOS read-only attribute is enforced when opening for write"

log_must eval "echo 'before' > $file"
exec 3>>$file
log_must write_dos_attributes readonly $file

log_mustnot eval "echo 'open for write' >> $file"
log_mustnot eval "echo 'open for write' > $file"
log_must cat $file
log_must eval "echo 'through the old descriptor' >&3"
log_must grep -q "through the old descriptor" $file

log_must write_dos_attributes noreadonly $file
log_must eval "echo 'after' >> $file"

log_pass "The DOS read-only attribute is enforced when opening for write"
