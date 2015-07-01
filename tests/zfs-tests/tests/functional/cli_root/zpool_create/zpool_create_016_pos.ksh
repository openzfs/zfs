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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib
. $TMPFILE

#
#
# DESCRIPTION:
# 'zpool create' will success with no device in swap
#
#
# STRATEGY:
# 1. delete all devices in the swap
# 2. create a zpool
# 3. Verify the creation is successed.
#

verify_runnable "global"

function cleanup
{
	destroy_pool -f $TESTPOOL

	#recover swap devices
	FSTAB=/tmp/fstab_$$
	$RM -f $FSTAB
	for sdisk in $swap_disks; do
		if [[ -n "$LINUX" ]]; then
			$ECHO "$sdisk	none	swap	sw	0 0" >> $FSTAB
		else
			$ECHO "$sdisk	-	-	swap	-	no	-" >> $FSTAB
		fi
	done
	if [ -e $FSTAB -a -z "$LINUX" ]
	then
		log_must $SWAPADD $FSTAB
	fi
	$RM -f $FSTAB
	if [ $dump_device != "none" ]
	then
		log_must $DUMPADM -u -d $dump_device
	fi
}

if [[ -n $DISK ]]; then
	disk=$DISK
else
	disk=$DISK0
fi

typeset slice_part=s
[[ -n "$LINUX" ]] && slice_part=p

typeset pool_dev=${disk}${slice_part}${SLICE0}
if [[ -n "$LINUX" ]]; then
	typeset swap_disks=$($SWAP -s | $GREP -v "swapfile" | $AWK '{print $1}')
else
	typeset swap_disks=$($SWAP -l | $GREP -v "swapfile" | $AWK '{print $1}')
fi
typeset dump_device=$($DUMPADM | $GREP "Dump device" | $AWK '{print $3}')

log_assert "'zpool create' should success with no device in swap."
log_onexit cleanup

for sdisk in $swap_disks; do
	log_note "Executing: swap -d $sdisk"
	if [[ -n "$LINUX" ]]; then
		swapoff $sdisk >/dev/null 2>&1;
	else
		$SWAP -d $sdisk >/dev/null 2>&1;
	fi
	if [[ $? != 0 ]]; then
		log_untested "Unable to delete swap device $sdisk because of" \
				"insufficient RAM"
	fi
done

log_must $ZPOOL create $TESTPOOL $pool_dev
destroy_pool $TESTPOOL

log_pass "'zpool create' passed as expected with applicable scenario."
