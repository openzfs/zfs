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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

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
feature@async_destroy=disabled \
feature@async_destroy=active \
feature@xxx_fake_xxx=enabled \
unsupported@some_feature=inactive \
unsupported@some_feature=readonly \
"

function cleanup
{
	datasetexists $TESTPOOL && log_must $ZPOOL destroy $TESTPOOL
}

log_assert "'zpool create' with invalid feature names/states fails"
log_onexit cleanup

for prop in $properties; do
	log_mustnot $ZPOOL create -f -o "$prop" $TESTPOOL $DISKS
	log_mustnot datasetexists $TESTPOOL
done

log_pass
