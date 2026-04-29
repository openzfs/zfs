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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2025 ConnectWise Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
# 'zpool scrub' with thorough scrub enabled works with various configurations:
# 1) Compressed dataset
# 2) Compressed and encrypted dataset
# 3) Encrypted dataset
# 4) Compressed and encrypted dataset with keys unloaded
# 5) Compressed and encrypted dataset with key unloaded during thorough scrub
# 6) Verify thorough scrub pause/resume
# 7) Verify incompatible flags with -t (including error scrub)
# 8) Verify compatible flags with -t
#

function cleanup
{
	datasetexists $TESTPOOL/comp && destroy_dataset $TESTPOOL/comp
	datasetexists $TESTPOOL/comp_enc && destroy_dataset $TESTPOOL/comp_enc
	datasetexists $TESTPOOL/enc && destroy_dataset $TESTPOOL/enc
	datasetexists $TESTPOOL/comp_enc_unloaded && destroy_dataset $TESTPOOL/comp_enc_unloaded
	datasetexists $TESTPOOL/comp_enc_unloading && destroy_dataset $TESTPOOL/comp_enc_unloading
}

verify_runnable "global"

log_onexit cleanup

log_assert "Thorough scrub works with various dataset configurations."

# 1) Compressed dataset
log_must zfs create $TESTPOOL/comp
log_must zfs set compression=on $TESTPOOL/comp
typeset file_comp="/$TESTPOOL/comp/$TESTFILE0"
log_must dd if=/dev/urandom of=$file_comp bs=1024 count=1024 oflag=sync
# Make sure data is compressible
write_compressible $(get_prop mountpoint $TESTPOOL/comp) 16m

# 2) Compressed and encrypted dataset
log_must eval "echo 'password' | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/comp_enc"
log_must zfs set compression=on $TESTPOOL/comp_enc
typeset file_comp_enc="/$TESTPOOL/comp_enc/$TESTFILE0"
log_must dd if=/dev/urandom of=$file_comp_enc bs=1024 count=1024 oflag=sync
write_compressible $(get_prop mountpoint $TESTPOOL/comp_enc) 16m

# 3) Encrypted dataset
log_must eval "echo 'password' | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/enc"
log_must zfs set compression=off $TESTPOOL/enc
typeset file_enc="/$TESTPOOL/enc/$TESTFILE0"
log_must dd if=/dev/urandom of=$file_enc bs=1024 count=1024 oflag=sync

# 4) Compressed and encrypted dataset with keys unloaded
log_must eval "echo 'password' | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/comp_enc_unloaded"
log_must zfs set compression=on $TESTPOOL/comp_enc_unloaded
typeset file_comp_enc_unloaded="/$TESTPOOL/comp_enc_unloaded/$TESTFILE0"
log_must dd if=/dev/urandom of=$file_comp_enc_unloaded bs=1024 count=1024 oflag=sync
write_compressible $(get_prop mountpoint $TESTPOOL/comp_enc_unloaded) 16m

log_must zfs unmount $TESTPOOL/comp_enc_unloaded
log_must zfs unload-key $TESTPOOL/comp_enc_unloaded

# Run and wait for thorough scrub
log_must zpool scrub -t -w $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "thorough"

log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

# 5) Compressed and encrypted dataset with key unloaded during scrub
log_must eval "echo 'password' | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/comp_enc_unloading"
log_must zfs set compression=on $TESTPOOL/comp_enc_unloading
typeset file_enc_unloading="/$TESTPOOL/comp_enc_unloading/$TESTFILE0"
# Write enough data to allow time for unloading key
log_must dd if=/dev/urandom of=$file_enc_unloading bs=1024 count=10240 oflag=sync
write_compressible $(get_prop mountpoint $TESTPOOL/comp_enc_unloading) 16m

log_must zfs unmount $TESTPOOL/comp_enc_unloading
# Key is still loaded

# Start scrub (async)
log_must zpool scrub -t $TESTPOOL

# Unload key while scrub is running
log_must zfs unload-key $TESTPOOL/comp_enc_unloading

# Wait for scrub
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

# Run and wait for normal scrub
log_must zpool scrub -w $TESTPOOL

log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

# 6) Verify thorough scrub pause/resume
log_must zpool scrub -t $TESTPOOL
sleep 3
log_must check_pool_status $TESTPOOL "scan" "thorough"
log_must zpool scrub -p $TESTPOOL
sleep 3
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_must zpool scrub -t $TESTPOOL
sleep 3
log_must check_pool_status $TESTPOOL "scan" "thorough"
log_must zpool scrub -p $TESTPOOL
sleep 3
log_must zpool scrub -t $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "thorough"
log_must zpool wait -t scrub $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "thorough"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"


# 7) Verify incompatible flags with -t
log_mustnot zpool scrub -s -t $TESTPOOL
log_mustnot zpool scrub -p -t $TESTPOOL
log_mustnot zpool scrub -t -e $TESTPOOL

# 8) Verify compatible flags with -t
log_must zpool scrub -t -w $TESTPOOL
log_must zpool sync $TESTPOOL
log_must zpool scrub -t -C $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_pass "Thorough scrub works with various dataset configurations."
