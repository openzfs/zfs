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
