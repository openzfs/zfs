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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

################################################################################
#
#  Specifying invalid feature names/states should cause the create to fail.
#
#  1. Try to create the pool with a variety of invalid feature names/states.
#  2. Verify no pool was created.
#
################################################################################

verify_runnable "global"

properties="\
feature@async_destroy=disable \
feature@async_destroy=active \
feature@xxx_fake_xxx=enabled \
unsupported@some_feature=inactive \
unsupported@some_feature=readonly \
"

function cleanup
{
	datasetexists $TESTPOOL && log_must zpool destroy $TESTPOOL
}

log_assert "'zpool create' with invalid feature names/states fails"
log_onexit cleanup

for prop in $properties; do
	log_mustnot zpool create -f -o "$prop" $TESTPOOL $DISKS
	log_mustnot datasetexists $TESTPOOL
done

log_pass
