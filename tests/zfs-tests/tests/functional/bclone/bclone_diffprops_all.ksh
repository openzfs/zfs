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
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "both"

verify_crossfs_block_cloning

save_tunable BCLONE_STRICT_PROPERTIES

function cleanup
{
	restore_tunable BCLONE_STRICT_PROPERTIES
	log_must zfs inherit checksum $TESTSRCFS
	log_must zfs inherit compress $TESTSRCFS
	log_must zfs inherit copies $TESTSRCFS
	log_must zfs inherit recordsize $TESTSRCFS
	log_must zfs inherit checksum $TESTDSTFS
	log_must zfs inherit compress $TESTDSTFS
	log_must zfs inherit copies $TESTDSTFS
	log_must zfs inherit recordsize $TESTDSTFS
}
log_onexit cleanup

log_assert "Verify block cloning across datasets with different properties"

# Disable strict property checking to allow cross-dataset cloning with different properties
log_must set_tunable32 BCLONE_STRICT_PROPERTIES 0

log_must zfs set checksum=off $TESTSRCFS
log_must zfs set compress=off $TESTSRCFS
log_must zfs set copies=1 $TESTSRCFS
log_must zfs set recordsize=131072 $TESTSRCFS
log_must zfs set checksum=fletcher2 $TESTDSTFS
log_must zfs set compress=lz4 $TESTDSTFS
log_must zfs set copies=3 $TESTDSTFS
log_must zfs set recordsize=8192 $TESTDSTFS

FILESIZE=$(random_int_between 2 32767)
FILESIZE=$((FILESIZE * 64))
bclone_test text $FILESIZE false $TESTSRCDIR $TESTDSTDIR

log_must zfs set checksum=sha256 $TESTSRCFS
log_must zfs set compress=zstd $TESTSRCFS
log_must zfs set copies=2 $TESTSRCFS
log_must zfs set recordsize=262144 $TESTSRCFS
log_must zfs set checksum=off $TESTDSTFS
log_must zfs set compress=off $TESTDSTFS
log_must zfs set copies=1 $TESTDSTFS
log_must zfs set recordsize=131072 $TESTDSTFS

FILESIZE=$(random_int_between 2 32767)
FILESIZE=$((FILESIZE * 64))
bclone_test text $FILESIZE false $TESTSRCDIR $TESTDSTDIR

log_must zfs set checksum=sha512 $TESTSRCFS
log_must zfs set compress=gzip $TESTSRCFS
log_must zfs set copies=2 $TESTSRCFS
log_must zfs set recordsize=512 $TESTSRCFS
log_must zfs set checksum=fletcher4 $TESTDSTFS
log_must zfs set compress=lzjb $TESTDSTFS
log_must zfs set copies=3 $TESTDSTFS
log_must zfs set recordsize=16384 $TESTDSTFS

FILESIZE=$(random_int_between 2 32767)
FILESIZE=$((FILESIZE * 64))
bclone_test text $FILESIZE false $TESTSRCDIR $TESTDSTDIR

log_pass
