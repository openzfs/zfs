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
	#
	# Ensure we don't leave disks in the offline state
	#
	for disk in $DISKLIST; do
		log_must zpool online $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "online"
	done

	kill $killpid >/dev/null 2>&1
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
