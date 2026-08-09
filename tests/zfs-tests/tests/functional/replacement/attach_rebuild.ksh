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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# 	Attaching disks during I/O should pass for supported pools.
#
# STRATEGY:
#	1. Create multidisk pools (stripe/mirror/raidz/draid) and
#	   start some random I/O
#	2. Attach a disk to the pool.
#	3. Verify the integrity of the file system and the resilvering.
#
# NOTE: Raidz does not support the sequential resilver (-s) option.
#

verify_runnable "global"

function cleanup
{
	if [[ -n "$child_pids" ]]; then
		for wait_pid in $child_pids; do
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

options="$options -r "

[[ -n "$options" ]] && options_display=$options

child_pids=""

function attach_test
{
	typeset -i iters=2
	typeset -i index=0
	typeset opt=$1
	typeset disk1=$2
	typeset disk2=$3

	typeset i=0
	while [[ $i -lt $iters ]]; do
		log_note "Invoking file_trunc with: $options_display"
		file_trunc $options $TESTDIR/$TESTFILE.$i &
		typeset pid=$!

		sleep 1

		child_pids="$child_pids $pid"
		((i = i + 1))
	done

	log_must zpool attach -sw $opt $TESTPOOL1 $disk1 $disk2

	for wait_pid in $child_pids; do
		kill $wait_pid
	done
	child_pids=""

	log_must zpool export $TESTPOOL1
	log_must zpool import -d $TESTDIR $TESTPOOL1
	log_must zfs umount $TESTPOOL1/$TESTFS1
	log_must zdb -cdui $TESTPOOL1/$TESTFS1
	log_must zfs mount $TESTPOOL1/$TESTFS1
	verify_pool $TESTPOOL1
}

specials_list=""
i=0
while [[ $i != 3 ]]; do
	truncate -s $MINVDEVSIZE $TESTDIR/$TESTFILE1.$i
	specials_list="$specials_list $TESTDIR/$TESTFILE1.$i"

	((i = i + 1))
done

#
# Create a replacement disk special file.
#
truncate -s $MINVDEVSIZE $TESTDIR/$REPLACEFILE

for op in "" "-f"; do
	create_pool $TESTPOOL1 mirror $specials_list
	log_must zfs create $TESTPOOL1/$TESTFS1
	log_must zfs set mountpoint=$TESTDIR1 $TESTPOOL1/$TESTFS1

	attach_test "$opt" $TESTDIR/$TESTFILE1.1 $TESTDIR/$REPLACEFILE

	log_must eval "zpool iostat -v $TESTPOOL1 | grep \"$REPLACEFILE\""

	destroy_pool $TESTPOOL1
done

log_note "Verify 'zpool attach' fails with non-mirrors."

for type in "" "raidz" "raidz1" "draid" "draid1"; do
	for op in "" "-f"; do
		create_pool $TESTPOOL1 $type $specials_list
		log_must zfs create $TESTPOOL1/$TESTFS1
		log_must zfs set mountpoint=$TESTDIR1 $TESTPOOL1/$TESTFS1

		log_mustnot zpool attach -s "$opt" $TESTDIR/$TESTFILE1.1 \
		    $TESTDIR/$REPLACEFILE

		log_mustnot eval "zpool iostat -v $TESTPOOL1 | grep \"$REPLACEFILE\""

		destroy_pool $TESTPOOL1
	done
done

log_pass
