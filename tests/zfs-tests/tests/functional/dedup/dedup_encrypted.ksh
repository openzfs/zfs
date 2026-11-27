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
# Copyright (c) 2026, Tigerblue77.
#

#
# DESCRIPTION:
#	Identical blocks deduplicate in an encrypted dataset, whichever function
#	derives their salt and IV from the plaintext: keyed BLAKE3 in a key
#	created with zfs_crypt_dedup_blake3=1, or HMAC-SHA512 in a key created
#	with 0, like every key of an older release. They keep deduplicating in a
#	clone, after the key is loaded again, and in a raw-received copy, which
#	keeps the choice of the key it was sent from.
#
# STRATEGY:
#	1. Create an encrypted dataset with each value of the tunable, and
#	   check that only the first key records keyed BLAKE3.
#	2. Write a file, then a copy of it, to each: every block deduplicates.
#	3. Write it to a clone of each.
#	4. Export and import the pool, load the keys, and write it again.
#	5. Raw send each dataset and receive it, check that the record came
#	   along, load the key of the copy, and write the file there.
#	After each step, every block of the file is in the duplicate table and
#	none is left in the unique table.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset SRCFILE=$TEST_BASE_DIR/dedup_encrypted.src
typeset STREAM=$TEST_BASE_DIR/dedup_encrypted.zsend
typeset PASSPHRASE="dedup-encrypted"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $SRCFILE $STREAM
	log_must restore_tunable CRYPT_DEDUP_BLAKE3
	log_must restore_tunable DEDUP_LOG_TXG_MAX
}

log_assert "encrypted dedup deduplicates with either salt and IV derivation"

log_must save_tunable CRYPT_DEDUP_BLAKE3
log_must save_tunable DEDUP_LOG_TXG_MAX
log_onexit cleanup

# flush the dedup log every txg, so that zdb -D sees every entry
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

# number of DSL Crypto Keys that record keyed BLAKE3
function blake3_keys
{
	zpool sync $TESTPOOL
	zdb -dddd $TESTPOOL | grep -cE 'DSL_CRYPTO_DEDUP_PRF = 1 ?$'
}

# number of entries in a class of the DDT
function ddt_entries # class
{
	zdb -D $TESTPOOL | awk -v class="DDT-sha256-zap-$1:" '
	    $1 == class { sub(/.*entries=/, ""); n = $0 }
	    END { print n + 0 }'
}

function check_ddt # step
{
	log_must zpool sync $TESTPOOL
	typeset dup=$(ddt_entries duplicate)
	typeset uniq=$(ddt_entries unique)
	log_note "$1: $dup duplicate, $uniq unique DDT entries"
	(( dup == 8 && uniq == 0 )) || \
	    log_fail "$1: $dup duplicate and $uniq unique DDT entries," \
	    "expected 8 and 0"
}

function write_copy # dataset file
{
	log_must dd if=$SRCFILE of=$(get_prop mountpoint $1)/$2 bs=128k
}

function load_and_mount # dataset
{
	log_must eval "echo $PASSPHRASE | zfs load-key $1"
	log_must zfs mount $1
}

# compression=off so that the plaintext of each block is the file data,
# xattr=sa so that no other block is deduplicated
log_must zpool create -f -O dedup=sha256 -O compression=off -O xattr=sa \
    $TESTPOOL $DISKS
log_must test "$(get_pool_prop feature@blake3 $TESTPOOL)" = "enabled"
log_must dd if=/dev/urandom of=$SRCFILE bs=128k count=4

log_must set_tunable32 CRYPT_DEDUP_BLAKE3 1
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
    "-o keyformat=passphrase $TESTPOOL/blake3"
log_must set_tunable32 CRYPT_DEDUP_BLAKE3 0
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
    "-o keyformat=passphrase $TESTPOOL/hmac"
log_must set_tunable32 CRYPT_DEDUP_BLAKE3 1
log_must test $(blake3_keys) -eq 1

# four blocks per dataset, each written twice
for ds in blake3 hmac; do
	write_copy $TESTPOOL/$ds file1
	write_copy $TESTPOOL/$ds file2
done
check_ddt "copies"

for ds in blake3 hmac; do
	log_must zfs snapshot $TESTPOOL/$ds@snap
	log_must zfs clone $TESTPOOL/$ds@snap $TESTPOOL/$ds-clone
	write_copy $TESTPOOL/$ds-clone file3
done
check_ddt "clones"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
for ds in blake3 hmac; do
	load_and_mount $TESTPOOL/$ds
	log_must zfs mount $TESTPOOL/$ds-clone
	write_copy $TESTPOOL/$ds file4
done
check_ddt "reloaded keys"

for ds in blake3 hmac; do
	log_must eval "zfs send -w $TESTPOOL/$ds@snap > $STREAM"
	log_must eval "zfs receive $TESTPOOL/$ds-copy < $STREAM"
done
log_must test $(blake3_keys) -eq 2
for ds in blake3 hmac; do
	load_and_mount $TESTPOOL/$ds-copy
	write_copy $TESTPOOL/$ds-copy file5
done
check_ddt "raw-received copies"

log_must zdb -b $TESTPOOL

log_pass "encrypted dedup deduplicates with either salt and IV derivation"
