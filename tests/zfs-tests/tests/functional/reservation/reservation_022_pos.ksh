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
# Copyright 2018 Joyent, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/reservation/reservation.shlib

#
# DESCRIPTION:
#
# Cloning a volume with -o refreservation=auto creates a thick provisioned
# volume
#
# STRATEGY:
# 1) Create a sparse volume.
# 2) Snapshot and clone it, using clone -o refreservation=auto.
# 3) Verify that the clone has refreservation that matches the size predicted by
#    volsize_to_reservation().
# 4) Snapshot this second volume and clone it, using clone -o
#    refreservation=auto.
# 5) Verify that the second clone has refreservation that matches the size
#    predicted by volsize_to_reservation().
#

verify_runnable "global"

function cleanup
{
	# Destroy first vol and descendants in one go.
	destroy_dataset "$TESTPOOL/$TESTVOL" "-Rf"
}

log_onexit cleanup

log_assert "Cloning a volume with -o refreservation=auto creates a thick" \
    "provisioned volume"

space_avail=$(get_prop available $TESTPOOL)
(( vol_size = (space_avail / 4) & ~(1024 * 1024 - 1) ))

vol=$TESTPOOL/$TESTVOL
vol2=$TESTPOOL/$TESTVOL2
vol3=$TESTPOOL/$TESTVOL2-again

# Create sparse vol and verify
log_must zfs create -s -V $vol_size $vol
resv=$(get_prop refreservation $vol)
log_must test $resv -eq 0

# Clone it
snap=$vol@clone
log_must zfs snapshot $snap
log_must zfs clone -o refreservation=auto $snap $vol2

# Verify it is thick provisioned
resv=$(get_prop refreservation $vol2)
expected=$(volsize_to_reservation $vol2 $vol_size)
log_must test $resv -eq $expected

# Clone the thick provisioned volume
snap=$vol2@clone
log_must zfs snapshot $snap
log_must zfs clone -o refreservation=auto $snap $vol3

# Verify new newest clone is also thick provisioned
resv=$(get_prop refreservation $vol3)
expected=$(volsize_to_reservation $vol3 $vol_size)
log_must test $resv -eq $expected

log_pass "Cloning a thick provisioned volume results in a sparse volume"
