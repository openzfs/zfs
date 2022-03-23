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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Executing 'zpool offline' with valid parameters succeeds.
#
# STRATEGY:
# 1. Create an array of correctly formed 'zpool offline' options
# 2. Execute each element of the array.
# 3. Verify use of each option is successful.

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)
set -A disks $DISKLIST
typeset -i num=${#disks[*]}

set -A args "" "-t"

function cleanup
{
	#
	# Ensure we don't leave disks in the offline state
	#
	for disk in $DISKLIST; do
		log_must zpool online $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "online"

	done
}

log_assert "Executing 'zpool offline' with correct options succeeds"

log_onexit cleanup

if [[ -z $DISKLIST ]]; then
	log_fail "DISKLIST is empty."
fi

typeset -i i=0
typeset -i j=1

for disk in $DISKLIST; do
	i=0
	while [[ $i -lt ${#args[*]} ]]; do
		if (( j < num )) ; then
			log_must zpool offline ${args[$i]} $TESTPOOL $disk
			log_must check_state $TESTPOOL $disk "offline"
		else
			log_mustnot zpool offline ${args[$i]} $TESTPOOL $disk
			log_must check_state $TESTPOOL $disk "online"
		fi

		(( i = i + 1 ))
	done
	(( j = j + 1 ))
done

log_note "Issuing repeated 'zpool offline' commands succeeds."

typeset -i iters=20
typeset -i index=0

for disk in $DISKLIST; do
        i=0
        while [[ $i -lt $iters ]]; do
		index=`expr $RANDOM % ${#args[*]}`
                log_must zpool offline ${args[$index]} $TESTPOOL $disk
                log_must check_state $TESTPOOL $disk "offline"

                (( i = i + 1 ))
        done

	log_must zpool online $TESTPOOL $disk
	log_must check_state $TESTPOOL $disk "online"
done

log_pass "'zpool offline -f' succeeded"
