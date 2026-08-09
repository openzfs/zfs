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

log_assert "Verify block cloning properly clones regular files across datasets"

# Disable compression to make sure we won't use embedded blocks.
log_must zfs set compress=off $TESTSRCFS
log_must zfs set compress=off $TESTDSTFS

for filesize in 1 107 113 511 512 513 4095 4096 4097 131071 131072 131073 \
  1048575 1048576 1048577 4194303 4194304 4194305; do
    bclone_test random $filesize false $TESTSRCDIR $TESTDSTDIR
done

sync_pool $TESTPOOL
log_must zdb -b $TESTPOOL

log_pass
