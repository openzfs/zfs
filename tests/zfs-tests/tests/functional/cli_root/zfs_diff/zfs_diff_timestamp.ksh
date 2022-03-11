#!/bin/ksh -p
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
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs diff -t' should display inode change time correctly.
#
# STRATEGY:
# 1. Create a snapshot
# 2. Create some files with a random delay and snapshot the filesystem again
# 3. Verify 'zfs diff -t' correctly display timestamps
#

verify_runnable "both"

function cleanup
{
	for snap in $TESTSNAP1 $TESTSNAP2; do
		snapexists "$snap" && destroy_dataset "$snap"
	done
	find "$MNTPOINT" -type f -delete
	rm -f "$FILEDIFF"
}

#
# Creates $count files in $fspath. Waits a random delay between each file.
#
function create_random # <fspath> <count>
{
	fspath="$1"
	typeset -i count="$2"
	typeset -i i=0

	while (( i < count )); do
		log_must touch "$fspath/file$i"
		sleep $(random_int_between 1 3)
		(( i = i + 1 ))
	done
}

log_assert "'zfs diff -t' should display inode change time correctly."
log_onexit cleanup

DATASET="$TESTPOOL/$TESTFS"
TESTSNAP1="$DATASET@snap1"
TESTSNAP2="$DATASET@snap2"
MNTPOINT="$(get_prop mountpoint $DATASET)"
FILEDIFF="$TESTDIR/zfs-diff.txt"
FILENUM=5

# 1. Create a snapshot
log_must zfs snapshot "$TESTSNAP1"

# 2. Create some files with a random delay and snapshot the filesystem again
create_random "$MNTPOINT" $FILENUM
log_must zfs snapshot "$TESTSNAP2"

# 3. Verify 'zfs diff -t' correctly display timestamps
typeset -i count=0
log_must eval "zfs diff -t $TESTSNAP1 $TESTSNAP2 > $FILEDIFF"
awk '{print substr($1,1,index($1,".")-1) " " $NF}' "$FILEDIFF" | while read -r ctime file
do
	# If path from 'zfs diff' is not a file (could be xattr object) skip it
	if [[ ! -f "$file" ]]; then
		continue;
	fi

	filetime=$(stat_ctime $file)
	if [[ "$filetime" != "$ctime" ]]; then
		log_fail "Unexpected ctime for file $file ($filetime != $ctime)"
	else
		log_note "Correct ctime read on $file: $ctime"
	fi

	(( i = i + 1 ))
done
if [[ $i != $FILENUM ]]; then
	log_fail "Wrong number of files verified ($i != $FILENUM)"
fi

log_pass "'zfs diff -t' displays inode change time correctly."
