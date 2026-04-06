#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2026, Christos Longros. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that user holds on datasets prevent destruction.
#	Verify that holds can be listed and released on datasets.
#	Verify that destruction succeeds after all holds are released.
#	Verify that 'zfs hold -r' applies to descendent datasets.
#	Verify that recursive destroy fails while a child dataset is held.
#
# STRATEGY:
#	1. Create a filesystem and a volume.
#	2. Place a user hold on the filesystem; verify destroy fails.
#	3. Place a second hold; release one at a time; verify destroy
#	   only succeeds after both are released.
#	4. Repeat for a volume.
#	5. Create a dataset hierarchy and verify 'zfs hold -r' holds
#	   every descendent and that 'zfs holds -r' lists holds on every
#	   descendent.
#	6. Verify 'zfs destroy -r' on the parent fails while a child is
#	   held and succeeds after the child's hold is released.
#

verify_runnable "both"

function cleanup
{
	for tag in hold1 hold2 rhold; do
		zfs release -r $tag $TESTPOOL/$TESTFS1 2>/dev/null
		zfs release $tag $TESTPOOL/$TESTVOL1 2>/dev/null
	done
	destroy_dataset $TESTPOOL/$TESTFS1/child 2>/dev/null
	destroy_dataset $TESTPOOL/$TESTFS1
	destroy_dataset $TESTPOOL/$TESTVOL1
}

log_assert "User holds on datasets prevent destruction"
log_onexit cleanup

TESTFS1=${TESTFS}-holddataset
TESTVOL1=${TESTVOL}-holddataset

# Create test datasets
log_must zfs create $TESTPOOL/$TESTFS1
if is_global_zone; then
	log_must zfs create -V 64M $TESTPOOL/$TESTVOL1
fi

# Test filesystem holds
log_must zfs hold hold1 $TESTPOOL/$TESTFS1
log_must eval "zfs holds $TESTPOOL/$TESTFS1 | grep -q hold1"
log_mustnot zfs destroy $TESTPOOL/$TESTFS1

# Multiple holds
log_must zfs hold hold2 $TESTPOOL/$TESTFS1
log_must eval "zfs holds $TESTPOOL/$TESTFS1 | grep -q hold1"
log_must eval "zfs holds $TESTPOOL/$TESTFS1 | grep -q hold2"
log_mustnot zfs destroy $TESTPOOL/$TESTFS1

# Release first hold, destroy should still fail
log_must zfs release hold1 $TESTPOOL/$TESTFS1
log_mustnot zfs destroy $TESTPOOL/$TESTFS1

# Release second hold, destroy should succeed
log_must zfs release hold2 $TESTPOOL/$TESTFS1
log_must zfs destroy $TESTPOOL/$TESTFS1

# Test volume holds
if is_global_zone; then
	log_must zfs hold hold1 $TESTPOOL/$TESTVOL1
	log_must eval "zfs holds $TESTPOOL/$TESTVOL1 | grep -q hold1"
	log_mustnot zfs destroy $TESTPOOL/$TESTVOL1
	log_must zfs release hold1 $TESTPOOL/$TESTVOL1
	log_must zfs destroy $TESTPOOL/$TESTVOL1
fi

# Recursive hold on a dataset hierarchy
log_must zfs create $TESTPOOL/$TESTFS1
log_must zfs create $TESTPOOL/$TESTFS1/child
log_must zfs hold -r rhold $TESTPOOL/$TESTFS1
log_must eval "zfs holds $TESTPOOL/$TESTFS1 | grep -q rhold"
log_must eval "zfs holds $TESTPOOL/$TESTFS1/child | grep -q rhold"

# 'zfs holds -r' on a dataset descends into children
log_must eval "[[ \$(zfs holds -r $TESTPOOL/$TESTFS1 | grep -c rhold) -eq 2 ]]"

# Recursive destroy must fail while any child is held
log_mustnot zfs destroy -r $TESTPOOL/$TESTFS1

# Release the child's hold, parent's hold still blocks destroy
log_must zfs release rhold $TESTPOOL/$TESTFS1/child
log_mustnot zfs destroy -r $TESTPOOL/$TESTFS1

# Release parent's hold; recursive destroy now succeeds
log_must zfs release rhold $TESTPOOL/$TESTFS1
log_must zfs destroy -r $TESTPOOL/$TESTFS1

log_pass "User holds on datasets prevent destruction"
