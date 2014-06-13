#! /usr/bin/ksh -p
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
# Verify the ability to take snapshots of zvols used as dump or swap.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Set the volume as dump or swap
# 3. Verify creating a snapshot of the zvol succeeds.
#

verify_runnable "global"

function cleanup
{
	typeset dumpdev=$(get_dumpdevice)
	if [[ $dumpdev != $savedumpdev ]] ; then
		safe_dumpadm $savedumpdev
	fi

	$SWAP -l | $GREP -w $voldev > /dev/null 2>&1
        if (( $? == 0 ));  then
		log_must $SWAP -d $voldev
	fi

	typeset snap
	for snap in snap0 snap1 ; do
		if datasetexists $TESTPOOL/$TESTVOL@$snap ; then
			log_must $ZFS destroy $TESTPOOL/$TESTVOL@$snap
		fi
	done
}

function verify_snapshot
{
	typeset volume=$1

	log_must $ZFS snapshot $volume@snap0
	log_must $ZFS snapshot $volume@snap1
	log_must datasetexists $volume@snap0 $volume@snap1

	log_must $ZFS destroy $volume@snap1
	log_must $ZFS snapshot $volume@snap1

	log_mustnot $ZFS rollback -r $volume@snap0
	log_must datasetexists $volume@snap0

	log_must $ZFS destroy -r $volume@snap0
}

log_assert "Verify the ability to take snapshots of zvols used as dump or swap."
log_onexit cleanup

voldev=/dev/zvol/dsk/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

# create snapshot over dump zvol
safe_dumpadm $voldev
log_must is_zvol_dumpified $TESTPOOL/$TESTVOL

verify_snapshot $TESTPOOL/$TESTVOL

safe_dumpadm $savedumpdev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

# create snapshot over swap zvol

log_must $SWAP -a $voldev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

verify_snapshot $TESTPOOL/$TESTVOL

log_must $SWAP -d $voldev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

log_pass "Creating snapshots from dump/swap zvols succeeds."
