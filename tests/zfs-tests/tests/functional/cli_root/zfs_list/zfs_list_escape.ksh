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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs list -H --escape' escapes values as 'zfs diff' escapes paths,
# 'zfs list -H' doesn't, and '--escape' needs '-H' and refuses '-j'.
#
# STRATEGY:
# 1. Create a dataset with a space in its name, and a space, tab,
#    newline, backslash and non-ASCII letter in its mountpoint and in a
#    user property
# 2. Verify 'zfs list -H --escape' escapes them and 'zfs list -H' doesn't
# 3. Verify '--escape' fails without '-H' and with '-j'
#

verify_runnable "global"

PROP="org.openzfs:escape"
NAME_ESC="$TESTPOOL/$TESTFS/esc\\0040fs"
MNT_ESC='/esc\0040mnt/tab\0011here/nl\0012here/back\0134slash/\0305\0274upan'
VAL_ESC='a\0040b\0011c\0015d\0177e\0134f'
DATASET=$(printf '%b' "$NAME_ESC")
MNT=$(printf '%b' "$MNT_ESC")
VAL=$(printf '%b' "$VAL_ESC")
TAB=$(printf '\t')

function cleanup
{
	datasetexists "$DATASET" && log_must zfs destroy "$DATASET"
}

log_assert "'zfs list -H --escape' escapes values, 'zfs list -H' doesn't"
log_onexit cleanup


# 1. Prepare a dataset
log_must zfs create -o canmount=noauto -o mountpoint="$MNT" -o "$PROP=$VAL" \
    "$DATASET"

# 2. Compare escaped and raw output
expected="$NAME_ESC$TAB$MNT_ESC$TAB$VAL_ESC"
actual=$(zfs list -H --escape -o name,mountpoint,"$PROP" "$DATASET")
[[ "$actual" == "$expected" ]] || \
    log_fail "'zfs list -H --escape' printed '$actual', expected '$expected'"

expected="$DATASET$TAB$MNT$TAB$VAL"
actual=$(zfs list -H -o name,mountpoint,"$PROP" "$DATASET")
[[ "$actual" == "$expected" ]] || \
    log_fail "'zfs list -H' printed '$actual', expected '$expected'"

# 3. Check the option combinations
log_mustnot zfs list --escape "$DATASET"
log_mustnot zfs list -j --escape "$DATASET"
log_mustnot zfs list -H -j --escape "$DATASET"

log_pass "'zfs list -H --escape' escapes values, 'zfs list -H' doesn't"
