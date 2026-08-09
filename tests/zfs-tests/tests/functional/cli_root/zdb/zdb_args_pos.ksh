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
