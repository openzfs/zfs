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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Executing 'zpool online' with valid parameters succeeds.
#
# STRATEGY:
# 1. Create an array of correctly formed 'zpool online' options
# 2. Execute each element of the array.
# 3. Verify use of each option is successful.
#

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)

set -A args ""

function cleanup
{
	#
	# Ensure we don't leave disks in temporary online state (-t)
	#
	for disk in $DISKLIST; do
		log_must zpool online $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "online"

	done
}

log_assert "Executing 'zpool online' with correct options succeeds"

log_onexit cleanup

if [[ -z $DISKLIST ]]; then
	log_fail "DISKLIST is empty."
fi

typeset -i i=0
typeset -i j=0

for disk in $DISKLIST; do
	i=0
	while [[ $i -lt ${#args[*]} ]]; do

		sync_pool $TESTPOOL
		log_must zpool offline $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "offline"

		log_must zpool online ${args[$i]} $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "online"

		while [[ $j -lt 20 ]]; do
			is_pool_resilvered $TESTPOOL && break
			sleep 0.5
			(( j = j + 1 ))
		done
		is_pool_resilvered $TESTPOOL || \
		    log_file "Pool didn't resilver after online"

		(( i = i + 1 ))
	done
done

log_note "Issuing repeated 'zpool online' commands succeeds."

typeset -i iters=20
typeset -i index=0

for disk in $DISKLIST; do
        i=0
        while [[ $i -lt $iters ]]; do
		index=`expr $RANDOM % ${#args[*]}`
                log_must zpool online ${args[$index]} $TESTPOOL $disk
                log_must check_state $TESTPOOL $disk "online"

                (( i = i + 1 ))
        done
done

log_pass "'zpool online' with correct options succeeded"
