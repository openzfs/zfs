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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zfs rename should work on existing datasets that exceed
#	zfs_max_dataset_nesting (our nesting limit) except in the
#	scenario that we try to rename it to something deeper
#	than it already is.
#
# STRATEGY:
#	1. Create a set of ZFS datasets within our nesting limit.
#	2. Try renaming one of them on top of the other so its
#	   children pass the limit - it should fail.
#	3. Increase the nesting limit.
#	4. Check that renaming a dataset on top of the other
#	   cannot exceed the new nesting limit but can exceed
#	   the old one.
#	5. Bring back the old nesting limit so you can simulate
#	   the scenario of existing datasets that exceed our
#	   nesting limit.
#	6. Make sure that 'zfs rename' can work only if we are
#	   trying to keep existing datasets that exceed the limit
#	   at the same nesting level or less. Making it even
#	   deeper should not work.
#

verify_runnable "both"

dsA01="a"
dsA02="a/a"
dsA49=$(printf 'a/%.0s' {1..48})"a"

dsB01="b"
dsB49=$(printf 'b/%.0s' {1..48})"b"

dsC01="c"
dsC16=$(printf 'c/%.0s' {1..15})"c"

dsB16A=$(printf 'b/%.0s' {1..16})"a"
dsB15A=$(printf 'b/%.0s' {1..15})"a"

dsB15A47A=$(printf 'b/%.0s' {1..15})$(printf 'a/%.0s' {1..47})"a"
dsB15A47C=$(printf 'b/%.0s' {1..15})$(printf 'a/%.0s' {1..47})"c"

dsB15A40B=$(printf 'b/%.0s' {1..15})$(printf 'a/%.0s' {1..40})"b"
dsB15A47B=$(printf 'b/%.0s' {1..15})$(printf 'a/%.0s' {1..47})"b"

function nesting_cleanup
{
	log_must zfs destroy -fR $TESTPOOL/$dsA01
	log_must zfs destroy -fR $TESTPOOL/$dsB01
	log_must zfs destroy -fR $TESTPOOL/$dsC01

	# If the test fails after increasing the limit and
	# before resetting it, it will be left at the modified
	# value for the remaining tests. That's the reason
	# we reset it again here just in case.
	log_must set_tunable64 MAX_DATASET_NESTING 50 Z
}

log_onexit nesting_cleanup

log_must zfs create -p $TESTPOOL/$dsA49
log_must zfs create -p $TESTPOOL/$dsB49
log_must zfs create -p $TESTPOOL/$dsC16

log_mustnot zfs rename $TESTPOOL/$dsA02 $TESTPOOL/$dsB15A

# extend limit
log_must set_tunable64 MAX_DATASET_NESTING 64 Z

log_mustnot zfs rename $TESTPOOL/$dsA02 $TESTPOOL/$dsB16A
log_must zfs rename $TESTPOOL/$dsA02 $TESTPOOL/$dsB15A

# bring back old limit
log_must set_tunable64 MAX_DATASET_NESTING 50 Z

log_mustnot zfs rename $TESTPOOL/$dsC01 $TESTPOOL/$dsB15A47C
log_must zfs rename $TESTPOOL/$dsB15A47A $TESTPOOL/$dsB15A47B
log_must zfs rename $TESTPOOL/$dsB15A47B $TESTPOOL/$dsB15A40B

log_pass "Verify 'zfs rename' limits datasets so they don't pass " \
	"the nesting limit. For existing ones that do, it should " \
	"not allow them to grow anymore."
