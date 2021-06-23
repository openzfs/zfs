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
# Copyright (c) 2021 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify '-o compatibility' property is updated in both the
#	pool config MOS object and the cache file.
#
# STRATEGY:
#	1. Create a pool with '-o compatibility=legacy', then verify
#	   the property exists in the MOS config and cache file.
#	2. Create a pool, set the 'compatibility=off' property, then
#	   verify the property exists in the MOS config and cache file.
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && log_must zpool destroy $TESTPOOL
	rm -f $CACHE_FILE
}

function check_config
{
	typeset propval=$1

	poolval="$(zpool get -H -o value compatibility $TESTPOOL)"
	if [ "$poolval" != "$propval" ]; then
		log_fail "compatibility property set incorrectly $curval"
	fi

	if ! zdb -C -U $CACHE_FILE | grep "compatibility: '$propval'"; then
		log_fail "compatibility property missing in cache file"
	fi

	if ! zdb -C -U $CACHE_FILE $TESTPOOL | grep "compatibility: '$propval'"; then
		log_fail "compatibility property missing from MOS object"
	fi
}

log_onexit cleanup

log_assert "verify '-o compatibility' in MOS object and cache file"

CACHE_FILE=$TEST_BASE_DIR/cachefile.$$

# 1. Create a pool with '-o compatibility=legacy', then verify
#    the property exists in the MOS config and cache file.
log_must zpool create -f -o cachefile=$CACHE_FILE -o compatibility=legacy $TESTPOOL $DISKS
log_must check_config legacy
log_must zpool export -F $TESTPOOL
log_must zpool import -c $CACHE_FILE $TESTPOOL
log_must check_config legacy
log_must zpool destroy -f $TESTPOOL

# 2. Create a pool, set the 'compatibility=off' property, then
#    verify the property exists in the MOS config and cache file.
log_must zpool create -f -o cachefile=$CACHE_FILE $TESTPOOL $DISKS
log_must zpool set compatibility=legacy $TESTPOOL
log_must check_config legacy
log_must zpool export -F $TESTPOOL
log_must zpool import -c $CACHE_FILE $TESTPOOL
log_must check_config legacy
log_must zpool destroy -f $TESTPOOL

log_pass "verify '-o compatibility' in MOS object and cache file"
