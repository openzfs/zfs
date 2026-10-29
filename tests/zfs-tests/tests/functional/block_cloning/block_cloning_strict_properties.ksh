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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

#
# DESCRIPTION:
#	With zfs_bclone_strict_properties set, no block is cloned
#	between datasets whose checksum, compression, copies or dedup
#	properties differ, or whose special_small_blocks would put
#	the block on another vdev class.  copy_file_range() copies
#	instead.  With the tunable clear, the same calls clone.
#

verify_runnable "global"

verify_crossfs_block_cloning

claim="Strict properties stop cloning across mismatched datasets."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	restore_tunable BCLONE_STRICT_PROPERTIES
}

log_onexit cleanup

typeset -a disks=($DISKS)
(( ${#disks[@]} >= 2 )) || log_unsupported "needs two disks"

log_must save_tunable BCLONE_STRICT_PROPERTIES
log_must zpool create -o feature@block_cloning=enabled \
    -O compression=off $TESTPOOL ${disks[0]} special ${disks[1]}
log_must zfs create $TESTPOOL/src
log_must zfs create -o checksum=sha256 $TESTPOOL/cksum
log_must zfs create -o compression=lz4 $TESTPOOL/comp
log_must zfs create -o copies=2 $TESTPOOL/copies
log_must zfs create -o dedup=on $TESTPOOL/dedup
log_must zfs create -o special_small_blocks=128K $TESTPOOL/tier

log_must dd if=/dev/urandom of=/$TESTPOOL/src/file bs=128K count=4
log_must sync_pool $TESTPOOL

typeset ds blocks want
typeset -i strict
for strict in 1 0; do
	log_must set_tunable32 BCLONE_STRICT_PROPERTIES $strict
	want="0 1 2 3"
	(( strict == 1 )) && want=""
	for ds in cksum comp copies dedup tier; do
		log_must clonefile -f /$TESTPOOL/src/file \
		    /$TESTPOOL/$ds/file$strict 0 0 524288
		log_must sync_pool $TESTPOOL
		log_must have_same_content /$TESTPOOL/src/file \
		    /$TESTPOOL/$ds/file$strict
		blocks=$(get_same_blocks $TESTPOOL/src file \
		    $TESTPOOL/$ds file$strict)
		log_note "$ds strict=$strict shared: '$blocks'"
		log_must [ "$blocks" = "$want" ]
	done
done

log_pass $claim
