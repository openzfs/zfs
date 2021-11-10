#! /bin/ksh -p
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
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
#	Create a snapshot from regular filesystem, volume,
#	or filesystem upon volume, Build a clone file system
#	from the snapshot and verify new files can be written.
#
# STRATEGY:
# 	1. Create snapshot use 3 combination:
#		- Regular filesystem
#		- Regular volume
#		- Filesystem upon volume
# 	2. Clone a new file system from the snapshot
# 	3. Verify the cloned file system is writable
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
	if is_freebsd; then
		# Pool creation on zvols is forbidden by default.
		# Save and the current setting.
		typeset _saved=$(get_tunable VOL_RECURSIVE)
		log_must set_tunable64 VOL_RECURSIVE 1
	fi
	create_pool $TESTPOOL1 ${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
	if is_freebsd; then
		# Restore the previous setting.
		log_must set_tunable64 VOL_RECURSIVE $_saved
	fi
	log_must zfs create $TESTPOOL1/$TESTFS
	log_must zfs set mountpoint=$TESTDIR2 $TESTPOOL1/$TESTFS

	return 0
}

function cleanup_all
{
	typeset -i i=0

	i=0
	while (( i < ${#args[*]} )); do
		snapexists ${args[i]} && \
			destroy_dataset "${args[i]}" "-Rf"

		[[ -d ${args[i+3]} ]] && \
			log_must rm -rf ${args[i+3]}

		[[ -d ${args[i+1]} ]] && \
			log_must rm -rf ${args[i+1]}

		(( i = i + 4 ))
	done

	datasetexists $TESTPOOL1/$TESTFS && \
		destroy_dataset $TESTPOOL1/$TESTFS -f

	destroy_pool $TESTPOOL1

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

log_pass "The clone file system is writable."
