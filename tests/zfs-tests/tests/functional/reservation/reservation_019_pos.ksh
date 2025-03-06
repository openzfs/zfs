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
# A thin provisioned volume can become thick provisioned with 'zfs set
# refreservation=auto'.
#
# STRATEGY:
# 1) Create a sparse value.
# 2) Use zfs set refreservation=auto to make it thick provisioned.
# 3) Verify that refreservation is now the size predicted by
# volsize_to_reservation().
#

verify_runnable "global"

function cleanup
{
	destroy_dataset "$TESTPOOL/$TESTVOL" "-f"
}

log_onexit cleanup

log_assert "A thin provisioned volume can become thick provisioned with" \
    "'zfs set refreservation=auto'."

space_avail=$(get_prop available $TESTPOOL)
(( vol_size = (space_avail / 2) & ~(1024 * 1024 - 1) ))

vol=$TESTPOOL/$TESTVOL

# Create sparse vol and verify
log_must zfs create -V $vol_size -s $vol
resv=$(get_prop refreservation $vol)
log_must test $resv -eq 0

# Set refreservation
log_must zfs set refreservation=auto $vol

# Verify
resv=$(get_prop refreservation $vol)
expected=$(volsize_to_reservation $vol $vol_size)
log_must test $resv -eq $expected

log_pass "Setting refreservation=auto set refreservation to expected value"
