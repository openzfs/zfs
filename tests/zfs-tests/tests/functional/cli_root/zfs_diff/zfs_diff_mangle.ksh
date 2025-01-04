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
# http://www.illumos.org/license/CDDL.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs diff' escapes filenames as expected, 'zfs diff -h' doesn't
#
# STRATEGY:
# 1. Prepare a dataset
# 2. Create some files
# 3. verify 'zfs diff' mangles them and 'zfs diff -h' doesn't
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -r "$DATASET"
}

log_assert "'zfs diff' mangles filenames, 'zfs diff -h' doesn't"
log_onexit cleanup

DATASET="$TESTPOOL/$TESTFS/fs"
TESTSNAP1="$DATASET@snap1"

# 1. Prepare a dataset
log_must zfs create "$DATASET"
MNTPOINT="$(get_prop mountpoint "$DATASET")"
log_must zfs snapshot "$TESTSNAP1"

printf '%c\t'"$MNTPOINT/"'%s\n' M '' + 'śmieszny żupan'                       + 'достопримечательности'                                                                                                                                                                                              | sort > "$MNTPOINT/śmieszny żupan"
printf '%c\t'"$MNTPOINT/"'%s\n' M '' + '\0305\0233mieszny\0040\0305\0274upan' + '\0320\0264\0320\0276\0321\0201\0321\0202\0320\0276\0320\0277\0321\0200\0320\0270\0320\0274\0320\0265\0321\0207\0320\0260\0321\0202\0320\0265\0320\0273\0321\0214\0320\0275\0320\0276\0321\0201\0321\0202\0320\0270' | sort > "$MNTPOINT/достопримечательности"
log_must diff -u <(zfs diff -h "$TESTSNAP1" | grep -vF '<xattrdir>' | sort) "$MNTPOINT/śmieszny żupan"
log_must diff -u <(zfs diff    "$TESTSNAP1" | grep -vF '<xattrdir>' | sort) "$MNTPOINT/достопримечательности"

log_pass "'zfs diff' mangles filenames, 'zfs diff -h' doesn't"
