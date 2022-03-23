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
. $STF_SUITE/tests/functional/largest_pool/largest_pool.cfg

# DESCRIPTION:
#	The largest pool can be created and a dataset in that
#	pool can be created and mounted.
#
# STRATEGY:
#	create a pool which will contain a volume device.
#	create a volume device of desired sizes.
#	create the largest pool allowed using the volume vdev.
#	create and mount a dataset in the largest pool.
#	create some files in the zfs file system.
#	do some zpool list commands and parse the output.

verify_runnable "global"

#
# Parse the results of zpool & zfs creation with specified size
#
# $1: volume size
#
# return value:
# 0 -> success
# 1 -> failure
#
function parse_expected_output
{
	UNITS=`echo $1 | sed -e 's/^\([0-9].*\)\([a-z].\)/\2/'`
	case "$UNITS" in
		'mb') CHKUNIT="M" ;;
		'gb') CHKUNIT="G" ;;
		'tb') CHKUNIT="T" ;;
		'pb') CHKUNIT="P" ;;
		'eb') CHKUNIT="E" ;;
		*) CHKUNIT="M" ;;
	esac

	log_note "Detect zpool $TESTPOOL in this test machine."
	log_must eval "zpool list $TESTPOOL > $TEST_BASE_DIR/j.$$"
	log_must eval "grep $TESTPOOL $TEST_BASE_DIR/j.$$ | \
		awk '{print $2}' | grep $CHKUNIT"

	log_note "Detect the file system in this test machine."
	log_must eval "df -F zfs -h > $TEST_BASE_DIR/j.$$"
	log_must eval "grep $TESTPOOL $TEST_BASE_DIR/j.$$ | \
		awk '{print $2}' | grep $CHKUNIT"

	return 0
}

#
# Check and destroy zfs, volume & zpool remove the temporary files
#
function cleanup
{
	log_note "Start cleanup the zfs and pool"

	if datasetexists $TESTPOOL/$TESTFS ; then
		if ismounted $TESTPOOL/$TESTFS ; then
			log_must zfs unmount $TESTPOOL/$TESTFS
		fi
		destroy_dataset $TESTPOOL/$TESTFS
	fi

	destroy_pool $TESTPOOL

	datasetexists $TESTPOOL2/$TESTVOL && \
		destroy_dataset $TESTPOOL2/$TESTVOL

	destroy_pool $TESTPOOL2

	rm -f $TEST_BASE_DIR/j.* > /dev/null
}

log_assert "The largest pool can be created and a dataset in that" \
	"pool can be created and mounted."

# Set trigger. When the test case exit, cleanup is executed.
log_onexit cleanup

# -----------------------------------------------------------------------
# volume sizes with unit designations.
#
# Note: specifying the number '1' as size will not give the correct
# units for 'df'.  It must be greater than one.
# -----------------------------------------------------------------------
typeset str
for volsize in $VOLSIZES; do
	log_note "Create a pool which will contain a volume device"
	log_must create_pool $TESTPOOL2 "$DISKS"

	log_note "Create a volume device of desired sizes: $volsize"
	if ! str=$(zfs create -sV $volsize $TESTPOOL2/$TESTVOL 2>&1); then
		if [[ is_32bit && \
			$str == *${VOL_LIMIT_KEYWORD1}* || \
			$str == *${VOL_LIMIT_KEYWORD2}* || \
			$str == *${VOL_LIMIT_KEYWORD3}* ]]
		then
			log_unsupported \
				"Max volume size is 1TB on 32-bit systems."
		else
			log_fail "zfs create -sV $volsize $TESTPOOL2/$TESTVOL"
		fi
	fi
	block_device_wait

	log_note "Create the largest pool allowed using the volume vdev"
	log_must create_pool $TESTPOOL "$VOL_PATH"

	log_note "Create a zfs file system in the largest pool"
	log_must zfs create $TESTPOOL/$TESTFS

	log_note "Parse the execution result"
	parse_expected_output $volsize

	log_note "unmount this zfs file system $TESTPOOL/$TESTFS"
	log_must zfs unmount $TESTPOOL/$TESTFS

	log_note "Destroy zfs, volume & zpool"
	log_must zfs destroy $TESTPOOL/$TESTFS
	destroy_pool $TESTPOOL
	log_must_busy zfs destroy $TESTPOOL2/$TESTVOL
	destroy_pool $TESTPOOL2
done

log_pass "Dataset can be created, mounted & destroy in largest pool succeeded."
