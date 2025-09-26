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
# Copyright 2012, 2016 by Delphix. All rights reserved.
# Copyright 2025 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
#	Verify zpool add succeeds when adding vdevs with matching redundancy
#	and warns with differing redundancy for a degraded pool.
#
# STRATEGY:
#	1. Create several files == $MINVDEVSIZE.
#	2. Verify 'zpool add' succeeds with matching redundancy
#	3. Verify 'zpool add' warns with differing redundancy when
#	  a. Degraded pool with replaced mismatch vdev (file vs disk)
#	  b. Degraded pool dRAID distributed spare active
#	  c. Degraded pool hot spare active
#

verify_runnable "global"

log_assert "Verify 'zpool add' warns for differing redundancy."
log_onexit zpool_create_add_cleanup

zpool_create_add_setup

set -A redundancy1_create_args \
	"mirror $vdev0 $vdev1" \
	"raidz1 $vdev0 $vdev1" \
	"draid1:1s $vdev0 $vdev1 $vdev9"

set -A redundancy2_create_args \
	"mirror $vdev0 $vdev1 $vdev2" \
	"raidz2 $vdev0 $vdev1 $vdev2" \
	"draid2:1s $vdev0 $vdev1 $vdev2 $vdev9"

set -A redundancy3_create_args \
	"mirror $vdev0 $vdev1 $vdev2 $vdev3" \
	"raidz3 $vdev0 $vdev1 $vdev2 $vdev3" \
	"draid3:1s $vdev0 $vdev1 $vdev2 $vdev3 $vdev9"

set -A redundancy1_add_args \
	"mirror $vdev5 $vdev6" \
	"raidz1 $vdev5 $vdev6" \
	"raidz1 $vdev5 $vdev6 mirror $vdev7 $vdev8" \
	"mirror $vdev5 $vdev6 raidz1 $vdev7 $vdev8" \
	"draid1 $vdev5 $vdev6 mirror $vdev7 $vdev8" \
	"mirror $vdev5 $vdev6 draid1 $vdev7 $vdev8"

set -A redundancy2_add_args \
	"mirror $vdev5 $vdev6 $vdev7" \
	"raidz2 $vdev5 $vdev6 $vdev7" \
	"draid2 $vdev5 $vdev6 $vdev7"

set -A redundancy3_add_args \
	"mirror $vdev5 $vdev6 $vdev7 $vdev8" \
	"raidz3 $vdev5 $vdev6 $vdev7 $vdev8" \
	"draid3 $vdev5 $vdev6 $vdev7 $vdev8"

set -A redundancy1_create_draid_args \
	"draid1:1s $vdev0 $vdev1 $vdev2"

set -A redundancy2_create_draid_args \
	"draid2:1s $vdev0 $vdev1 $vdev2 $vdev3"

set -A redundancy3_create_draid_args \
	"draid3:1s $vdev0 $vdev1 $vdev2 $vdev3 $vdev9"

set -A redundancy1_create_spare_args \
	"mirror $vdev0 $vdev1 spare $vdev_lo" \
	"raidz1 $vdev0 $vdev1 spare $vdev_lo" \
	"draid1 $vdev0 $vdev1 spare $vdev_lo"

set -A redundancy2_create_spare_args \
	"mirror $vdev0 $vdev1 $vdev2 spare $vdev_lo" \
	"raidz2 $vdev0 $vdev1 $vdev2 spare $vdev_lo" \
	"draid2 $vdev0 $vdev1 $vdev2 spare $vdev_lo"

set -A redundancy3_create_spare_args \
	"mirror $vdev0 $vdev1 $vdev2 $vdev3 spare $vdev_lo" \
	"raidz3 $vdev0 $vdev1 $vdev2 $vdev3 spare $vdev_lo" \
	"draid3 $vdev0 $vdev1 $vdev2 $vdev3 spare $vdev_lo"

set -A replace_args "$vdev1" "$vdev_lo"
set -A draid1_args "$vdev1" "draid1-0-0"
set -A draid2_args "$vdev1" "draid2-0-0"
set -A draid3_args "$vdev1" "draid3-0-0"

typeset -i i=0
typeset -i j=0

function zpool_create_degraded_add
{
	typeset -n create_args=$1
	typeset -n add_args=$2
	typeset -n rm_args=$3

	i=0
	while ((i < ${#create_args[@]})); do
		j=0
		while ((j < ${#add_args[@]})); do
			log_must zpool create $TESTPOOL1 ${create_args[$i]}
			log_must zpool offline -f $TESTPOOL1 ${rm_args[0]}
			log_must zpool replace -w $TESTPOOL1 ${rm_args[0]} ${rm_args[1]}
			log_must zpool add $TESTPOOL1 ${add_args[$j]}
			log_must zpool destroy -f $TESTPOOL1
			log_must zpool labelclear -f ${rm_args[0]}

			((j += 1))
		done
		((i += 1))
	done
}

function zpool_create_forced_degraded_add
{
	typeset -n create_args=$1
	typeset -n add_args=$2
	typeset -n rm_args=$3

	i=0
	while ((i < ${#create_args[@]})); do
		j=0
		while ((j < ${#add_args[@]})); do
			log_must zpool create $TESTPOOL1 ${create_args[$i]}
			log_must zpool offline -f $TESTPOOL1 ${rm_args[0]}
			log_must zpool replace -w $TESTPOOL1 ${rm_args[0]} ${rm_args[1]}
			log_mustnot zpool add $TESTPOOL1 ${add_args[$j]}
			log_must zpool add --allow-replication-mismatch $TESTPOOL1 ${add_args[$j]}
			log_must zpool destroy -f $TESTPOOL1
			log_must zpool labelclear -f ${rm_args[0]}

			((j += 1))
		done
		((i += 1))
	done
}

# 2. Verify 'zpool add' succeeds with matching redundancy and a degraded pool.
zpool_create_degraded_add redundancy1_create_args redundancy1_add_args replace_args
zpool_create_degraded_add redundancy2_create_args redundancy2_add_args replace_args
zpool_create_degraded_add redundancy3_create_args redundancy3_add_args replace_args

# 3. Verify 'zpool add' warns with differing redundancy and a degraded pool.
#
# a. Degraded pool with replaced mismatch vdev (file vs disk)
zpool_create_forced_degraded_add redundancy1_create_args redundancy2_add_args replace_args
zpool_create_forced_degraded_add redundancy1_create_args redundancy3_add_args replace_args

zpool_create_forced_degraded_add redundancy2_create_args redundancy1_add_args replace_args
zpool_create_forced_degraded_add redundancy2_create_args redundancy3_add_args replace_args

zpool_create_forced_degraded_add redundancy3_create_args redundancy1_add_args replace_args
zpool_create_forced_degraded_add redundancy3_create_args redundancy2_add_args replace_args

# b. Degraded pool dRAID distributed spare active

zpool_create_forced_degraded_add redundancy1_create_draid_args redundancy2_add_args draid1_args
zpool_create_forced_degraded_add redundancy1_create_draid_args redundancy3_add_args draid1_args

zpool_create_forced_degraded_add redundancy2_create_draid_args redundancy1_add_args draid2_args
zpool_create_forced_degraded_add redundancy2_create_draid_args redundancy3_add_args draid2_args

zpool_create_forced_degraded_add redundancy3_create_draid_args redundancy1_add_args draid3_args
zpool_create_forced_degraded_add redundancy3_create_draid_args redundancy2_add_args draid3_args

# c. Degraded pool hot spare active
zpool_create_forced_degraded_add redundancy1_create_spare_args redundancy2_add_args replace_args
zpool_create_forced_degraded_add redundancy1_create_spare_args redundancy3_add_args replace_args

zpool_create_forced_degraded_add redundancy2_create_spare_args redundancy1_add_args replace_args
zpool_create_forced_degraded_add redundancy2_create_spare_args redundancy3_add_args replace_args

zpool_create_forced_degraded_add redundancy3_create_spare_args redundancy1_add_args replace_args
zpool_create_forced_degraded_add redundancy3_create_spare_args redundancy2_add_args replace_args

log_pass "Verify 'zpool add' warns for differing redundancy."
