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
# Copyright (c) 2016 by Intel Corporation. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

verify_runnable "global"

cleanup_devices $DISKS

if [[ -f ${ZEDLET_DIR}/zed.pid ]]; then
	zedpid=$($CAT ${ZEDLET_DIR}/zed.pid)
	log_must $KILL $zedpid
fi

log_must $RM ${ZEDLET_DIR}/all-syslog.sh
log_must $RM ${ZEDLET_DIR}/zed.pid
log_must $RM ${ZEDLET_DIR}/zedlog
log_must $RM ${ZEDLET_DIR}/state
log_must $RMDIR $ZEDLET_DIR

if is_loop_device $DISK1; then
	SD=$($LSSCSI | $NAWK '/scsi_debug/ {print $6; exit}')
	SDDEVICE=$($ECHO $SD | $NAWK -F / '{print $3}')

	if [[ -z $SDDEVICE ]]; then
		log_pass
	fi
	#offline disk
	on_off_disk $SDDEVICE "offline"
	block_device_wait

	log_must $MODUNLOAD scsi_debug
fi

log_pass
