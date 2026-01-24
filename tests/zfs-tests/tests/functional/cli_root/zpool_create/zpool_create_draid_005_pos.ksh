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
# Copyright (c) 2026 Seagate Technology, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify creation of several failure groups/domains in one big row.
#
# STRATEGY:
# 1) Test valid stripe/spare/children/width combinations.
# 2) Test invalid stripe/spare/children/width combinations outside the
#    allowed limits.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	rm -f $draid_vdevs
	rmdir $TESTDIR
}

log_assert "'zpool create <pool> draid:#d:#c:#s:#w <vdevs>'"

log_onexit cleanup

mkdir $TESTDIR

# Generate 10 random valid configurations to test.
for (( i=0; i<10; i++ )); do
	parity=$(random_int_between 1 3)
	spares=$(random_int_between 0 3)
	data=$(random_int_between 1 16)
	n=$(random_int_between 2 7)

	(( min_children = (data + parity + spares) ))
	children=$(random_int_between $min_children 32)
	(( width = (children * n) ))

	draid="draid${parity}:${data}d:${children}c:${spares}s:${width}w"

	draid_vdevs=$(echo $TESTDIR/file.{01..$width})
	log_must truncate -s $MINVDEVSIZE $draid_vdevs

	log_must zpool create $TESTPOOL $draid $draid_vdevs
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL

	rm -f $draid_vdevs
done

children=32
draid_vdevs=$(echo $TESTDIR/file.{01..$children})
log_must truncate -s $MINVDEVSIZE $draid_vdevs

mkdir $TESTDIR
log_must truncate -s $MINVDEVSIZE $draid_vdevs

# Exceeds maximum data disks (limited by total children)
log_must zpool create $TESTPOOL draid2:14d:32w $draid_vdevs
log_must destroy_pool $TESTPOOL
log_mustnot zpool create $TESTPOOL draid2:14d:33w $draid_vdevs
log_mustnot zpool create $TESTPOOL draid2:14d:31w $draid_vdevs

# Width matches vdevs, but it must be multiple of children
log_mustnot zpool create $TESTPOOL draid2:13d:15c:32w $draid_vdevs

log_pass "'zpool create <pool> draid:#d:#c:#s:#w <vdevs>'"
