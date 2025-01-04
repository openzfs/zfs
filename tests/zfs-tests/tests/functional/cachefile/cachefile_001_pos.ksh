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
