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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/online_offline/online_offline.cfg

#
# DESCRIPTION:
# Turning disks in a pool offline should fail when there is no longer
# sufficient redundancy.
#
# STRATEGY:
# 1. Start some random I/O on the mirror or raidz.
# 2. Verify that we can offline as many disks as the redundancy level
# will support, but not more.
# 3. Verify the integrity of the file system and the resilvering.
#

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)

function cleanup
{
	kill $killpid >/dev/null 2>&1

	#
	# Ensure we don't leave disks in the offline state
	#
	for disk in $DISKLIST; do
		log_must zpool online $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "online"
	done
	sleep 1 # Delay for resilver to start
	log_must zpool wait -t resilver $TESTPOOL

	[[ -e $TESTDIR ]] && log_must rm -rf $TESTDIR/*
}

log_assert "Turning both disks offline should fail."

log_onexit cleanup

file_trunc -f $((64 * 1024 * 1024)) -b 8192 -c 0 -r $TESTDIR/$TESTFILE1 &
typeset killpid="$! "

disks=($DISKLIST)

#
# The setup script will give us either a mirror or a raidz. The former can have
# all but one vdev offlined, whereas with raidz there can be only one.
#
pooltype='mirror'
zpool list -v $TESTPOOL | grep raidz >/dev/null 2>&1 && pooltype='raidz'

typeset -i i=0
while [[ $i -lt ${#disks[*]} ]]; do
	typeset -i j=0
	if [[ $pooltype = 'mirror' ]]; then
		# Hold one disk online, verify the others can be offlined.
		log_must zpool online $TESTPOOL ${disks[$i]}
		check_state $TESTPOOL ${disks[$i]} "online" || \
		    log_fail "Failed to set ${disks[$i]} online"
		sleep 1 # Delay for resilver to start
		log_must zpool wait -t resilver $TESTPOOL
		log_must zpool clear $TESTPOOL
		while [[ $j -lt ${#disks[*]} ]]; do
			if [[ $j -eq $i ]]; then
				((j++))
				continue
			fi
			log_must zpool offline $TESTPOOL ${disks[$j]}
			check_state $TESTPOOL ${disks[$j]} "offline" || \
			    log_fail "Failed to set ${disks[$j]} offline"
			((j++))
		done
	elif [[ $pooltype = 'raidz' ]]; then
		# Hold one disk offline, verify the others can't be offlined.
		log_must zpool offline $TESTPOOL ${disks[$i]}
		check_state $TESTPOOL ${disks[$i]} "offline" || \
		    log_fail "Failed to set ${disks[$i]} offline"
		while [[ $j -lt ${#disks[*]} ]]; do
			if [[ $j -eq $i ]]; then
				((j++))
				continue
			fi
			log_mustnot zpool offline $TESTPOOL ${disks[$j]}
			check_state $TESTPOOL ${disks[$j]} "online" || \
			    log_fail "Failed to set ${disks[$j]} online"
			check_state $TESTPOOL ${disks[$i]} "offline" || \
			    log_fail "Failed to set ${disks[$i]} offline"
			((j++))
		done
		log_must zpool online $TESTPOOL ${disks[$i]}
		check_state $TESTPOOL ${disks[$i]} "online" || \
		    log_fail "Failed to set ${disks[$i]} online"
		sleep 1 # Delay for resilver to start
		log_must zpool wait -t resilver $TESTPOOL
		log_must zpool clear $TESTPOOL
	fi
	((i++))
done

log_must kill $killpid
sync_all_pools
log_must sync

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass
