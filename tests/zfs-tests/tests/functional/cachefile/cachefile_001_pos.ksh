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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cachefile/cachefile.cfg
. $STF_SUITE/tests/functional/cachefile/cachefile.kshlib

#
# DESCRIPTION:
#
# Creating a pool with "cachefile" set doesn't update zpool.cache
#
# STRATEGY:
# 1. Create a pool with the cachefile property set
# 2. Verify that the pool doesn't have an entry in zpool.cache
# 3. Verify the cachefile property is set
# 4. Create a pool without the cachefile property
# 5. Verify the cachefile property isn't set
# 6. Verify that zpool.cache contains an entry for the pool
#

function cleanup
{
	typeset file

	if poolexists $TESTPOOL ; then
                destroy_pool $TESTPOOL
        fi
	for file in $CPATH1 $CPATH2 ; do
		if [[ -f $file ]] ; then
			log_must rm $file
		fi
	done
}

verify_runnable "global"

log_assert "Creating a pool with \"cachefile\" set doesn't update zpool.cache"
log_onexit cleanup

set -A opts "none" "false" "none" \
	"$CPATH" "true" "-" \
	"$CPATH1" "true" "$CPATH1" \
	"$CPATH2" "true" "$CPATH2"

typeset -i i=0

while (( i < ${#opts[*]} )); do
	log_must zpool create -o cachefile=${opts[i]} $TESTPOOL $DISKS
	case ${opts[((i+1))]} in
		false) log_mustnot pool_in_cache $TESTPOOL
			;;
		true) log_must pool_in_cache $TESTPOOL ${opts[i]}
			;;
	esac

	PROP=$(get_pool_prop cachefile $TESTPOOL)
	if [[ $PROP != ${opts[((i+2))]} ]]; then
		log_fail "cachefile property not set as expected. " \
			"Expect: ${opts[((i+2))]}, Current: $PROP"
	fi
	log_must zpool destroy $TESTPOOL
	(( i = i + 3 ))
done

log_pass "Creating a pool with \"cachefile\" set doesn't update zpool.cache"
