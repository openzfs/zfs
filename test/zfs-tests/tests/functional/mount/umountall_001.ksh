#!/usr/bin/ksh -p

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
# Copyright (c) 2013 by Delphix. All rights reserved.
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

log_must $ZFS mount -a
for fs in 1 2 3 ; do
	log_must mounted $TESTPOOL/$TESTFS.$fs
done

# This is the list we check the output of umountall -n against. We seed it
# with these values because umountall will ignore them, and they're possible
# (though most are improbable) ZFS filesystem mountpoints.
zfs_list="/ /lib /sbin /tmp /usr /var /var/adm /var/run"

# Append our ZFS filesystems to the list, not worrying about duplicates.
for fs in $($MOUNT -p | $AWK '{if ($4 == "zfs") print $3}'); do
	zfs_list="$zfs_list $fs"
done

fs=''
for fs in $($UMOUNTALL -n -F zfs 2>&1 | $AWK '{print $2}'); do
	for i in $zfs_list; do
		[[ $fs = $i ]] && continue 2
	done
	log_fail "umountall -n -F zfs tried to unmount $fs"
done
[[ -n $fs ]] || log_fail "umountall -n -F zfs produced no output"

log_pass "All ZFS file systems would have been unmounted"
