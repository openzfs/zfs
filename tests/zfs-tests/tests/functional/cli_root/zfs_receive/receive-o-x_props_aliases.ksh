#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Verify ZFS property override (-o) and exclude (-x) options work when
# receiving a send stream, using property name aliases
#
# STRATEGY:
# 1. Create a filesystem with children.
# 2. Snapshot the filesystems.
# 3. Create various send streams (full, incremental, replication) and verify
#    we can both override and exclude aliased properties.
#

verify_runnable "both"

function cleanup
{
	log_must rm -f $streamfile_full
	log_must rm -f $streamfile_incr
	log_must rm -f $streamfile_repl
	log_must rm -f $streamfile_trun
	destroy_dataset "$orig" "-rf"
	destroy_dataset "$dest" "-rf"
}

log_assert "ZFS receive property alias override and exclude options work as expected."
log_onexit cleanup

orig=$TESTPOOL/$TESTFS1
origsub=$orig/sub
dest=$TESTPOOL/$TESTFS2
destsub=$dest/sub
typeset streamfile_full=$TESTDIR/streamfile_full.$$
typeset streamfile_incr=$TESTDIR/streamfile_incr.$$
typeset streamfile_repl=$TESTDIR/streamfile_repl.$$
typeset streamfile_trun=$TESTDIR/streamfile_trun.$$

#
# 3.1 Verify we can't specify the same property in multiple -o or -x options
#     or an invalid value was specified.
#
# Create a full send stream
log_must zfs create $orig
log_must zfs snapshot $orig@snap1
log_must eval "zfs send $orig@snap1 > $streamfile_full"
# Verify we reject invalid options
log_mustnot eval "zfs recv $dest -o compress < $streamfile_full"
log_mustnot eval "zfs recv $dest -x compress=off < $streamfile_full"
log_mustnot eval "zfs recv $dest -o compress=off -x compress < $streamfile_full"
log_mustnot eval "zfs recv $dest -o compress=off -o compress=on < $streamfile_full"
log_mustnot eval "zfs recv $dest -x compress -x compress < $streamfile_full"
log_mustnot eval "zfs recv $dest -o version=1 < $streamfile_full"
log_mustnot eval "zfs recv $dest -x version < $streamfile_full"
log_mustnot eval "zfs recv $dest -x normalization < $streamfile_full"
# Verify we also reject invalid ZVOL options
log_must zfs create -V 32K -s $orig/zvol
log_must eval "zfs send $orig@snap1 > $streamfile_full"
log_mustnot eval "zfs recv $dest -x volblock < $streamfile_full"
log_mustnot eval "zfs recv $dest -o volblock=32K < $streamfile_full"
# Cleanup
block_device_wait
log_must_busy zfs destroy -r -f $orig

#
# 3.2 Verify -o property=value works on streams without properties.
#
# Create a full send stream
log_must zfs create $orig
log_must zfs snapshot $orig@snap1
log_must eval "zfs send $orig@snap1 > $streamfile_full"
# Receive the full stream, override some properties
log_must eval "zfs recv -o compress=on -o '$userprop:dest'='$userval' "\
	"$dest < $streamfile_full"
log_must eval "check_prop_source $dest compression on local"
log_must eval "check_prop_source $dest '$userprop:dest' '$userval' local"
# Cleanup
log_must zfs destroy -r -f $orig
log_must zfs destroy -r -f $dest

#
# 3.3 Verify -o property=value and -x work
#     for an incremental replication send stream.
#
# Create a dataset tree and receive it
log_must zfs create $orig
log_must zfs create $origsub
log_must zfs snapshot -r $orig@snap1
log_must eval "zfs send -R $orig@snap1 > $streamfile_repl"
log_must eval "zfs recv $dest < $streamfile_repl"
# Fill the datasets with properties and create an incremental replication stream
log_must zfs snapshot -r $orig@snap2
log_must zfs snapshot -r $orig@snap3
log_must eval "zfs set copies=2 $orig"
log_must eval "zfs set dnsize=4k $orig"
log_must eval "zfs set compression=gzip $origsub"
log_must eval "zfs send -R -I $orig@snap1 $orig@snap3 > $streamfile_incr"
# Sets various combination of override and exclude options
log_must eval "zfs recv -F -o atime=off -o quota=123456789 -o checksum=sha512" \
	" -o dnsize=2k -x compress $dest < $streamfile_incr"
# Verify we can correctly override and exclude properties
log_must eval "check_prop_source $dest copies 2 received"
log_must eval "check_prop_source $dest atime off local"
log_must eval "check_prop_source $dest quota 123456789 local"
log_must eval "check_prop_source $dest checksum sha512 local"
log_must eval "check_prop_source $dest dnodesize 2k local"
log_must eval "check_prop_inherit $destsub copies $dest"
log_must eval "check_prop_inherit $destsub atime $dest"
log_must eval "check_prop_inherit $destsub checksum $dest"
log_must eval "check_prop_source $destsub quota 0 default"
log_must eval "check_prop_source $destsub compression on default"
# Cleanup
log_must zfs destroy -r -f $orig
log_must zfs destroy -r -f $dest

#
# 3.4 Verify '-x property' does not remove existing local properties and a
#     modified sent property is received and updated to the new value but can
#     still be excluded.
#
# Create a dataset tree
log_must zfs create $orig
log_must zfs create $origsub
log_must zfs snapshot -r $orig@snap1
log_must eval "zfs set copies=2 $orig"
log_must eval "zfs send -R $orig@snap1 > $streamfile_repl"
log_must eval "zfs receive $dest < $streamfile_repl"
log_must eval "check_prop_source $dest copies 2 received"
log_must eval "check_prop_inherit $destsub copies $dest"
# Set new custom properties on both source and destination
log_must eval "zfs set copies=3 $orig"
log_must eval "zfs set compression=on $orig"
log_must eval "zfs set compression=lzjb $origsub"
log_must eval "zfs set compression=gzip $dest"
# Receive the new stream, verify we preserve locally set properties
log_must zfs snapshot -r $orig@snap2
log_must zfs snapshot -r $orig@snap3
log_must eval "zfs send -R -I $orig@snap1 $orig@snap3 > $streamfile_incr"
log_must eval "zfs recv -F -x copies -x compress $dest < $streamfile_incr"
log_must eval "check_prop_source $dest copies 1 default"
log_must eval "check_prop_received $dest copies 3"
log_must eval "check_prop_source $destsub copies 1 default"
log_must eval "check_prop_received $destsub copies '-'"
log_must eval "check_prop_source $dest compression gzip local"
log_must eval "check_prop_inherit $destsub compression $dest"
# Cleanup
log_must zfs destroy -r -f $orig
log_must zfs destroy -r -f $dest

#
# 3.6 Verify we correctly restore existing properties on a failed receive
#
# Receive a "clean" dataset tree
log_must zfs create $orig
log_must zfs create $origsub
log_must zfs snapshot -r $orig@snap1
log_must eval "zfs send -R $orig@snap1 > $streamfile_repl"
log_must eval "zfs receive $dest < $streamfile_repl"
# Set custom properties on the destination
log_must eval "zfs set compress=on $dest"
log_must eval "zfs set compress=lzjb $destsub"
# Create a truncated incremental replication stream
mntpnt=$(get_prop mountpoint $orig)
log_must eval "dd if=/dev/urandom of=$mntpnt/file bs=1024k count=10"
log_must zfs snapshot -r $orig@snap2
log_must zfs snapshot -r $orig@snap3
log_must eval "zfs send -R -I $orig@snap1 $orig@snap3 > $streamfile_incr"
log_must eval "dd if=$streamfile_incr of=$streamfile_trun bs=1024k count=9"
# Receive the truncated stream, verify original properties are kept
log_mustnot eval "zfs recv -F -o copies=3 -o compress=gzip "\
	"$dest < $streamfile_trun"
log_must eval "check_prop_source $dest copies 1 default"
log_must eval "check_prop_source $destsub copies 1 default"
log_must eval "check_prop_source $dest compression on local"
log_must eval "check_prop_source $destsub compression lzjb local"
# Cleanup
log_must zfs destroy -r -f $orig
log_must zfs destroy -r -f $dest

#
# 3.7 Verify that we can't get around checking a property is readonly
#     by using the alias or receiving a parent replication stream.
log_must zfs create $orig
log_must zfs create -V 128K -s $origsub
log_must zfs snapshot -r $orig@snap1
log_must eval "zfs send -R $orig@snap1 > $streamfile_repl"
log_mustnot eval "zfs receive -o volblock=64k $dest < $streamfile_repl"
# Cleanup
block_device_wait
log_must_busy zfs destroy -r -f $orig

log_pass "ZFS receive property alias override and exclude options passed."
