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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	Verify zpool create succeed with multiple keywords combination.
#
# STRATEGY:
#	1. Create base filesystem to hold virtual disk files.
#	2. Create several files == $MINVDEVSIZE.
#	3. Verify 'zpool create' succeed with valid keywords combination.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}


log_assert "Verify 'zpool create' succeed with keywords combination."
log_onexit cleanup

create_pool $TESTPOOL $DISKS
mntpnt=$(get_prop mountpoint $TESTPOOL)

typeset -i i=0
while ((i < 10)); do
	log_must truncate -s $MINVDEVSIZE $mntpnt/vdev$i

	eval vdev$i=$mntpnt/vdev$i
	((i += 1))
done

set -A valid_args \
	"mirror $vdev0 $vdev1 $vdev2 mirror $vdev3 $vdev4 $vdev5" \
	"mirror $vdev0 $vdev1 mirror $vdev2 $vdev3 mirror $vdev4 $vdev5" \
	"mirror $vdev0 $vdev1 $vdev2 mirror $vdev3 $vdev4 $vdev5 \
		spare $vdev6" \
	"mirror $vdev0 $vdev1 mirror $vdev2 $vdev3 mirror $vdev4 $vdev5 \
		spare $vdev6 $vdev7" \
	"mirror $vdev0 $vdev1 spare $vdev2 mirror $vdev3 $vdev4" \
	"mirror $vdev0 $vdev1 raidz $vdev2 $vdev3" \
	"mirror $vdev0 $vdev1 raidz $vdev2 $vdev3 $vdev4" \
	"mirror $vdev0 $vdev1 $vdev2 raidz2 $vdev3 $vdev4 $vdev5" \
	"mirror $vdev0 $vdev1 $vdev2 $vdev3 \
		raidz3 $vdev4 $vdev5 $vdev6 $vdev7" \
	"raidz $vdev0 $vdev1 $vdev2 mirror $vdev3 $vdev4" \
	"raidz $vdev0 $vdev1 $vdev2 raidz1 $vdev3 $vdev4 $vdev5" \
	"raidz $vdev0 $vdev1 raidz1 $vdev2 $vdev3 raidz $vdev4 $vdev5" \
	"raidz $vdev0 $vdev1 $vdev2 raidz1 $vdev3 $vdev4 $vdev5 \
		spare $vdev6" \
	"raidz $vdev0 $vdev1 raidz1 $vdev2 $vdev3 raidz $vdev4 $vdev5 \
		spare $vdev6 $vdev7" \
	"raidz $vdev0 $vdev1 spare $vdev2 raidz $vdev3 $vdev4" \
	"raidz2 $vdev0 $vdev1 $vdev2 mirror $vdev3 $vdev4 $vdev5" \
	"raidz2 $vdev0 $vdev1 $vdev2 raidz2 $vdev3 $vdev4 $vdev5" \
	"raidz2 $vdev0 $vdev1 $vdev2 raidz2 $vdev3 $vdev4 $vdev5 \
		raidz2 $vdev6 $vdev7 $vdev8" \
	"raidz2 $vdev0 $vdev1 $vdev2 raidz2 $vdev3 $vdev4 $vdev5 \
		spare $vdev6" \
	"raidz2 $vdev0 $vdev1 $vdev2 raidz2 $vdev3 $vdev4 $vdev5 \
		raidz2 $vdev6 $vdev7 $vdev8 spare $vdev9" \
	"raidz2 $vdev0 $vdev1 $vdev2 spare $vdev3 raidz2 $vdev4 $vdev5 $vdev6" \
	"raidz3 $vdev0 $vdev1 $vdev2 $vdev3 \
		mirror $vdev4 $vdev5 $vdev6 $vdev7" \
	"draid $vdev0 $vdev1 $vdev2 mirror $vdev3 $vdev4" \
	"draid $vdev0 $vdev1 $vdev2 raidz1 $vdev3 $vdev4 $vdev5" \
	"draid $vdev0 $vdev1 $vdev2 draid1 $vdev3 $vdev4 $vdev5" \
	"draid $vdev0 $vdev1 $vdev2 special mirror $vdev3 $vdev4" \
	"draid2 $vdev0 $vdev1 $vdev2 $vdev3 mirror $vdev4 $vdev5 $vdev6" \
	"draid2 $vdev0 $vdev1 $vdev2 $vdev3 raidz2 $vdev4 $vdev5 $vdev6" \
	"draid2 $vdev0 $vdev1 $vdev2 $vdev3 draid2 $vdev4 $vdev5 $vdev6 $vdev7"\
	"draid2 $vdev0 $vdev1 $vdev2 $vdev3 \
		special mirror $vdev4 $vdev5 $vdev6" \
	"draid2 $vdev0 $vdev1 $vdev2 $vdev3 \
		special mirror $vdev4 $vdev5 $vdev6 \
		cache $vdev7 log mirror $vdev8 $vdev9" \
	"draid $vdev0 $vdev1 $vdev2 draid $vdev4 $vdev5 $vdev6 $vdev7 \
		special mirror $vdev8 $vdev9" \
	"spare $vdev0 $vdev1 $vdev2 mirror $vdev3 $vdev4 raidz $vdev5 $vdev6"

set -A forced_args \
	"$vdev0 raidz $vdev1 $vdev2 raidz1 $vdev3 $vdev4 $vdev5" \
	"$vdev0 raidz2 $vdev1 $vdev2 $vdev3 raidz2 $vdev4 $vdev5 $vdev6" \
	"$vdev0 mirror $vdev1 $vdev2 mirror $vdev3 $vdev4" \
	"$vdev0 mirror $vdev1 $vdev2 raidz $vdev3 $vdev4 \
		raidz2 $vdev5 $vdev6 $vdev7 spare $vdev8" \
	"$vdev0 mirror $vdev1 $vdev2 spare $vdev3 raidz $vdev4 $vdev5" \
	"raidz $vdev0 $vdev1 raidz2 $vdev2 $vdev3 $vdev4" \
	"raidz $vdev0 $vdev1 raidz2 $vdev2 $vdev3 $vdev4 spare $vdev5" \
	"raidz $vdev0 $vdev1 spare $vdev2 raidz2 $vdev3 $vdev4 $vdev5" \
	"raidz $vdev0 $vdev1 draid2 $vdev2 $vdev3 $vdev4 $vdev5" \
	"raidz $vdev0 $vdev1 draid3 $vdev2 $vdev3 $vdev4 $vdev5 $vdev6" \
	"mirror $vdev0 $vdev1 raidz $vdev2 $vdev3 raidz2 $vdev4 $vdev5 $vdev6" \
	"mirror $vdev0 $vdev1 raidz $vdev2 $vdev3 \
		raidz2 $vdev4 $vdev5 $vdev6 spare $vdev7" \
	"mirror $vdev0 $vdev1 raidz $vdev2 $vdev3 \
		spare $vdev4 raidz2 $vdev5 $vdev6 $vdev7" \
	"mirror $vdev0 $vdev1 draid $vdev2 $vdev3 $vdev4 \
		draid2 $vdev5 $vdev6 $vdev7 $vdev8 spare $vdev9" \
	"draid $vdev0 $vdev1 $vdev2 $vdev3 \
		draid2 $vdev4 $vdev5 $vdev6 $vdev7 $vdev8" \
	"draid $vdev0 $vdev1 $vdev2 draid $vdev4 $vdev5 $vdev6 \
		special mirror $vdev7 $vdev8 $vdev9" \
	"spare $vdev0 $vdev1 $vdev2 mirror $vdev3 $vdev4 \
		raidz2 $vdev5 $vdev6 $vdev7"

i=0
while ((i < ${#valid_args[@]})); do
	log_must zpool create $TESTPOOL1 ${valid_args[$i]}
	log_must zpool destroy -f $TESTPOOL1

	((i += 1))
done

i=0
while ((i < ${#forced_args[@]})); do
	log_mustnot zpool create $TESTPOOL1 ${forced_args[$i]}
	log_must zpool create -f $TESTPOOL1 ${forced_args[$i]}
	log_must zpool destroy -f $TESTPOOL1

	((i += 1))
done

log_pass "'zpool create' succeed with keywords combination."
