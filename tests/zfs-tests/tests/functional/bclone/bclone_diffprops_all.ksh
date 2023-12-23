#! /bin/ksh -p
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
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "both"

verify_block_cloning
verify_crossfs_block_cloning

log_assert "Verify block cloning across datasets with different properties"

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

log_must zfs inherit checksum $TESTSRCFS
log_must zfs inherit compress $TESTSRCFS
log_must zfs inherit copies $TESTSRCFS
log_must zfs inherit recordsize $TESTSRCFS
log_must zfs inherit checksum $TESTDSTFS
log_must zfs inherit compress $TESTDSTFS
log_must zfs inherit copies $TESTDSTFS
log_must zfs inherit recordsize $TESTDSTFS

log_pass
