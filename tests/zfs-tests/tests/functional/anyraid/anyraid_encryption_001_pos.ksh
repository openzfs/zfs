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
# Copyright (c) 2026, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify AnyRAID works correctly with ZFS native encryption.
# Create an anymirror1 pool, create an encrypted dataset, write data,
# export/import, load the key, and verify data integrity.
# This test is self-contained and does not depend on any other test.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 3 disks.
# 2. Create an encrypted dataset using a passphrase.
# 3. Write data and record xxh128 checksums.
# 4. Export the pool.
# 5. Import the pool and load the encryption key.
# 6. Verify all data checksums match.
# 7. Run scrub, verify no errors.
#

verify_runnable "global"

PASSPHRASE="testpassword123"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_assert "AnyRAID with native encryption preserves data across export/import"

#
# Create backing files and set tile size.
#
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2}
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

#
# Create the pool.
#
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Create an encrypted dataset.
#
log_must eval "echo '$PASSPHRASE' | zfs create \
	-o encryption=aes-256-gcm \
	-o keyformat=passphrase \
	-o keylocation=prompt \
	$TESTPOOL/encrypted"

#
# Write files and record checksums.
#
set -A cksums
typeset -i idx=0

while (( idx < 5 )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/encrypted/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/encrypted/file.$idx)
	(( idx = idx + 1 ))
done

#
# Also write a larger file for more coverage.
#
log_must file_write -o create -b 1048576 -c 32 -d 'R' \
	-f /$TESTPOOL/encrypted/largefile
typeset large_cksum=$(xxh128digest /$TESTPOOL/encrypted/largefile)

#
# Sync and export the pool.
#
log_must zpool sync $TESTPOOL
log_must zpool export $TESTPOOL

#
# Import the pool. The encrypted dataset will not be mounted yet.
#
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Load the encryption key and mount the dataset.
#
log_must eval "echo '$PASSPHRASE' | zfs load-key $TESTPOOL/encrypted"
log_must zfs mount $TESTPOOL/encrypted

#
# Verify all checksums.
#
idx=0
while (( idx < 5 )); do
	typeset newcksum=$(xxh128digest /$TESTPOOL/encrypted/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for file.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

typeset new_large_cksum=$(xxh128digest /$TESTPOOL/encrypted/largefile)
[[ "$new_large_cksum" == "$large_cksum" ]] || \
	log_fail "Checksum mismatch for largefile: expected=$large_cksum got=$new_large_cksum"

#
# Run scrub and verify no errors.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true

typeset cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | \
	awk 'NF > 2 && $5 != 0' | wc -l)
(( cksum_count == 0 )) || log_fail "Checksum errors detected after scrub"

log_pass "AnyRAID with native encryption preserves data across export/import"
