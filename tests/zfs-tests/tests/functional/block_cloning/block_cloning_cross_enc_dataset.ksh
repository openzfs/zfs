#!/bin/ksh -p
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
# Copyright (c) 2023, Kay Pedersen <mail@mkwg.de>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

if [[ $(linux_version) -lt $(linux_version "5.3") ]]; then
  log_unsupported "copy_file_range can't copy cross-filesystem before Linux 5.3"
fi

claim="Block cloning across encrypted datasets."

log_assert $claim

DS1="$TESTPOOL/encrypted1"
DS2="$TESTPOOL/encrypted2"
DS1_NC="$TESTPOOL/notcrypted1"
PASSPHRASE="top_secret"

function prepare_enc
{
    log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS
    log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	    "-o keyformat=passphrase -o keylocation=prompt $DS1"
    log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	    "-o keyformat=passphrase -o keylocation=prompt $DS2"
    log_must zfs create $DS1/child1
    log_must zfs create $DS1/child2
    log_must zfs create $DS1_NC

    log_note "Create test file"
    # we must wait until the src file txg is written to the disk otherwise we
    # will fallback to normal copy. See "dmu_read_l0_bps" in
    # "zfs/module/zfs/dmu.c" and "zfs_clone_range" in
    # "zfs/module/zfs/zfs_vnops.c"
    log_must dd if=/dev/urandom of=/$DS1/file bs=128K count=4
    log_must dd if=/dev/urandom of=/$DS1/child1/file bs=128K count=4
    log_must dd if=/dev/urandom of=/$DS1_NC/file bs=128K count=4
    log_must sync_pool $TESTPOOL
}

function cleanup_enc
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

function clone_and_check
{
    I_FILE="$1"
    O_FILE=$2
    I_DS=$3
    O_DS=$4
    SAME_BLOCKS=$5
    # the CLONE option provides a choice between copy_file_range
    # which should clone and a dd which is a copy no matter what
    CLONE=$6
    SNAPSHOT=$7
    if [ ${#SNAPSHOT} -gt 0 ]; then
        I_FILE=".zfs/snapshot/$SNAPSHOT/$1"
    fi
    if [ $CLONE ]; then
        log_must clonefile -f "/$I_DS/$I_FILE" "/$O_DS/$O_FILE" 0 0 524288
    else
        log_must dd if="/$I_DS/$I_FILE" of="/$O_DS/$O_FILE" bs=128K
    fi
    log_must sync_pool $TESTPOOL

    log_must have_same_content "/$I_DS/$I_FILE" "/$O_DS/$O_FILE"

    if [ ${#SNAPSHOT} -gt 0 ]; then
        I_DS="$I_DS@$SNAPSHOT"
        I_FILE="$1"
    fi
    typeset blocks=$(get_same_blocks \
      $I_DS $I_FILE $O_DS $O_FILE $PASSPHRASE)
    log_must [ "$blocks" = "$SAME_BLOCKS" ]
}

log_onexit cleanup_enc

prepare_enc

log_note "Cloning entire file with copy_file_range across different enc" \
    "roots, should fallback"
# we are expecting no same block map.
clone_and_check "file" "clone" $DS1 $DS2 "" true
log_note "check if the file is still readable and the same after" \
    "unmount and key unload, shouldn't fail"
typeset hash1=$(md5digest "/$DS1/file")
log_must zfs umount $DS1 && zfs unload-key $DS1
typeset hash2=$(md5digest "/$DS2/clone")
log_must [ "$hash1" = "$hash2" ]

cleanup_enc
prepare_enc

log_note "Cloning entire file with copy_file_range across different child datasets"
# clone shouldn't work because of deriving a new master key for the child
# we are expecting no same block map.
clone_and_check "file" "clone" $DS1 "$DS1/child1" "" true
clone_and_check "file" "clone" "$DS1/child1" "$DS1/child2" "" true

cleanup_enc
prepare_enc

log_note "Copying entire file with copy_file_range across same snapshot"
log_must zfs snapshot -r $DS1@s1
log_must sync_pool $TESTPOOL
log_must rm -f "/$DS1/file"
log_must sync_pool $TESTPOOL
clone_and_check "file" "clone" "$DS1" "$DS1" "0 1 2 3" true "s1"

cleanup_enc
prepare_enc

log_note "Copying entire file with copy_file_range across different snapshot"
clone_and_check "file" "file" $DS1 $DS2 "" true
log_must zfs snapshot -r $DS2@s1
log_must sync_pool $TESTPOOL
log_must rm -f "/$DS1/file" "/$DS2/file"
log_must sync_pool $TESTPOOL
clone_and_check "file" "clone" "$DS2" "$DS1" "" true "s1"
typeset hash1=$(md5digest "/$DS1/.zfs/snapshot/s1/file")
log_note "destroy the snapshot and check if the file is still readable and" \
    "has the same content"
log_must zfs destroy -r $DS2@s1
log_must sync_pool $TESTPOOL
typeset hash2=$(md5digest "/$DS1/file")
log_must [ "$hash1" = "$hash2" ]

cleanup_enc
prepare_enc

log_note "Copying with copy_file_range from non encrypted to encrypted"
clone_and_check "file" "copy" $DS1_NC $DS1 "" true

cleanup_enc
prepare_enc

log_note "Copying with copy_file_range from encrypted to non encrypted"
clone_and_check "file" "copy" $DS1 $DS1_NC "" true

log_must sync_pool $TESTPOOL

log_pass $claim
