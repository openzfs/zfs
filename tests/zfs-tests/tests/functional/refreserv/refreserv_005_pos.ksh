#!/bin/ksh -p
#
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
