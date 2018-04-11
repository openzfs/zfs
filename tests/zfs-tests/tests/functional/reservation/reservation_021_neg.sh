#!/bin/ksh -p
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
# The use of refreservation=auto on a filesystem does not change the
# refreservation and results in an error.
#
# STRATEGY:
# 1) Create a filesystem
# 2) Verify that zfs set refreservation=auto fails without changing
# refreservation from none.
# 3) Set refreservation to a valid value.
# 4) Verify that zfs set refreservation=auto fails without changing
# refreservation from the previous value.
#

verify_runnable "both"

fs=$TESTPOOL/$TESTFS/$(basename $0).$$

function cleanup
{
	destroy_dataset "$fs" "-f"
}

log_onexit cleanup

log_assert "refreservation=auto on a filesystem generates an error without" \
	"changing refreservation"

space_avail=$(get_prop available $TESTPOOL)
(( fs_size = space_avail / 4 ))

# Create a filesystem with no refreservation
log_must zfs create $fs
resv=$(get_prop refreservation $fs)
log_must test $resv -eq 0

# Verify that refreservation=auto fails without altering refreservation
log_mustnot zfs set refreservation=auto $fs
resv=$(get_prop refreservation $fs)
log_must test $resv -eq 0

# Set refreservation and verify
log_must zfs set refreservation=$fs_size $fs
resv=$(get_prop refreservation $fs)
log_must test $resv -eq $fs_size

# Verify that refreservation=auto fails without altering refreservation
log_mustnot zfs set refreservation=auto $fs
resv=$(get_prop refreservation $fs)
log_must test $resv -eq $fs_size

log_pass "refreservation=auto does not work on filesystems, as expected"
