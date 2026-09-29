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
# Creating a volume with -o refreservation=auto creates a thick provisioned
# volume, whose refreservation follows later volsize changes.
#
# STRATEGY:
# 1) Create a volume with -o refreservation=auto and verify that its
#    refreservation matches the size predicted by volsize_to_reservation()
#    and the one 'zfs create -V' sets by default.
# 2) Do the same with a non-default volblocksize, with -s, and for an
#    encrypted volume.
# 3) Grow and shrink the volumes and verify that their refreservation
#    follows.
# 4) Verify that setting volsize and refreservation=auto together on a
#    sparse volume gives the same refreservation, and that a refreservation
#    other than auto is left as given at creation.
# 5) Verify that a filesystem cannot be created with -o refreservation=auto,
#    and that a volume whose refreservation does not fit is not created.
#

verify_runnable "global"

function cleanup
{
	typeset ds

	for ds in $vol $vol_bs $vol_sparse $vol_crypt $vol_set $vol_num \
	    $vol_ref $vol_big $fs; do
		datasetexists $ds && destroy_dataset $ds
	done
}

#
# Verify that the volume is thick provisioned, with the refreservation that
# 'zfs create -V' sets by default for its volsize and volblocksize.
#
function check_thick # <volume>
{
	typeset volsize=$(get_prop volsize $1)
	typeset vbs=$(get_prop volblocksize $1)
	typeset resv=$(get_prop refreservation $1)

	log_must test $resv -eq $(volsize_to_reservation $1 $volsize)
	log_must zfs create -V $volsize -o volblocksize=$vbs $vol_ref
	log_must test $resv -eq $(get_prop refreservation $vol_ref)
	destroy_dataset $vol_ref
}

log_onexit cleanup

log_assert "Creating a volume with -o refreservation=auto creates a thick" \
    "provisioned volume"

space_avail=$(get_prop available $TESTPOOL)
(( vol_size = (space_avail / 16) & ~(1024 * 1024 - 1) ))

vol=$TESTPOOL/$TESTVOL
vol_bs=$TESTPOOL/$TESTVOL-bs
vol_sparse=$TESTPOOL/$TESTVOL-sparse
vol_crypt=$TESTPOOL/$TESTVOL-crypt
vol_set=$TESTPOOL/$TESTVOL-set
vol_num=$TESTPOOL/$TESTVOL-num
vol_ref=$TESTPOOL/$TESTVOL2
vol_big=$TESTPOOL/$TESTVOL-big
fs=$TESTPOOL/$TESTFS1

log_must zfs create -V $vol_size -o refreservation=auto $vol
check_thick $vol

# The volblocksize of the new volume must be taken into account
log_must zfs create -V $vol_size -o volblocksize=64k \
    -o refreservation=auto $vol_bs
check_thick $vol_bs

# As with any other refreservation, -s does not override the one given
log_must zfs create -s -V $vol_size -o refreservation=auto $vol_sparse
check_thick $vol_sparse

log_must eval "echo password | zfs create -V $vol_size -o encryption=on" \
    "-o keyformat=passphrase -o refreservation=auto $vol_crypt"
check_thick $vol_crypt

# The refreservation must follow volsize changes
for ds in $vol $vol_bs $vol_sparse $vol_crypt; do
	for size in $((vol_size * 2)) $((vol_size / 2)); do
		log_must zfs set volsize=$size $ds
		log_must test $(get_prop refreservation $ds) -eq \
		    $(volsize_to_reservation $ds $size)
	done
done

# Setting volsize and refreservation=auto together must give the same result
log_must zfs create -s -V $vol_size $vol_set
log_must zfs set volsize=$((vol_size * 2)) refreservation=auto $vol_set
check_thick $vol_set

# A refreservation other than auto is left as given
log_must zfs create -V $vol_size -o refreservation=$((vol_size / 2)) $vol_num
log_must test $(get_prop refreservation $vol_num) -eq $((vol_size / 2))

# refreservation=auto is only allowed on volumes
log_mustnot zfs create -o refreservation=auto $fs
log_mustnot datasetexists $fs

# A volume whose refreservation does not fit must not be created
(( big_size = (space_avail * 2) & ~(1024 * 1024 - 1) ))
log_mustnot zfs create -V $big_size -o refreservation=auto $vol_big
log_mustnot datasetexists $vol_big

log_pass "Creating a volume with -o refreservation=auto creates a thick" \
    "provisioned volume"
