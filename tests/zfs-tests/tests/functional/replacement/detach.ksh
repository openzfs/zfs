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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# 	Detaching disks during I/O should pass for supported pools.
#
# STRATEGY:
#	1. Create multidisk pools (stripe/mirror/raidz/draid) and
#	   start some random I/O
#	2. Detach a disk from the pool.
#	3. Verify the integrity of the file system and the resilvering.
#

verify_runnable "global"

function cleanup
{
	if [[ -n "$child_pids" ]]; then
		for wait_pid in $child_pids
		do
		        kill $wait_pid
		done
	fi

	if poolexists $TESTPOOL1; then
		destroy_pool $TESTPOOL1
	fi

	[[ -e $TESTDIR ]] && log_must rm -rf $TESTDIR/*
}

log_assert "Replacing a disk during I/O completes."

options=""
options_display="default options"

log_onexit cleanup

[[ -n "$HOLES_FILESIZE" ]] && options=" $options -f $HOLES_FILESIZE "

[[ -n "$HOLES_BLKSIZE" ]] && options="$options -b $HOLES_BLKSIZE "

[[ -n "$HOLES_COUNT" ]] && options="$options -c $HOLES_COUNT "

[[ -n "$HOLES_SEED" ]] && options="$options -s $HOLES_SEED "

[[ -n "$HOLES_FILEOFFSET" ]] && options="$options -o $HOLES_FILEOFFSET "

ptions="$options -r "

[[ -n "$options" ]] && options_display=$options

child_pids=""

function detach_test
{
	typeset -i iters=2
	typeset -i index=0
	typeset disk1=$1

	typeset i=0
	while [[ $i -lt $iters ]]; do
		log_note "Invoking file_trunc with: $options_display"
		file_trunc $options $TESTDIR/$TESTFILE.$i &
		typeset pid=$!

		sleep 1

		child_pids="$child_pids $pid"
		((i = i + 1))
	done

	log_must zpool detach $TESTPOOL1 $disk1

	sleep 10

	for wait_pid in $child_pids
	do
		kill $wait_pid
	done
	child_pids=""

        log_must zpool export $TESTPOOL1
        log_must zpool import -d $TESTDIR $TESTPOOL1
        log_must zfs umount $TESTPOOL1/$TESTFS1
        log_must zdb -cdui $TESTPOOL1/$TESTFS1
        log_must zfs mount $TESTPOOL1/$TESTFS1
}

specials_list=""
i=0
while [[ $i != 3 ]]; do
	truncate -s $MINVDEVSIZE $TESTDIR/$TESTFILE1.$i
	specials_list="$specials_list $TESTDIR/$TESTFILE1.$i"

	((i = i + 1))
done

create_pool $TESTPOOL1 mirror $specials_list
log_must zfs create $TESTPOOL1/$TESTFS1
log_must zfs set mountpoint=$TESTDIR1 $TESTPOOL1/$TESTFS1

detach_test $TESTDIR/$TESTFILE1.1

log_mustnot eval "zpool iostat -v $TESTPOOL1 | grep \"$TESTFILE1.1\""

destroy_pool $TESTPOOL1

log_note "Verify 'zpool detach' fails with non-mirrors."

for type in "" "raidz" "raidz1" "draid"; do
	create_pool $TESTPOOL1 $type $specials_list
	log_must zfs create $TESTPOOL1/$TESTFS1
	log_must zfs set mountpoint=$TESTDIR1 $TESTPOOL1/$TESTFS1

	log_mustnot zpool detach $TESTDIR/$TESTFILE1.1

	log_must eval "zpool iostat -v $TESTPOOL1 | grep \"$TESTFILE1.1\""

	destroy_pool $TESTPOOL1
done

log_pass
