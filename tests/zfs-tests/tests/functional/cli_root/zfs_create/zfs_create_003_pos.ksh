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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create.cfg

#
# DESCRIPTION:
# 'zfs create [-b <blocksize>] -V <size> <volume>' can create a volume
# with specified blocksize, which is power of 2 between 512 - 128k.
#
# STRATEGY:
# 1. Create a volume with blocksize in the storage pool
# 2. Verify the volume created successfully
#

verify_runnable "global"

function cleanup
{
	datasetexists $vol && destroy_dataset $vol -f
}

log_assert "Verify creating volume with specified blocksize works."
log_onexit cleanup

set -A options "" "-b 1k" "-b 1K" "-b 1024" "-b 1024b"
vol=$TESTPOOL/$TESTVOL

typeset -i i=0
while (( i < ${#options[*]} )); do
	log_must zfs create ${options[i]} -V $VOLSIZE $vol
	datasetexists $vol || \
		log_fail "zfs create ${options[i]} -V $VOLSIZE $vol fail."

	log_must_busy zfs destroy -f $vol
	((i = i + 1))
done

log_pass "'zfs create [-b <blocksize>] -V <size> <volume>' works as expected."
