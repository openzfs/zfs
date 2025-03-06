#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2023 by Pawel Jakub Dawidek
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "both"

verify_block_cloning
verify_crossfs_block_cloning

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
