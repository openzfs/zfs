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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create -n <pool> <vspec> ...' can display the configuration without
# actually creating the pool.
#
# STRATEGY:
# 1. Create storage pool with -n option; this should only work when valid
#    properties are specified on the command line
# 2. Verify the pool has not been actually created
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool create -n <pool> ...' can display the configuration" \
        "without actually creating the pool."

log_onexit cleanup

typeset goodprops=('' '-o comment=text' '-O checksum=on' '-O ns:prop=value')
typeset badprops=('-o ashift=9999' '-O doesnotexist=on' '-O volsize=10M')

# Verify zpool create -n with valid pool-level and fs-level options
for prop in "${goodprops[@]}"
do
	#
	# Make sure disk is clean before we use it
	#
	log_must create_pool -p $TESTPOOL -d "$DISK0"
	destroy_pool $TESTPOOL

	str="would create '$TESTPOOL' with the following layout:"
	log_must_expect "$str" \
		create_pool -p $TESTPOOL -d "$DISK0" -e "-n $prop"

	poolexists $TESTPOOL && \
		log_fail "'zpool create -n <pool> ...' fail."
done

# Verify zpool create -n with invalid options
for prop in "${badprops[@]}"
do
	#
	# Make sure disk is clean before we use it
	#
	log_must create_pool -p $TESTPOOL -d "$DISK0"
	destroy_pool $TESTPOOL

	log_mustnot create_pool -p $TESTPOOL -d "$DISK0" -e "-n $prop"
done

log_pass "'zpool create -n <pool>...' success."
