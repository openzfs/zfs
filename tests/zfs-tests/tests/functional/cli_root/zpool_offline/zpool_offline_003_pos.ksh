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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test force faulting a VDEV with 'zpool offline -f'
#
# STRATEGY:
# For both temporary and persistent faults, do the following:
# 1. Force fault a vdev, and clear the fault.
# 2. Offline a vdev, force fault it, clear the fault, and online it.
# 3. Force fault a vdev, export it, then import it.

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)
set -A disks $DISKLIST
typeset -i num=${#disks[*]}

set -A args "" "-t"

function cleanup
{
	# Ensure we don't leave disks in the offline state
	for disk in $DISKLIST; do
		log_must zpool online $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "online"
	done
}

log_assert "Executing 'zpool offline -f' with correct options succeeds"

log_onexit cleanup

if [[ -z $DISKLIST ]]; then
	log_fail "DISKLIST is empty."
fi

typeset -i i=0
typeset -i j=1

# Get name of the first disk in the pool
disk=${DISKLIST%% *}

# Test temporary and persistent faults
for arg in f tf ; do
	# Force fault disk, and clear the fault
	log_must zpool offline -$arg $TESTPOOL $disk
	check_state $TESTPOOL $disk "faulted"
	log_must zpool clear $TESTPOOL $disk
	check_state $TESTPOOL $disk "online"

	# Offline a disk, force fault it, clear the fault, and online it
	log_must zpool offline $TESTPOOL $disk
	check_state $TESTPOOL $disk "offline"
	log_must zpool offline -$arg $TESTPOOL $disk
	check_state $TESTPOOL $disk "faulted"
	log_must zpool clear $TESTPOOL $disk
	check_state $TESTPOOL $disk "offline"
	log_must zpool online $TESTPOOL $disk
	check_state $TESTPOOL $disk "online"

	# Test faults across imports
	log_must zpool offline -tf $TESTPOOL $disk
	check_state $TESTPOOL $disk "faulted"
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	log_note "-$arg now imported"
	if [[ "$arg" = "f" ]] ; then
		# Persistent fault
		check_state $TESTPOOL $disk "faulted"
		log_must zpool clear $TESTPOOL $disk
	else
		# Temporary faults get cleared by imports
		check_state $TESTPOOL $disk "online"
	fi
done
log_pass "'zpool offline -f' with correct options succeeded"
