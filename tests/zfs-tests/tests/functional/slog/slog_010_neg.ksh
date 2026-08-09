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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Slog device can not be replaced with spare device
#
# STRATEGY:
#	1. Create a pool with hotspare and log devices.
#	2. Verify slog device can not be replaced with hotspare device.
#	3. Create pool2 with hotspare
#	4. Verify slog device can not be replaced with hotspare device in pool2.
#

verify_runnable "global"

log_assert "Slog device can not be replaced with spare device."
log_onexit cleanup
log_must setup

log_must zpool create $TESTPOOL $VDEV spare $SDEV log $LDEV
sdev=$(random_get $SDEV)
ldev=$(random_get $LDEV)
log_mustnot zpool replace $TESTPOOL $ldev $sdev
log_mustnot verify_slog_device $TESTPOOL $sdev 'ONLINE'
log_must verify_slog_device $TESTPOOL $ldev 'ONLINE'

log_must zpool create $TESTPOOL2 $VDEV2 spare $SDEV2
sdev2=$(random_get $SDEV2)
log_mustnot zpool replace $TESTPOOL $ldev $sdev2
log_mustnot zpool replace -f $TESTPOOL $ldev $sdev2
log_mustnot verify_slog_device $TESTPOOL $sdev2 'ONLINE'
log_must verify_slog_device $TESTPOOL $ldev 'ONLINE'

log_pass "Slog device can not be replaced with spare device."
