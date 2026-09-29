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
# Copyright (c) 2026 by iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/reservation/reservation.shlib

#
# DESCRIPTION:
#
# A volsize change that fails does not change the refreservation that libzfs
# sets along with it.
#
# STRATEGY:
# 1) Create a thick provisioned volume and make it read-only.  Verify that
#    growing it fails and leaves its volsize and refreservation unchanged.
# 2) Do the same for an encrypted volume whose key is not loaded.
# 3) Create a sparse volume and make it read-only.  Verify that growing it
#    with refreservation=auto fails and leaves its volsize and
#    refreservation unchanged, including the refreservation's source.
# 4) Do the same in a parent whose quota the new refreservation does not
#    fit in, and verify that the refreservation is not set at all.
# 5) Grow a thick provisioned volume beyond the pool's free space.  Verify
#    that its volsize is put back and its refreservation is unchanged.
# 6) Receive a thick provisioned volume and make it read-only.  Verify that
#    growing it fails and leaves its received refreservation unchanged.
#

verify_runnable "global"

function cleanup
{
	typeset ds

	for ds in $vol $vol_crypt $vol_sparse $vol_full $vol_recv $parent; do
		datasetexists $ds && destroy_dataset $ds -r
	done
}

log_onexit cleanup

log_assert "A volsize change that fails does not change the refreservation"

space_avail=$(get_prop available $TESTPOOL)
(( vol_size = (space_avail / 16) & ~(1024 * 1024 - 1) ))
(( new_size = vol_size * 2 ))

vol=$TESTPOOL/$TESTVOL
vol_crypt=$TESTPOOL/$TESTVOL-crypt
vol_sparse=$TESTPOOL/$TESTVOL-sparse
vol_full=$TESTPOOL/$TESTVOL-full
vol_recv=$TESTPOOL/$TESTVOL-recv
parent=$TESTPOOL/$TESTFS1

# A thick provisioned volume, which libzfs grows the refreservation of
log_must zfs create -V $vol_size $vol
resv=$(get_prop refreservation $vol)
log_must zfs set readonly=on $vol
log_mustnot zfs set volsize=$new_size $vol
log_must test $(get_prop volsize $vol) -eq $vol_size
log_must test $(get_prop refreservation $vol) -eq $resv

# The same, when the volume cannot be resized because its key is not loaded
log_must eval "echo password | zfs create -V $vol_size -o encryption=on" \
    "-o keyformat=passphrase $vol_crypt"
resv=$(get_prop refreservation $vol_crypt)
log_must_busy zfs unload-key $vol_crypt
log_mustnot zfs set volsize=$new_size $vol_crypt
log_must test $(get_prop volsize $vol_crypt) -eq $vol_size
log_must test $(get_prop refreservation $vol_crypt) -eq $resv

# A sparse volume, asking for refreservation=auto along with the volsize
log_must zfs create -s -V $vol_size $vol_sparse
log_must zfs set readonly=on $vol_sparse
log_mustnot zfs set volsize=$new_size refreservation=auto $vol_sparse
log_must test $(get_prop volsize $vol_sparse) -eq $vol_size
log_must test $(get_prop refreservation $vol_sparse) -eq 0
log_must test "$(zfs get -H -o source refreservation $vol_sparse)" = \
    "default"

# The same, where the refreservation fails too and must be left alone
log_must zfs create -o quota=$vol_size $parent
log_must zfs create -s -V $vol_size $parent/$TESTVOL
log_must zfs set readonly=on $parent/$TESTVOL
log_mustnot zfs set volsize=$new_size refreservation=auto $parent/$TESTVOL
log_must test $(get_prop volsize $parent/$TESTVOL) -eq $vol_size
log_must test $(get_prop refreservation $parent/$TESTVOL) -eq 0
log_must test "$(zfs get -H -o source refreservation $parent/$TESTVOL)" = \
    "default"

# A thick provisioned volume grown beyond the free space, where the
# refreservation is what fails and libzfs puts the old volsize back
(( big_size = (space_avail * 2) & ~(1024 * 1024 - 1) ))
log_must zfs create -V $vol_size $vol_full
resv=$(get_prop refreservation $vol_full)
log_mustnot zfs set volsize=$big_size $vol_full
log_must test $(get_prop volsize $vol_full) -eq $vol_size
log_must test $(get_prop refreservation $vol_full) -eq $resv

# A received thick provisioned volume, which must keep its received value
log_must zfs snapshot $vol@snap
log_must eval "zfs send -p $vol@snap | zfs receive $vol_recv"
resv=$(get_prop refreservation $vol_recv)
log_must test "$(zfs get -H -o source refreservation $vol_recv)" = "received"
log_must zfs set readonly=on $vol_recv
log_mustnot zfs set volsize=$new_size $vol_recv
log_must test $(get_prop volsize $vol_recv) -eq $vol_size
log_must test $(get_prop refreservation $vol_recv) -eq $resv
log_must test "$(zfs get -H -o source refreservation $vol_recv)" = "received"

log_pass "A volsize change that fails does not change the refreservation"
