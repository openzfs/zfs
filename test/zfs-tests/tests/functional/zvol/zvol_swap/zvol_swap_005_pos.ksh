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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	swaplow + swaplen must be less than or equal to the volume size.
#
# STRATEGY:
#	1. Get test system page size and test volume size.
#	2. Random get swaplow and swaplen.
#	3. Verify swap -a should succeed when swaplow + swaplen <= volume size.
#

verify_runnable "global"

assertion="Verify the sum of swaplow and swaplen is less or equal to volsize"
log_assert $assertion

typeset vol=$TESTPOOL/$TESTVOL
typeset swapname="/dev/zvol/dsk/$vol"
typeset -i pageblocks volblocks max_swaplow
#
# Both swaplow and swaplen are the desired length of
# the swap area in 512-byte blocks.
#
((pageblocks = $($PAGESIZE) / 512))
((volblocks = $(get_prop volsize $vol) / 512))
((max_swaplow = (volblocks - (pageblocks * 2))))

for i in {0..10}; do
	swaplow=$($SHUF -n 1 -i ${pageblocks}-${max_swaplow})
	((maxlen = max_swaplow - swaplow))
	swaplen=$($SHUF -n 1 -i ${pageblocks}-${maxlen})
	log_must $SWAP -a $swapname $swaplow $swaplen
	log_must $SWAP -d $swapname $swaplow
done

log_pass $assertion
