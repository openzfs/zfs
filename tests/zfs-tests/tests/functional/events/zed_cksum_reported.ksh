#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2022, Klara Inc.
#
# This software was developed by Rob Wing <rob.wing@klarasystems.com>
# under sponsorship from Seagate Technology LLC and Klara Inc.

# DESCRIPTION:
#	Verify that checksum errors are accurately reported to ZED
#
# STRATEGY:
#	1. Create a mirrored/raidz pool
#	2. Inject checksum error
#	3. Verify checksum error count reported to ZED is not zero
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

MOUNTDIR="$TEST_BASE_DIR/checksum_mount"
FILEPATH="$MOUNTDIR/checksum_file"
VDEV="$TEST_BASE_DIR/vdevfile.$$"
VDEV1="$TEST_BASE_DIR/vdevfile1.$$"
POOL="checksum_pool"
FILESIZE="10M"

function cleanup
{
	log_must zed_stop

	log_must zinject -c all
	if poolexists $POOL ; then
		destroy_pool $POOL
	fi
	log_must rm -fd $VDEV $MOUNTDIR
}
log_onexit cleanup

log_assert "Test reported checksum errors to ZED"

function setup_pool
{
	type="$1"

	log_must zpool create -f -m $MOUNTDIR $POOL $type $VDEV $VDEV1
	log_must zpool events -c
	log_must truncate -s 0 $ZED_DEBUG_LOG
	log_must zfs set compression=off $POOL
	log_must zfs set primarycache=none $POOL
}

function do_clean
{
	log_must zinject -c all
	log_must zpool destroy $POOL
}

function do_checksum_error
{
	log_must mkfile $FILESIZE $FILEPATH
	log_must zinject -a -t data -e checksum -T read -f 100 $FILEPATH

	dd if=$FILEPATH of=/dev/null bs=1 count=1 2>/dev/null

	log_must file_wait_event $ZED_DEBUG_LOG "ereport.fs.zfs.checksum" 10

	# checksum error as reported from the vdev.
	zpool_cksum=`zpool get -H -o value checksum_errors $POOL $VDEV`

	# first checksum error reported to ZED.
	zed_cksum=$(awk '/ZEVENT_CLASS=ereport.fs.zfs.checksum/, \
	    /ZEVENT_VDEV_CKSUM_ERRORS=/ { \
	    if ($1 ~ "ZEVENT_VDEV_CKSUM_ERRORS") \
	    { print $0; exit } }' $ZED_DEBUG_LOG)

	log_must [ $zpool_cksum -gt 0 ]

	log_mustnot [ "$zed_cksum" = "ZEVENT_VDEV_CKSUM_ERRORS=0" ]

	log_must [ "$zed_cksum" = "ZEVENT_VDEV_CKSUM_ERRORS=1" ]
}

# Set checksum_n=1
# fire 1 event, should degrade.
function checksum_error
{
	type=$1

	setup_pool $type
	do_checksum_error
	do_clean
}

log_must truncate -s $MINVDEVSIZE $VDEV
log_must truncate -s $MINVDEVSIZE $VDEV1
log_must mkdir -p $MOUNTDIR

log_must zed_start
checksum_error mirror
checksum_error raidz

log_pass "Test reported checksum errors to ZED"
