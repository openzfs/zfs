#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/reservation/reservation.shlib

#
# DESCRIPTION:
#
# Reservation properties on data objects should be preserved when the
# pool within which they are contained is exported and then re-imported.
#
#
# STRATEGY:
# 1) Create a filesystem as dataset
# 2) Create another filesystem at the same level
# 3) Create a regular volume at the same level
# 4) Create a sparse volume at the same level
# 5) Create a filesystem within the dataset filesystem
# 6) Set reservations on all filesystems
# 7) Export the pool
# 8) Re-import the pool
# 9) Verify that the reservation settings are correct
#

verify_runnable "global"

log_assert "Reservation properties preserved across exports and imports"

function cleanup
{
	for obj in $OBJ_LIST; do
                datasetexists $obj && destroy_dataset $obj -f
        done

	log_must zero_reservation $TESTPOOL/$TESTFS
}
log_onexit cleanup

OBJ_LIST="$TESTPOOL/$TESTFS1/$TESTFS2 $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTVOL \
    $TESTPOOL/$TESTVOL2"

log_must zfs create $TESTPOOL/$TESTFS1
log_must zfs create $TESTPOOL/$TESTFS1/$TESTFS2

space_avail=$(get_prop available $TESTPOOL)

typeset -il resv_set=space_avail/5
resv_set=$(floor_volsize $resv_set)
typeset -il sparse_vol_set_size=space_avail*5
sparse_vol_set_size=$(floor_volsize $sparse_vol_set_size)

# When initially created, a regular volume's reservation property is set
# equal to its size (unlike a sparse volume), so we don't need to set it
# explicitly later on
log_must zfs create -V $resv_set $TESTPOOL/$TESTVOL
log_must zfs create -s -V $sparse_vol_set_size $TESTPOOL/$TESTVOL2

log_must zfs set reservation=$resv_set $TESTPOOL/$TESTFS
log_must zfs set reservation=$resv_set $TESTPOOL/$TESTFS1
log_must zfs set reservation=$resv_set $TESTPOOL/$TESTFS1/$TESTFS2
log_must zfs set reservation=$resv_set $TESTPOOL/$TESTVOL2

log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL

for obj in $TESTPOOL/$TESTFS $OBJ_LIST; do

	if [[ $obj == $TESTPOOL/$TESTVOL ]]; then
		expected=$(volsize_to_reservation $obj $resv_set)
		found=$(get_prop refreservation $obj)
	else
		expected=$resv_set
		found=$(get_prop reservation $obj)
	fi

	[[ $found != $expected ]] && \
	    log_fail "Reservation property for $obj incorrect. Expected " \
	    "$expected but got $found."
done

log_pass "Reservation properties preserved across exports and imports"
