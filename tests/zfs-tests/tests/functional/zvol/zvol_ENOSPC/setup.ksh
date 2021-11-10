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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_ENOSPC/zvol_ENOSPC.cfg

verify_runnable "global"

DISK=${DISKS%% *}
if is_mpath_device $DISK; then
	delete_partitions
fi

default_zvol_setup $DISK $VOLSIZE $BLOCKSIZE

log_must new_fs ${ZVOL_RDEVDIR}/$TESTPOOL/$TESTVOL

log_must mkdir $TESTDIR
log_must mount ${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL $TESTDIR

log_pass
