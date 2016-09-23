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

[[ -z $UDEVADM ]] && log_fail "Missing UDEVADM command"
[[ -z $NAWK ]]  && log_fail "Missing NAWK command"
[[ -z $EGREP ]] && log_fail "Missing EGREP command"
[[ -z $LSSCSI ]] && log_fail "Missing LSSCSI command"
[[ -z $MODUNLOAD ]] && log_fail "Missing MODUNLOAD command"
[[ -z $PGREP ]] && log_fail "Missing PGREP command"

verify_runnable "global"
if [[ ! -d /var/tmp/zed ]]; then
	log_must $MKDIR  /var/tmp/zed
fi

modprobe -n scsi_debug
if (($? != 0)); then
	log_unsupported "Platform does not have scsi_debug module"
fi

# Verify the ZED is not already running.
$PGREP -x zed > /dev/null
if (($? == 0)); then
	log_fail "ZED already running"
fi

log_must $CP ${ZEDLETDIR}/all-syslog.sh $ZEDLET_DIR

log_note "Starting ZED"
#run ZED in the background and redirect foreground logging output to zedlog
log_must eval "$ZED -vF -d $ZEDLET_DIR -p $ZEDLET_DIR/zed.pid -s" \
    "$ZEDLET_DIR/state 2>${ZEDLET_DIR}/zedlog &"

#if using loop devices, create a scsi_debug device to be used with
#auto-online test
if is_loop_device $DISK1; then
	$LSMOD | $EGREP scsi_debug > /dev/zero
	if (($? == 0)); then
		log_fail "SCSI_DEBUG module already installed"
	else
		log_must $MODLOAD scsi_debug dev_size_mb=$SDSIZE \
		    add_host=$SDHOSTS num_tgts=$SDTGTS max_luns=$SDLUNS
		block_device_wait
		$LSSCSI | $EGREP scsi_debug > /dev/null
		if (($? == 1)); then
			log_fail "scsi_debug failed"
		else
			SDDEVICE=$($LSSCSI \
			    | $NAWK '/scsi_debug/ {print $6; exit}')
			log_must  $FORMAT -s $SDDEVICE mklabel gpt
		fi
	fi
fi

log_pass
