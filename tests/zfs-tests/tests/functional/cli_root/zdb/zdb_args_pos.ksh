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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZDB allows a large number of possible inputs
# and combinations of those inputs. Test for non-zero
# exit values. These input options are based on the zdb
# man page
#
# STRATEGY:
# 1. Create an array containing value zdb parameters.
# 2. For each element, execute the sub-command.
# 3. Verify it does not return a error.
#

verify_runnable "global"

log_assert "Execute zdb using valid parameters."

log_onexit cleanup

function cleanup
{
	default_cleanup_noexit
}

function test_imported_pool
{
	typeset -a args=("-A" "-b" "-C" "-c" "-d" "-D" "-G" "-h" "-i" "-L" \
            "-M" "-P" "-s" "-v" "-Y" "-y")
	for i in ${args[@]}; do
		log_must eval "zdb $i $TESTPOOL >/dev/null"
	done
}

function test_exported_pool
{
	log_must zpool export $TESTPOOL
	typeset -a args=("-A" "-b" "-C" "-c" "-d" "-D" "-F" "-G" "-h" "-i" "-L" "-M" \
            "-P" "-s" "-v" "-X" "-Y" "-y")
	for i in ${args[@]}; do
		log_must eval "zdb -e $i $TESTPOOL >/dev/null"
	done
	log_must zpool import $TESTPOOL
}

function test_vdev
{
	typeset -a args=("-A" "-q" "-u" "-Aqu")
	VDEVS=$(get_pool_devices ${TESTPOOL} ${DEV_RDSKDIR})
	log_note $VDEVS
	set -A VDEV_ARRAY $VDEVS
	for i in ${args[@]}; do
		log_must eval "zdb -l $i ${VDEV_ARRAY[0]} >/dev/null"
	done
}

function test_metaslab
{
	typeset -a args=("-A" "-L" "-P" "-Y")
	for i in ${args[@]}; do
		log_must eval "zdb -m $i $TESTPOOL >/dev/null"
	done
}

default_mirror_setup_noexit $DISKS

test_imported_pool
test_exported_pool
test_vdev
test_metaslab

log_pass "Valid zdb parameters pass as expected."
