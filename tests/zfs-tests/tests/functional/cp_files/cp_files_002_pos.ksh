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
# Copyright (c) 2024 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

#
# DESCRIPTION:
# Verify all cp --reflink modes work with modified file.
#
# STRATEGY:
# 1. Verify "cp --reflink=never|auto|always" behaves as expected.
#    Two different modes of operation are tested.
#
#    a. zfs_bclone_wait_dirty=0: FICLONE and FICLONERANGE fail with EINVAL
#       when there are dirty blocks which cannot be immediately cloned.
#       This is the default behavior.
#
#    b. zfs_bclone_wait_dirty=1: FICLONE and FICLONERANGE wait for
#       dirty blocks to be written to disk allowing the clone to succeed.
#       The downside to this is it may be slow which depending on the
#       situtation may defeat the point of making a clone.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "cp --reflink is a GNU coreutils option"
fi

function cleanup
{
	datasetexists $TESTPOOL/cp-reflink && \
	    destroy_dataset $$TESTPOOL/cp-reflink -f
	log_must restore_tunable BCLONE_WAIT_DIRTY
}

function verify_copy
{
	src_cksum=$(xxh128digest $1)
	dst_cksum=$(xxh128digest $2)

	if [[ "$src_cksum" != "$dst_cksum" ]]; then
		log_must ls -l $CP_TESTDIR
		log_fail "checksum mismatch ($src_cksum != $dst_cksum)"
	fi
}

log_assert "Verify all cp --reflink modes work with modified file"

log_onexit cleanup

SRC_FILE=src.data
DST_FILE=dst.data
SRC_SIZE=$((1024 + $RANDOM % 1024))

# A smaller recordsize is used merely to speed up the test.
RECORDSIZE=4096

log_must save_tunable BCLONE_WAIT_DIRTY

log_must zfs create -o recordsize=$RECORDSIZE $TESTPOOL/cp-reflink
CP_TESTDIR=$(get_prop mountpoint $TESTPOOL/cp-reflink)

log_must cd $CP_TESTDIR

# Never wait on dirty blocks (zfs_bclone_wait_dirty=0)
log_must set_tunable32 BCLONE_WAIT_DIRTY 0

for mode in "never" "auto" "always"; do
	log_note "Checking 'cp --reflink=$mode'"

	# Create a new file and immediately copy it.
	log_must dd if=/dev/urandom of=$SRC_FILE bs=$RECORDSIZE count=$SRC_SIZE

	if [[ "$mode" == "always" ]]; then
		log_mustnot cp --reflink=$mode $SRC_FILE $DST_FILE
		log_must ls -l $CP_TESTDIR
	else
		log_must cp --reflink=$mode $SRC_FILE $DST_FILE
		verify_copy $SRC_FILE $DST_FILE
	fi
	log_must rm -f $DST_FILE

	# Append to an existing file and immediately copy it.
	sync_pool $TESTPOOL
	log_must dd if=/dev/urandom of=$SRC_FILE bs=$RECORDSIZE seek=$SRC_SIZE \
	    count=1 conv=notrunc
	if [[ "$mode" == "always" ]]; then
		log_mustnot cp --reflink=$mode $SRC_FILE $DST_FILE
		log_must ls -l $CP_TESTDIR
	else
		log_must cp --reflink=$mode $SRC_FILE $DST_FILE
		verify_copy $SRC_FILE $DST_FILE
	fi
	log_must rm -f $DST_FILE

	# Overwrite a random range of an existing file and immediately copy it.
	sync_pool $TESTPOOL
	log_must dd if=/dev/urandom of=$SRC_FILE bs=$((RECORDSIZE / 2)) \
            seek=$(($RANDOM % $SRC_SIZE)) count=$((1 + $RANDOM % 16)) conv=notrunc
	if [[ "$mode" == "always" ]]; then
		log_mustnot cp --reflink=$mode $SRC_FILE $DST_FILE
		log_must ls -l $CP_TESTDIR
	else
		log_must cp --reflink=$mode $SRC_FILE $DST_FILE
		verify_copy $SRC_FILE $DST_FILE
	fi
	log_must rm -f $SRC_FILE $DST_FILE
done

# Wait on dirty blocks (zfs_bclone_wait_dirty=1)
log_must set_tunable32 BCLONE_WAIT_DIRTY 1

for mode in "never" "auto" "always"; do
	log_note "Checking 'cp --reflink=$mode'"

	# Create a new file and immediately copy it.
	log_must dd if=/dev/urandom of=$SRC_FILE bs=$RECORDSIZE count=$SRC_SIZE
	log_must cp --reflink=$mode $SRC_FILE $DST_FILE
	verify_copy $SRC_FILE $DST_FILE
	log_must rm -f $DST_FILE

	# Append to an existing file and immediately copy it.
	log_must dd if=/dev/urandom of=$SRC_FILE bs=$RECORDSIZE seek=$SRC_SIZE \
	    count=1 conv=notrunc
	log_must cp --reflink=$mode $SRC_FILE $DST_FILE
	verify_copy $SRC_FILE $DST_FILE
	log_must rm -f $DST_FILE

	# Overwrite a random range of an existing file and immediately copy it.
	log_must dd if=/dev/urandom of=$SRC_FILE bs=$((RECORDSIZE / 2)) \
            seek=$(($RANDOM % $SRC_SIZE)) count=$((1 + $RANDOM % 16)) conv=notrunc
	log_must cp --reflink=$mode $SRC_FILE $DST_FILE
	verify_copy $SRC_FILE $DST_FILE
	log_must rm -f $SRC_FILE $DST_FILE
done

log_pass
