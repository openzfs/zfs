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
. $STF_SUITE/tests/functional/bclone/bclone_corner_cases.kshlib

verify_runnable "both"

verify_crossfs_block_cloning

function cleanup
{
	log_must zfs inherit compress $TESTSRCFS
	log_must zfs inherit compress $TESTDSTFS
	log_must zfs inherit recordsize $TESTSRCFS
	log_must zfs inherit recordsize $TESTDSTFS
}
log_onexit cleanup

log_assert "Verify various corner cases in block cloning across datasets"

# Disable compression to make sure we won't use embedded blocks.
log_must zfs set compress=off $TESTSRCFS
log_must zfs set recordsize=$RECORDSIZE $TESTSRCFS
log_must zfs set compress=off $TESTDSTFS
log_must zfs set recordsize=$RECORDSIZE $TESTDSTFS

bclone_corner_cases_test $TESTSRCDIR $TESTDSTDIR

sync_pool $TESTPOOL
log_must zdb -b $TESTPOOL

log_pass
