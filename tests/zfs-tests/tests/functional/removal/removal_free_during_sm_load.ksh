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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# Device removal reads each metaslab's space map.  When that read fails
# on one copy of the block, ZFS reads another copy and repairs the bad
# one, and the read completes only after the repair write has completed
# in the write interrupt taskq.  A thread of that taskq may be freeing a
# block in the same metaslab at the time, so the removal must not hold
# the metaslab's ms_lock while it reads the space map, or the pool hangs
# when that taskq has a single thread (as on systems with two CPUs).
#
# STRATEGY:
#	1. Use a single write interrupt thread (zio_taskq_write) and make the
#	   removal wait before it reads each space map
#	   (zfs_removal_sm_load_delay_ms).
#	2. Create a pool whose metadata, and so the space maps, goes to two
#	   special vdevs, fill a dozen metaslabs of the vdev to be removed,
#	   and add a second normal vdev for the removal to copy to.
#	3. Re-import the pool so that no space map is cached, and inject read
#	   errors into one special vdev.  Each space map block has copies on
#	   both special vdevs, and a read that tries the failing copy first is
#	   repaired from the other one.
#	4. Remove the vdev.  Each time the removal waits before reading a
#	   space map, overwrite blocks in that metaslab and sync, so that the
#	   write interrupt taskq frees them while the space map is read.
#	5. Verify that the removal finishes.  Repeat until a space map read
#	   has hit the injected errors, as the copy read first is random.
#

verify_runnable "global"

DISKDIR=$(mktemp -d)
DISK0=$DISKDIR/dsk0
DISK1=$DISKDIR/dsk1
SPEC0=$DISKDIR/spec0
SPEC1=$DISKDIR/spec1
MAP=$DISKDIR/map
SMOBJS=$DISKDIR/smobjs
DELAY_MS=2000
NFILES=24
HUNG=false

function cleanup
{
	restore_tunable REMOVAL_SM_LOAD_DELAY_MS
	[[ -n "$TASKQ_WRITE" ]] && set_taskq_write "$TASKQ_WRITE"
	if $HUNG; then
		# Any command on the hung pool would hang as well.
		log_note "Leaving the hung pool $TESTPOOL in place"
	else
		zinject -c all
		default_cleanup_noexit
	fi
	log_must rm -rf $DISKDIR
}

#
# zio_taskq_write is a string with spaces, which set_tunable can not set.
#
function set_taskq_write
{
	case "$UNAME" in
	Linux)
		printf "%s" "$1" >/sys/module/zfs/parameters/$ZIO_TASKQ_WRITE
		;;
	FreeBSD)
		sysctl vfs.zfs.$ZIO_TASKQ_WRITE="$1"
		;;
	esac
}

#
# Print the state of the latest removal of vdev 0 from the debug log: the
# metaslabs it has waited for, in order, then "done" if it has ended.
#
function removal_progress
{
	kstat dbgmsg | awk '
	    /starting removal thread for vdev 0 / { ms = ""; done = "" }
	    /removal: waiting [0-9]* ms before loading space map of vdev 0 / {
		ms = ms " " $NF
	    }
	    /(finishing|canceled) device removal for vdev 0 / { done = "done" }
	    END { print ms, done }'
}

function removal_starts
{
	kstat dbgmsg | grep -c "starting removal thread for vdev 0 "
}

#
# Print the stacks of the threads involved in the hang, if we can.
#
function log_stacks
{
	typeset pid

	[[ "$UNAME" == "Linux" ]] || return
	for pid in $(pgrep -x z_wr_int) $(pgrep -x spa_vdev_remove) \
	    $(pgrep -x txg_sync); do
		log_note "$(cat /proc/$pid/comm) $pid:" \
		    $(awk '{print $2}' /proc/$pid/stack | head -12)
	done
}

#
# Number of space map reads of vdev 0 that failed on $SPEC0.
#
function sm_read_errors
{
	zpool events -v $TESTPOOL | \
	    awk -v objs="$(echo $(<$SMOBJS))" -v path="$SPEC0" '
	    BEGIN { n = split(objs, a); for (i = 1; i <= n; i++) sm[a[i]] = 1 }
	    /class = "ereport.fs.zfs.io"/ { io = 1 }
	    /zio_objset = / { os = $3 }
	    /zio_object = / { ob = $3 }
	    /vdev_path = / { vp = $3; gsub("\"", "", vp) }
	    /^$/ {
		if (io && os == "0x0" && (ob in sm) && vp == path) hits++
		io = 0; os = ""; ob = ""; vp = ""
	    }
	    END { print hits + 0 }'
}

log_onexit cleanup
log_assert "Device removal does not hang on a free while it reads a space map"

log_must save_tunable REMOVAL_SM_LOAD_DELAY_MS
TASKQ_WRITE=$(get_tunable ZIO_TASKQ_WRITE)
log_must set_taskq_write "sync null fixed,1,1 null"

typeset -i round=0
typeset -i errors=0
while ((errors == 0 && round < 3)); do
	((round++))
	log_must truncate -s $((MINVDEVSIZE * 4)) $DISK0 $DISK1
	log_must truncate -s $MINVDEVSIZE $SPEC0 $SPEC1
	default_setup_noexit "$DISK0 special $SPEC0 $SPEC1"
	log_must zfs set compression=off recordsize=128k $TESTPOOL/$TESTFS
	for i in $(seq $NFILES); do
		log_must dd if=/dev/urandom of=$TESTDIR/file.$i bs=1M \
		    count=16 status=none
	done
	log_must zpool add $TESTPOOL $DISK1

	#
	# Find out which blocks of the files are in which metaslab of vdev 0.
	#
	log_must zpool export $TESTPOOL
	msshift=$(zdb -e -p $DISKDIR -C $TESTPOOL | \
	    awk '/metaslab_shift/ {print $2; exit}')
	zdb -e -p $DISKDIR -ddddd $TESTPOOL/$TESTFS | \
	    awk '/^\tpath\t/ { p = $2 }
		/ L0 0:/ { split($3, a, ":"); print p, $1, a[2] }' | \
	    while read path off dva; do
		echo $path $((16#$off / 131072)) $((16#$dva >> msshift))
	    done > $MAP
	zdb -e -p $DISKDIR -m $TESTPOOL | awk '
	    /^\tvdev/ { v = $2 }
	    /metaslab +[0-9]+ +offset/ && v == 0 && $6 != 0 {
		printf("0x%x\n", $6)
	    }' > $SMOBJS
	log_note "Blocks of vdev 0 per metaslab:" \
	    $(awk '{print $3}' $MAP | sort -n | uniq -c | awk '{print $2":"$1}')

	log_must zpool import -d $DISKDIR $TESTPOOL
	log_must zpool events -c
	log_must zinject -d $SPEC0 -e io -T read -f 100 $TESTPOOL
	log_must set_tunable32 REMOVAL_SM_LOAD_DELAY_MS $DELAY_MS

	#
	# The debug log may hold earlier removals of a vdev 0; wait for this
	# one to start.
	#
	typeset -i starts=$(removal_starts)
	zpool remove $TESTPOOL $DISK0 >$DISKDIR/remove.out 2>&1 &
	typeset remove_pid=$!
	typeset -i i=0
	while (($(removal_starts) == starts)) && ((i < 300)); do
		sleep 0.1
		((i++))
	done
	(($(removal_starts) > starts)) || log_fail "the removal did not start"

	#
	# Each time the removal waits before reading a space map, overwrite
	# blocks in that metaslab.  Without a fix the pool can hang, and
	# then every zpool command waits forever, so run "zpool sync" in the
	# background and give up if the removal makes no progress for a
	# minute.
	#
	typeset -i attempts=0
	typeset -i last=$SECONDS
	while :; do
		typeset finished=false
		set -A progress -- $(removal_progress) ""
		typeset -i n=${#progress[@]}-1
		if ((n > 0)) && [[ ${progress[n - 1]} == "done" ]]; then
			finished=true
			((n--))
		fi
		while ((attempts < n)); do
			typeset ms=${progress[attempts]}
			awk -v ms=$ms '$3 == ms' $MAP | head -2 | \
			    while read path blk m; do
				dd if=/dev/urandom of=$TESTDIR/$path bs=128k \
				    seek=$blk count=1 conv=notrunc status=none
			    done
			zpool sync $TESTPOOL >/dev/null 2>&1 &
			((attempts++))
			last=$SECONDS
		done
		$finished && break
		if ((SECONDS - last > 60)); then
			HUNG=true
			log_stacks
			log_fail "removal hung reading the space map of" \
			    "metaslab ${progress[attempts - 1]} of vdev 0"
		fi
		sleep 0.1
	done

	i=0
	while kill -0 $remove_pid 2>/dev/null && ((i < 600)); do
		sleep 0.1
		((i++))
	done
	if kill -0 $remove_pid 2>/dev/null; then
		HUNG=true
		log_fail "zpool remove hung"
	fi
	wait $remove_pid || log_fail "zpool remove failed"
	log_must zpool wait -t remove $TESTPOOL
	log_must set_tunable32 REMOVAL_SM_LOAD_DELAY_MS 0
	log_mustnot vdevs_in_pool $TESTPOOL $DISK0

	errors=$(sm_read_errors)
	log_note "Round $round: the removal read $attempts space maps," \
	    "$errors reads failed on $SPEC0"
	((attempts >= 10)) || log_fail "only $attempts space maps were read"

	log_must zinject -c all
	log_must zpool destroy $TESTPOOL
done
((errors > 0)) || log_fail "no space map read hit the injected errors"

log_pass "Device removal does not hang on a free while it reads a space map"
