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

typeset SDSIZE=256
typeset SDHOSTS=1
typeset SDTGTS=1
typeset SDLUNS=1

[[ -z $TRUNCATE ]] && log_fail "Missing TRUNCATE command"
[[ -z $UDEVADM ]] && log_fail "Missing UDEVADM command"
[[ -z $NAWK ]]  && log_fail "Missing NAWK command"
[[ -z $EGREP ]] && log_fail "Missing EGREP command"
[[ -z $READLINK ]] && log_fail "Missing READLINK command"
[[ -z $BASENAME ]] && log_fail "Missing BASENAME command"
[[ -z $TAIL ]] && log_fail "Missing TAIL command"
[[ -z $LSSCSI ]] && log_fail "Missing LSSCSI command"
[[ -z $TOUCH ]] && log_fail "Missing TOUCH command"
[[ -z $CHMOD ]] && log_fail "Missing CHMOD command"
[[ -z $MODUNLOAD ]] && log_fail "Missing MODUNLOAD command"
[[ -z $RM ]] && log_fail "Missing RM command"

verify_runnable "global"

log_must $TOUCH ${ZEDLET_DIR}/zedlog
log_must $CHMOD 666 ${ZEDLET_DIR}/zedlog
log_must $TOUCH ${ZEDLET_DIR}/zed.pid
log_must $CHMOD 666 ${ZEDLET_DIR}/zed.pid
log_must $TOUCH ${ZEDLET_DIR}/state
log_must $CHMOD 666 ${ZEDLET_DIR}/state

($LS /var/run/ | $EGREP zed.pid > /dev/null) 2>/dev/null
if (($? != 0)); then
	($LS /usr/local/var/run/ | $EGREP zed.pid > /dev/null) 2>/dev/null
	if (($? == 0)); then
		log_fail "ZED already running"
	fi
else
	log_fail "ZED already running"
fi

if [[ ! -d /etc/zfs ]]; then
	log_must $CP /usr/local/etc/zfs/zed.d/all-syslog.sh $ZEDLET_DIR
	#directory to store cachefile if not currently present
	log_must $MKDIR /etc/zfs
else
	log_must $CP /etc/zfs/zed.d/all-syslog.sh $ZEDLET_DIR
fi
log_note "Starting ZED"
#run ZED in the background and redirect foreground logging output to zedlog
log_must eval "$ZED -vF -d $ZEDLET_DIR -p $ZEDLET_DIR/zed.pid -s" \
    "$ZEDLET_DIR/state 2>${ZEDLET_DIR}/zedlog&"

#if using loop devices, create a scsi_debug device to be used with
#auto-online test
if is_loop_device $disk1; then
	$LSMOD | $EGREP scsi_debug > /dev/zero
	if (($? == 0)); then
		log_fail "SCSI_DEBUG module already installed"
	else
		log_must $MODLOAD scsi_debug dev_size_mb=$SDSIZE \
		    add_host=$SDHOSTS num_tgts=$SDTGTS max_luns=$SDLUNS
		$LSSCSI | $EGREP scsi_debug > /dev/null
		if (($? == 1)); then
			log_fail "scsi_debug failed"
		else
			SDDEVICE=$($LSSCSI \
			    | $NAWK '/scsi_debug/ {print $6; exit}')
			SDDEVICE_ID=$(get_persistent_disk_name $SDDEVICE)
			log_must  $FORMAT -s $SDDEVICE mklabel gpt
		fi
	fi
fi

log_pass
