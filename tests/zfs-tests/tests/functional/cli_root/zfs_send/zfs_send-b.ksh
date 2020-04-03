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
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# 'zfs send -b' should works as expected.
#
# STRATEGY:
# 1. Create a source dataset and set some properties
# 2. Verify command line options interact with '-b' correctly
# 3. Send the dataset and its properties to a new "backup" destination
# 4. Set some properties on the new "backup" dataset
# 5. Restore the "backup" dataset to a new destination
# 6. Verify only original (received) properties are sent from "backup"
#

verify_runnable "both"

function cleanup
{
	for ds in "$SENDFS" "$BACKUP" "$RESTORE"; do
		datasetexists $ds && log_must zfs destroy -r $ds
	done
}

log_assert "'zfs send -b' should work as expected."
log_onexit cleanup

SENDFS="$TESTPOOL/sendfs"
BACKUP="$TESTPOOL/backup"
RESTORE="$TESTPOOL/restore"

# 1. Create a source dataset and set some properties
log_must zfs create $SENDFS
log_must zfs snapshot "$SENDFS@s1"
log_must zfs bookmark "$SENDFS@s1" "$SENDFS#bm"
log_must zfs snapshot "$SENDFS@s2"
log_must zfs set "compression=gzip" $SENDFS
log_must zfs set "org.openzfs:prop=val" $SENDFS
log_must zfs set "org.openzfs:snapprop=val" "$SENDFS@s1"

# 2. Verify command line options interact with '-b' correctly
typeset opts=("" "p" "Rp" "cew" "nv" "D" "DLPRcenpvw")
for opt in ${opts[@]}; do
	log_must eval "zfs send -b$opt $SENDFS@s1 > /dev/null"
	log_must eval "zfs send -b$opt -i $SENDFS@s1 $SENDFS@s2 > /dev/null"
	log_must eval "zfs send -b$opt -I $SENDFS@s1 $SENDFS@s2 > /dev/null"
done
for opt in ${opts[@]}; do
	log_mustnot eval "zfs send -b$opt $SENDFS > /dev/null"
	log_mustnot eval "zfs send -b$opt $SENDFS#bm > /dev/null"
done

# Do 3..6 in a loop to verify various combination of "zfs send" options
typeset opts=("" "p" "R" "pR" "cew")
for opt in ${opts[@]}; do
	# 3. Send the dataset and its properties to a new "backup" destination
	# NOTE: only need to send properties (-p) here
	log_must eval "zfs send -p $SENDFS@s1 | zfs recv $BACKUP"

	# 4. Set some properties on the new "backup" dataset
	# NOTE: override "received" values and set some new properties as well
	log_must zfs set "compression=lz4" $BACKUP
	log_must zfs set "exec=off" $BACKUP
	log_must zfs set "org.openzfs:prop=newval" $BACKUP
	log_must zfs set "org.openzfs:newprop=newval" $BACKUP
	log_must zfs set "org.openzfs:snapprop=newval" "$BACKUP@s1"
	log_must zfs set "org.openzfs:newsnapprop=newval" "$BACKUP@s1"

	# 5. Restore the "backup" dataset to a new destination
	log_must eval "zfs send -b$opt $BACKUP@s1 | zfs recv $RESTORE"

	# 6. Verify only original (received) properties are sent from "backup"
	log_must eval "check_prop_source $RESTORE compression gzip received"
	log_must eval "check_prop_source $RESTORE org.openzfs:prop val received"
	log_must eval "check_prop_source $RESTORE@s1 org.openzfs:snapprop val received"
	log_must eval "check_prop_source $RESTORE exec on default"
	log_must eval "check_prop_missing $RESTORE org.openzfs:newprop"
	log_must eval "check_prop_missing $RESTORE@s1 org.openzfs:newsnapprop"

	# cleanup
	log_must zfs destroy -r $BACKUP
	log_must zfs destroy -r $RESTORE
done

log_pass "'zfs send -b' works as expected."
