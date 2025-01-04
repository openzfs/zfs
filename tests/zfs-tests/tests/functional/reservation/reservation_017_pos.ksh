#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
