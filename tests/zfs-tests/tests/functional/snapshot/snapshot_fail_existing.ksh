#! /bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright (c) 2019 by Datto Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
#	Create a snapshot from regular filesystem, volume,
#	or filesystem upon volume, Build a clone file system
#	from the snapshot.  Then test that snapshot -r handles
#	the case of an already existing snap.
#
# STRATEGY:
# 	1. Create snapshot use 3 combination:
#		- Regular filesystem
#		- Regular volume
#		- Filesystem upon volume
# 	2. Clone a new file system from the snapshot
# 	3. Verify snap -r fails
#

verify_runnable "both"

# Setup array, 4 elements as a group, refer to:
# i+0: name of a snapshot
# i+1: mountpoint of the snapshot
# i+2: clone created from the snapshot
# i+3: mountpoint of the clone

set -A args "$SNAPFS" "$SNAPDIR" "$TESTPOOL/$TESTCLONE" "$TESTDIR.0" \
	"$SNAPFS1" "$SNAPDIR3" "$TESTPOOL/$TESTCLONE1" "" \
	"$SNAPFS2" "$SNAPDIR2" "$TESTPOOL1/$TESTCLONE2" "$TESTDIR.2"

function setup_all
{
	create_pool $TESTPOOL1 ${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
	log_must zfs create $TESTPOOL1/$TESTFS
	log_must zfs set mountpoint=$TESTDIR2 $TESTPOOL1/$TESTFS

	return 0
}

function cleanup_all
{
	typeset -i i=0
	typeset ds
	typeset snap

	for ds in $ctr/$TESTVOL1 $ctr/$TESTCLONE \
		$TESTPOOL1/$TESTCLONE1 $TESTPOOL1/$TESTCLONE2 \
		$TESTPOOL1/$TESTFS;
	do
		destroy_dataset $ds "-rf"
	done

	destroy_pool $TESTPOOL1

	for ds in $TESTPOOL/$TESTCLONE1 $TESTPOOL/$TESTVOL \
		$TESTPOOL/$TESTCLONE $TESTPOOL/$TESTFS;
	do
		destroy_dataset $ds "-rf"
	done

	for snap in $ctr/$TESTFS1@$TESTSNAP1 \
		$snappool $snapvol $snapctr $snapctrvol \
		$snapctrclone $snapctrfs
	do
		snapexists $snap && destroy_dataset $snap "-rf"
	done

	while (( i < ${#args[*]} )); do
		snapexists ${args[i]} && \
			destroy_dataset "${args[i]}" "-Rf"

		[[ -d ${args[i+3]} ]] && \
			log_must rm -rf ${args[i+3]}

		[[ -d ${args[i+1]} ]] && \
			log_must rm -rf ${args[i+1]}

		(( i = i + 4 ))
	done

	[[ -d $TESTDIR2 ]] && \
		log_must rm -rf $TESTDIR2

	return 0
}

log_assert "Verify a cloned file system is writable."

log_onexit cleanup_all

setup_all

[[ -n $TESTDIR ]] && \
    log_must rm -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10
typeset -i i=0

for mtpt in $TESTDIR $TESTDIR2 ; do
	log_note "Populate the $mtpt directory (prior to snapshot)"
	typeset -i j=1
	while [[ $j -le $COUNT ]]; do
		log_must file_write -o create -f $mtpt/before_file$j \
			-b $BLOCKSZ -c $NUM_WRITES -d $j

		(( j = j + 1 ))
	done
done

while (( i < ${#args[*]} )); do
	#
	# Take a snapshot of the test file system.
	#
	log_must zfs snapshot ${args[i]}

	#
	# Clone a new file system from the snapshot
	#
	log_must zfs clone ${args[i]} ${args[i+2]}
	if [[ -n ${args[i+3]} ]] ; then
		log_must zfs set mountpoint=${args[i+3]} ${args[i+2]}

		FILE_COUNT=`ls -Al ${args[i+3]} | grep -v "total" \
		    | grep -v "\.zfs" | wc -l`
		if [[ $FILE_COUNT -ne $COUNT ]]; then
			ls -Al ${args[i+3]}
			log_fail "AFTER: ${args[i+3]} contains $FILE_COUNT files(s)."
		fi

		log_note "Verify the ${args[i+3]} directory is writable"
		j=1
		while [[ $j -le $COUNT ]]; do
			log_must file_write -o create -f ${args[i+3]}/after_file$j \
			-b $BLOCKSZ -c $NUM_WRITES -d $j
			(( j = j + 1 ))
		done

		FILE_COUNT=`ls -Al ${args[i+3]}/after* | grep -v "total" | wc -l`
		if [[ $FILE_COUNT -ne $COUNT ]]; then
			ls -Al ${args[i+3]}
			log_fail "${args[i+3]} contains $FILE_COUNT after* files(s)."
		fi
	fi

	(( i = i + 4 ))
done

# Set up to take the snap -r that we expect to fail.
ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS1
ctrclone=$ctr/$TESTCLONE
ctrvol=$ctr/$TESTVOL1
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapvol=$SNAPFS1
snapctrvol=$ctrvol@$TESTSNAP
snapctrclone=$ctrclone@$TESTSNAP
snapctrfs=$SNAPCTR

log_must zfs snapshot $ctrfs@$TESTSNAP1
log_must zfs clone $ctrfs@$TESTSNAP1 $ctrclone
if is_global_zone; then
	log_must zfs create -V $VOLSIZE $ctrvol
else
	log_must zfs create $ctrvol
fi

log_mustnot zfs snapshot -r $snappool

log_pass "Snapshot -r fails as expected."
