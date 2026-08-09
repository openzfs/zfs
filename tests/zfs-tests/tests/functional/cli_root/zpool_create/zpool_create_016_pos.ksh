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
