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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
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
typeset swapname="${ZVOL_DEVDIR}/$vol"
typeset -i pageblocks volblocks max_swaplow
#
# Both swaplow and swaplen are the desired length of
# the swap area in 512-byte blocks.
#
((pageblocks = $(getconf PAGESIZE) / 512))
((volblocks = $(get_prop volsize $vol) / 512))
((max_swaplow = (volblocks - (pageblocks * 2))))

for i in {0..10}; do
	swaplow=$(range_shuffle ${pageblocks} ${max_swaplow} | head -n 1)
	((maxlen = max_swaplow - swaplow))
	swaplen=$(range_shuffle ${pageblocks} ${maxlen} | head -n 1)
	log_must swap -a $swapname $swaplow $swaplen
	log_must swap -d $swapname $swaplow
done

log_pass $assertion
