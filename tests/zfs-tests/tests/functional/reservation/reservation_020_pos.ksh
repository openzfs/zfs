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
# Cloning a thick provisioned volume results in a sparse volume
#
# STRATEGY:
# 1) Create a thick provisioned volume.
# 2) Snapshot and clone it.
# 3) Verify that the clone is sparse.
#

verify_runnable "global"

function cleanup
{
	# Destroy first vol and descendants in one go.
	destroy_dataset "$TESTPOOL/$TESTVOL" "-Rf"
}

log_onexit cleanup

log_assert "Cloning a thick provisioned volume results in a sparse volume"

space_avail=$(get_prop available $TESTPOOL)
(( vol_size = (space_avail / 4) & ~(1024 * 1024 - 1) ))

vol=$TESTPOOL/$TESTVOL
snap=$vol@clone
vol2=$TESTPOOL/$TESTVOL2

# Create sparse vol and verify
log_must zfs create -V $vol_size $vol
resv=$(get_prop refreservation $vol)
expected=$(volsize_to_reservation $vol $vol_size)
log_must test $resv -eq $expected

# Clone it
log_must zfs snapshot $snap
log_must zfs clone $snap $vol2

# Verify
resv=$(get_prop refreservation $vol2)
log_must test $resv -eq 0

log_pass "Cloning a thick provisioned volume results in a sparse volume"
