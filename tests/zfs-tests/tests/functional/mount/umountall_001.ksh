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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# There are myriad problems associated with trying to test umountall in a way
# that works reliable across different systems. Some filesystems won't unmount
# because they're busy. Some won't remount because they were legacy mounts in
# the first place. etc...
# Make a best approximation by calling umountall with the -n option, and verify
# that the list of things it would try to unmout makes sense.
#
# STRATEGY:
# 1. Make a list of file systems umountall is known to ignore.
# 2. Append all ZFS file systems on this system.
# 3. Run umountall -n and verify the file systems it reports are in the list.
#

log_must zfs mount -a
for fs in 1 2 3 ; do
	log_must mounted $TESTPOOL/$TESTFS.$fs
done

# This is the list we check the output of umountall -n against. We seed it
# with these values because umountall will ignore them, and they're possible
# (though most are improbable) ZFS filesystem mountpoints.
zfs_list="/ /lib /sbin /tmp /usr /var /var/adm /var/run"

# Append our ZFS filesystems to the list, not worrying about duplicates.
if is_linux; then
	typeset mounts=$(mount | awk '$5 == "zfs" {print $3}')
elif is_freebsd; then
	typeset mounts=$(mount -p | awk '$3 == "zfs" {print $2}')
else
	typeset mounts=$(mount -p | awk '$4 == "zfs" {print $3}')
fi

for fs in $mounts; do
	zfs_list="$zfs_list $fs"
done

if is_linux; then
	mounts=$(umount --fake -av -t zfs 2>&1 | awk '/successfully umounted/ {print $1}')
	# Fallback to /proc/mounts for umount(8) (util-linux-ng 2.17.2)
	if [[ -z $mounts ]]; then
		mounts=$(awk '/zfs/ { print $2 }' /proc/mounts)
	fi
elif is_freebsd; then
	# Umountall and umount not supported on FreeBSD
	mounts=$(mount -t zfs | sort -r | awk '{print $3}')
else
	mounts=$(umountall -n -F zfs 2>&1 | awk '{print $2}')
fi

fs=''
for fs in $mounts; do
	for i in $zfs_list; do
		[[ $fs = $i ]] && continue 2
	done
	log_fail "umountall -n -F zfs tried to unmount $fs"
done
[[ -n $mounts ]] || log_fail "umountall -n -F zfs produced no output"

log_pass "All ZFS file systems would have been unmounted"
