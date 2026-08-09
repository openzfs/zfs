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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Volume (ref)reservation is not limited by volsize
#
# STRATEGY:
#	1. Create volume on filesystem
#	2. Setting quota for parent filesystem
#	3. Verify volume (ref)reservation is not limited by volsize
#

verify_runnable "global"

function cleanup
{
	destroy_dataset "$fs" "-rf"
	log_must zfs create $fs
	log_must zfs set mountpoint=$TESTDIR $fs
}

log_assert "Volume (ref)reservation is not limited by volsize"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
vol=$fs/vol
log_must zfs create -V 10M $vol
refreserv=`get_prop refreservation $vol`
fudge=1

# Verify the parent filesystem does not affect volume
log_must zfs set quota=25M $fs
log_must zfs set reservation=10M $vol
log_must zfs set refreservation=10M $vol

# Verify it is not affected by volsize
log_must zfs set reservation=$(($refreserv + $fudge)) $vol
log_must zfs set reservation=$(($refreserv - $fudge)) $vol
log_must zfs set refreservation=$(($refreserv + $fudge)) $vol
log_must zfs set refreservation=$(($refreserv - $fudge)) $vol

log_pass "Volume (ref)reservation is not limited by volsize"
