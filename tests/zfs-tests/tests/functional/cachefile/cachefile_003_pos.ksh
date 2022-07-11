#!/bin/ksh -p
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
# Setting altroot=<path> and cachefile=$CPATH for zpool create is succeed
#
# STRATEGY:
# 1. Attempt to create a pool with -o altroot=<path> -o cachefile=<value>
# 2. Verify the command succeed
#
#

TESTDIR=/altdir.$$

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

	if [ -d $TESTDIR ]
	then
		rmdir $TESTDIR
	fi
}

verify_runnable "global"

log_assert "Setting altroot=path and cachefile=$CPATH for zpool create succeed."
log_onexit cleanup

typeset -i i=0

set -A opts "none" "none" \
	"$CPATH" "-" \
	"$CPATH1" "$CPATH1" \
	"$CPATH2" "$CPATH2"


while (( i < ${#opts[*]} )); do
	log_must zpool create -o altroot=$TESTDIR -o cachefile=${opts[i]} \
		$TESTPOOL $DISKS
	if [[ ${opts[i]} != none ]]; then
		log_must pool_in_cache $TESTPOOL ${opts[i]}
	else
		log_mustnot pool_in_cache $TESTPOOL
	fi

	PROP=$(get_pool_prop cachefile $TESTPOOL)
	if [[ $PROP != ${opts[((i+1))]} ]]; then
		log_fail "cachefile property not set as expected. " \
			"Expect: ${opts[((i+1))]}, Current: $PROP"
	fi
	log_must zpool destroy $TESTPOOL
	(( i = i + 2 ))
done

log_pass "Setting altroot=path and cachefile=$CPATH for zpool create succeed."
