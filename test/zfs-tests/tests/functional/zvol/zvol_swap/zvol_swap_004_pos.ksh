#!/bin/bash
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	When a swap zvol is added it is resized to be equal to 1/4 c_max,
#	capped between 2G and 16G.
#
# STRATEGY:
#	1. Determine what 1/4 arc_c_max is.
#	2. Create a zvols in a variety of sizes.
#	3. Add them as swap, and verify the volsize is resized correctly.
#

verify_runnable "global"

log_assert "For an added swap zvol, (2G <= volsize <= 16G)"

typeset -i min max mem
((mem = $($KSTAT -p ::arcstats:c_max | $AWK '{print $2}') / 4))
((min = 2 * 1024 * 1024 * 1024))
((max = 16 * 1024 * 1024 * 1024))

for vbs in 512 1024 2048 4096 8192 16384 32768 65536 131072; do
	for multiplier in 1 32 16384 131072; do
		((volsize = vbs * multiplier))
		vol="$TESTPOOL/vol_$volsize"
		swapname="/dev/zvol/dsk/$vol"

		# Create a sparse volume to test larger sizes
		log_must $ZFS create -s -b $vbs -V $volsize $vol
		log_must $SWAP -a $swapname

		if ((mem <= min)); then		# volsize should be 2G
			new_volsize=$(get_prop volsize $vol)
			((new_volsize == min)) || log_fail \
			    "Unexpected volsize: $new_volsize"
		elif ((mem >= max)); then	# volsize should be 16G
			new_volsize=$(get_prop volsize $vol)
			((new_volsize == max)) || log_fail \
			    "Unexpected volsize: $new_volsize"
		else				# volsize should be 'mem'
			new_volsize=$(get_prop volsize $vol)
			((new_volsize == mem)) || log_fail \
			    "Unexpected volsize: $new_volsize"
		fi

		log_must $SWAP -d $swapname
		log_must $ZFS destroy $vol
	done
done

log_pass "For an added swap zvol, (2G <= volsize <= 16G)"
