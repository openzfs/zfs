#! /bin/ksh -p
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
# Copyright (c) 2023 by Pawel Jakub Dawidek
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "both"

verify_crossfs_block_cloning

function cleanup
{
	log_must zfs inherit compress $TESTSRCFS
	log_must zfs inherit compress $TESTDSTFS
}
log_onexit cleanup

log_assert "Verify block cloning properly clones small files (with embedded blocks) across datasets"

# Enable ZLE compression to make sure what is the maximum amount of data we
# can store in BP.
log_must zfs set compress=zle $TESTSRCFS
log_must zfs set compress=zle $TESTDSTFS

# Test BP_IS_EMBEDDED().
# Maximum embedded payload size is 112 bytes, but the buffer is extended to
# 512 bytes first and then compressed. 107 random bytes followed by 405 zeros
# gives exactly 112 bytes after compression with ZLE.
for filesize in 1 2 4 8 16 32 64 96 107; do
    bclone_test random $filesize true $TESTSRCDIR $TESTDSTDIR
done

log_pass
