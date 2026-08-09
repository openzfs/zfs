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
# For a sparse volume changes to the volsize are not reflected in the
# reservation.
#
# STRATEGY:
# 1) Create a regular and sparse volume
# 2) Get the space available in the pool
# 3) Set reservation with various sizes on the regular and sparse volumes
# 4) Verify that the 'reservation' property for the regular volume has
#    the correct value.
# 5) Verify that the 'reservation' property for the sparse volume is set to
#    'none'
#

verify_runnable "global"

function cleanup
{
	typeset vol

	for vol in $regvol $sparsevol; do
		destroy_dataset $vol
	done
}
log_onexit cleanup

log_assert "Verify that the volsize changes of sparse volumes are not " \
    "reflected in the reservation."
log_onexit cleanup

# Create a regular and sparse volume for testing.
regvol=$TESTPOOL/$TESTVOL
sparsevol=$TESTPOOL/$TESTVOL2
log_must zfs create -V 64M $regvol
log_must zfs create -s -V 64M $sparsevol

typeset vsize=$(get_prop available $TESTPOOL)
typeset iterate=10
typeset regreserv
typeset sparsereserv
typeset volblocksize=$(get_prop volblocksize $regvol)
typeset blknum=0
typeset randomblknum
((blknum = vsize / volblocksize))

while ((iterate > 1)); do
	((randomblknum = 1 + RANDOM % blknum))
	# Make sure volsize is a multiple of volume block size
	((vsize = randomblknum * volblocksize))
	log_must zfs set volsize=$vsize $regvol
	log_must zfs set volsize=$vsize $sparsevol
	vsize=$(volsize_to_reservation $regvol $vsize)
	regreserv=$(get_prop refreservation $regvol)
	sparsereserv=$(get_prop reservation $sparsevol)
	((sparsereserv == vsize)) && \
		log_fail "volsize changes of sparse volume is reflected in " \
		    "reservation (expected $vsize, got $sparsereserv)."
	((regreserv != vsize)) && \
		log_fail "volsize changes of regular volume is not reflected " \
		    "in reservation (expected $vsize, got $regreserv)."
	((iterate = iterate - 1))
done

log_pass "The volsize changes of sparse volumes are not reflected in the " \
    "reservation"
