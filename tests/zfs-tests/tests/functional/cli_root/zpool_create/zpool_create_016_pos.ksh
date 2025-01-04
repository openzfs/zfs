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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
#
# DESCRIPTION:
# 'zpool create' will success with no device in swap
#
#
# STRATEGY:
# 1. delete all devices in the swap
# 2. create a zpool
# 3. Verify the creation was successful
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	#recover swap devices
	FSTAB=$TEST_BASE_DIR/fstab_$$
	rm -f $FSTAB
	for sdisk in $swap_disks; do
		echo "$sdisk	-	-	swap	-	no	-" >> $FSTAB
	done
	if [ -e $FSTAB ]
	then
		log_must swapadd $FSTAB
	fi
	rm -f $FSTAB
	if [ $dump_device != "none" ]
	then
		log_must dumpadm -u -d $dump_device
	fi
}

typeset swap_disks=$(swap -l | awk '!/swapfile/ {print $1}')
typeset dump_device=$(dumpadm | awk '/Dump device/ {print $3}')

log_assert "'zpool create' should success with no device in swap."
log_onexit cleanup

for sdisk in $swap_disks; do
	log_note "Executing: swap -d $sdisk"
	swap -d $sdisk >/dev/null 2>&1 ||
		log_untested "Unable to delete swap device $sdisk because of" \
				"insufficient RAM"
done

log_must zpool create $TESTPOOL $DISK0
log_must zpool destroy $TESTPOOL

log_pass "'zpool create' passed as expected with applicable scenario."
