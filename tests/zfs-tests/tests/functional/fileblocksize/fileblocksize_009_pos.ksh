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
#	zfs fileblocksize lists, sets, checks and clears block size settings.
#
# STRATEGY:
#	1. Set a size on a tree with -r and check that empty files and
#	   subdirectories got it and a file with data was left alone.
#	2. Check the listing, -d, and that new files inherit the size.
#	3. Check -c finds the file with data, with newline and NUL output,
#	   and against another size with -b.
#	4. Check that setting a file with data or a bad size fails.
#	5. Clear with -C -r, and check project IDs are left alone throughout.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset t=$TESTDIR/cli

function cleanup
{
	rm -rf $t
}

log_onexit cleanup
log_assert "zfs fileblocksize lists, sets, checks and clears block sizes"

log_must mkdir -p $t/sub/deeper
log_must touch $t/empty $t/sub/empty
write_mib $t/full
log_must zfs project -p 7 -s $t

log_must zfs fileblocksize -b 16K -r $t
verify_ext $t extszinherit 16384
verify_ext $t/sub extszinherit 16384
verify_ext $t/sub/deeper extszinherit 16384
verify_ext $t/empty extsize 16384
verify_ext $t/sub/empty extsize 16384
verify_ext $t/full none 0

typeset out
out=$(zfs fileblocksize -d $t)
[[ "$out" == *"16384 I $t" ]] || log_fail "-d listing: '$out'"
out=$(zfs fileblocksize $t | grep "/full$")
[[ "$out" == *"0 - $t/full" ]] || log_fail "listing: '$out'"
(( $(zfs fileblocksize -r $t | wc -l) == 6 )) ||
    log_fail "recursive listing: $(zfs fileblocksize -r $t)"

write_mib $t/sub/new
verify_blksz $t/sub/new 16384

out=$(zfs fileblocksize -c -r $t)
[[ "$out" == "$t/full - block size is not set properly (0/16384)" ]] ||
    log_fail "check: '$out'"
out=$(zfs fileblocksize -c -0 -r $t | tr '\0' '|')
[[ "$out" == "$t/full|" ]] || log_fail "check -0: '$out'"
(( $(zfs fileblocksize -c -r -b 8K $t | wc -l) == 7 )) ||
    log_fail "check -b: $(zfs fileblocksize -c -r -b 8K $t)"

log_mustnot zfs fileblocksize -b 8K $t/full
log_mustnot zfs fileblocksize -b 3000 $t
log_mustnot zfs fileblocksize -b 256K $t
log_mustnot zfs fileblocksize -C -b 8K $t
log_mustnot zfs fileblocksize -d -b 8K $t
log_mustnot zfs fileblocksize -0 $t
log_mustnot zfs fileblocksize -r $t/empty

log_must zfs fileblocksize -C -r $t
for f in $t $t/sub $t/sub/deeper $t/empty $t/sub/empty $t/sub/new; do
	verify_ext $f none 0
done
[[ -z "$(zfs fileblocksize -c -r $t)" ]] ||
    log_fail "check after clear: $(zfs fileblocksize -c -r $t)"
out=$(zfs project -d $t)
[[ "$out" == *"7 P $t" ]] || log_fail "project ID changed: '$out'"

log_pass "zfs fileblocksize lists, sets, checks and clears block sizes"
