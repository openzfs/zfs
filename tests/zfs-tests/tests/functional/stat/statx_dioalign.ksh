#!/bin/ksh -p
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
# Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
#

#
# Uses the statx helper to test the results of the STATX_DIOALIGN request as we
# manipulate DIO enable, dataset recordsize and file size and structure.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

if ! is_linux ; then
	log_unsupported "statx(2) only available on Linux"
fi

if [[ $(linux_version) -lt $(linux_version "6.1") ]] ; then
	log_unsupported "STATX_DIOALIGN not available before Linux 6.1"
fi

CLAIM="STATX_DIOALIGN returns useful values when Direct IO is available."

TESTDS=${TESTPOOL}/${TESTFS}
TESTFILE=${TESTDIR}/${TESTFILE0}

log_must save_tunable DIO_ENABLED
typeset recordsize_saved=$(get_prop recordsize $TESTDS)
typeset direct_saved=$(get_prop direct $TESTDS)

function cleanup
{
	rm -f ${TESTFILE}
	zfs set recordsize=$recordsize_saved $TESTDS
	zfs set direct=$direct_saved $TESTDS
	restore_tunable DIO_ENABLED
}
log_onexit cleanup

# assert_dioalign <file> <memalign> <ioalign>
function assert_dioalign
{
	typeset file=$1
	typeset -i memalign=$2
	typeset -i ioalign=$3

	typeset -a v=($(statx dioalign $file | cut -f2- -d' '))
	log_note "statx dioalign returned: $file: mem=${v[0]} io=${v[1]}"
	log_must [ ${v[0]} -eq $memalign -a ${v[1]} -eq $ioalign ]
}

# assert_dioalign_failed <file>
function assert_dioalign_failed
{
	typeset file=$1
	log_mustnot statx dioalign $file
}

log_assert $CLAIM

# The mem alignment will always be PAGE_SIZE, so we need to know what that is.
typeset -i PAGE_SIZE=$(getconf PAGE_SIZE)

# Set recordsize to 128K, and make a 64K file (so only one block) for the
# sizing tests below.
log_must zfs set recordsize=128K $TESTDS
log_must dd if=/dev/urandom of=$TESTFILE bs=64k count=1
log_must zpool sync

# when DIO is disabled via tunable, statx will not return the dioalign result
# and the program fails
log_must set_tunable32 DIO_ENABLED 0

for d in disabled standard always ; do
    log_must zfs set direct=$d $TESTDS
    assert_dioalign_failed $TESTFILE
done

# when DIO is enabled via tunable, behaviour is dependent on the direct=
# property.
log_must set_tunable32 DIO_ENABLED 1

# when DIO is disabled via property, statx fails
log_must zfs set direct=disabled $TESTDS
assert_dioalign_failed $TESTFILE

# when DIO is enabled, the result should be mem=pagesize, io=recordsize
for d in standard always ; do
    log_must zfs set direct=$d $TESTDS
    assert_dioalign $TESTFILE $PAGE_SIZE 131072
done

# The IO size is the file's blocksize, unless it is in its first block and
# could grow to the recordsize. Our test file is currently a single 64K block,
# so any recordsize equal or larger than that should be used for the alignment.
for krs in 64 128 256 512 ; do
    typeset -i rs=$((krs * 1024))
    log_must zfs set recordsize=$rs $TESTDS
    for d in standard always ; do
	log_must zfs set direct=$d $TESTDS
	assert_dioalign $TESTFILE $PAGE_SIZE $rs
    done
done

# If the recordsize is smaller than the block size, then the file's block size
# will always be used.
for krs in 4 8 16 32 64 ; do
    typeset -i rs=$((krs * 1024))
    log_must zfs set recordsize=$rs $TESTDS
    for d in standard always ; do
	log_must zfs set direct=$d $TESTDS
	assert_dioalign $TESTFILE $PAGE_SIZE 65536
    done
done

# Now we extend the file into its second block. This effectively locks in its
# block size, which will always be returned regardless of recordsize changes.
log_must zfs set recordsize=128K $TESTDS
log_must dd if=/dev/urandom of=$TESTFILE bs=192K count=1
log_must zpool sync

# Confirm that no matter how we change the recordsize, the alignment remains at
# the block size.
for krs in 4 8 16 32 64 128 256 512 ; do
    typeset -i rs=$((krs * 1024))
    log_must zfs set recordsize=$rs $TESTDS
    for d in standard always ; do
	log_must zfs set direct=$d $TESTDS
	assert_dioalign $TESTFILE $PAGE_SIZE 131072
    done
done

# reset for write tests
log_must zfs set recordsize=16K $TESTDS
log_must zfs set direct=standard $TESTDS

# create an empty file, and fetch its alignment (which we know, so just test
# for it). then, do some O_DIRECT writes with that alignment. they should
# succeed.
log_must rm -f $TESTFILE
log_must touch $TESTFILE
log_must zpool sync
assert_dioalign $TESTFILE $PAGE_SIZE 16384
log_must dd if=/dev/urandom of=$TESTFILE bs=16384 count=16 oflag=direct

# same again, but writing with incorrect alignment, which should fail.
log_must rm -f $TESTFILE
log_must touch $TESTFILE
log_must zpool sync
assert_dioalign $TESTFILE $PAGE_SIZE 16384
log_mustnot dd if=/dev/urandom of=$TESTFILE bs=1024 count=256 oflag=direct

log_pass $CLAIM
