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
# Copyright (c) 2016, 2017 by Intel Corporation. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

[[ -z $UDEVADM ]] && log_fail "Missing UDEVADM command"
[[ -z $LSMOD ]]  && log_fail "Missing LSMOD command"
[[ -z $LSSCSI ]] && log_fail "Missing LSSCSI command"
[[ -z $MODUNLOAD ]] && log_fail "Missing MODUNLOAD command"
[[ -z $MODLOAD ]] && log_fail "Missing MODLOAD command"

verify_runnable "global"

if [[ ! -d $ZEDLET_DIR ]]; then
	log_must $MKDIR $ZEDLET_DIR
fi
if [[ ! -e $VDEVID_CONF ]]; then
	log_must $TOUCH $VDEVID_CONF
fi
if [[ -e $VDEVID_CONF_ETC ]]; then
	log_fail "Must not have $VDEVID_CONF_ETC file present on system"
fi

# Create a symlink for /etc/zfs/vdev_id.conf file
log_must ln -s $VDEVID_CONF $VDEVID_CONF_ETC

zed_start

# Create a scsi_debug device to be used with auto-online (if using loop devices)
# and auto-replace regardless of other devices
load_scsi_debug $SDSIZE $SDHOSTS $SDTGTS $SDLUNS

log_pass
