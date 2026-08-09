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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_remove/zpool_remove.cfg

#
# DESCRIPTION:
# Verify that 'zpool can remove hotspare devices from pool when it state
#              switch from active to inactive'
#
# STRATEGY:
# 1. Create a hotspare pool
# 2. Try to replace the inactive hotspare device to active device in the pool
# 3. Try to detach active (spare) device to make it inactive
# 3. Verify that the zpool remove succeed.
#

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
}

log_onexit cleanup
typeset disk=${DISK}

typeset spare_devs1="${DISK0}"
typeset spare_devs2="${DISK1}"
typeset spare_devs3="${DISK2}"

log_assert "zpool remove can remove hotspare device which state go though" \
	" active to inactive in pool"

log_note "Check spare device which state go through active to inactive"
log_must zpool create $TESTPOOL $spare_devs1 $spare_devs2 spare $spare_devs3
log_must zpool replace $TESTPOOL $spare_devs2 $spare_devs3
log_mustnot zpool remove $TESTPOOL $spare_devs3
log_must zpool detach $TESTPOOL $spare_devs3
log_must zpool remove $TESTPOOL $spare_devs3

log_pass "'zpool remove device passed as expected.'"
