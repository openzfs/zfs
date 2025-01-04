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
# receiving a send stream
#
# STRATEGY:
# 1. Create a filesystem with children.
# 2. Snapshot the filesystems.
# 3. Create various send streams (full, incremental, replication) and verify
#    we can both override and exclude native and user properties.
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

log_assert "ZFS receive property override and exclude options work as expected."
log_onexit cleanup

orig=$TESTPOOL/$TESTFS1
origsub=$orig/sub
dest=$TESTPOOL/$TESTFS2
destsub=$dest/sub
typeset userprop=$(valid_user_property 8)
typeset userval=$(user_property_value 8)
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
log_mustnot eval "zfs recv $dest -o atime < $streamfile_full"
log_mustnot eval "zfs recv $dest -x atime=off < $streamfile_full"
log_mustnot eval "zfs recv $dest -o atime=off -x atime < $streamfile_full"
log_mustnot eval "zfs recv $dest -o atime=off -o atime=on < $streamfile_full"
log_mustnot eval "zfs recv $dest -x atime -x atime < $streamfile_full"
log_mustnot eval "zfs recv $dest -o version=1 < $streamfile_full"
log_mustnot eval "zfs recv $dest -x version < $streamfile_full"
log_mustnot eval "zfs recv $dest -x normalization < $streamfile_full"
# Verify we also reject invalid ZVOL options
log_must zfs create -V 32K -s $orig/zvol
log_must eval "zfs send $orig@snap1 > $streamfile_full"
log_mustnot eval "zfs recv $dest -x volsize < $streamfile_full"
log_mustnot eval "zfs recv $dest -o volsize=32K < $streamfile_full"
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
log_must eval "zfs recv -o compression=on -o '$userprop:dest'='$userval' "\
	"$dest < $streamfile_full"
log_must eval "check_prop_source $dest compression on local"
log_must eval "check_prop_source $dest '$userprop:dest' '$userval' local"
# Cleanup
log_must zfs destroy -r -f $orig
log_must zfs destroy -r -f $dest

#
# 3.3 Verify -o property=value and -x work on both native and user properties
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
log_must eval "zfs set '$userprop:orig'='$userval' $orig"
log_must eval "zfs set '$userprop:orig'='$userval' $origsub"
log_must eval "zfs set '$userprop:snap'='$userval' $orig@snap1"
log_must eval "zfs set '$userprop:snap'='$userval' $origsub@snap3"
log_must eval "zfs send -R -I $orig@snap1 $orig@snap3 > $streamfile_incr"
# Sets various combination of override and exclude options
log_must eval "zfs recv -F -o atime=off -o '$userprop:dest2'='$userval' "\
	"-o quota=123456789 -o checksum=sha512 -x compression "\
        "-x '$userprop:orig' -x '$userprop:snap3' $dest < $streamfile_incr"
# Verify we can correctly override and exclude properties
log_must eval "check_prop_source $dest copies 2 received"
log_must eval "check_prop_source $dest atime off local"
log_must eval "check_prop_source $dest '$userprop:dest2' '$userval' local"
log_must eval "check_prop_source $dest quota 123456789 local"
log_must eval "check_prop_source $dest checksum sha512 local"
log_must eval "check_prop_inherit $destsub copies $dest"
log_must eval "check_prop_inherit $destsub atime $dest"
log_must eval "check_prop_inherit $destsub checksum $dest"
log_must eval "check_prop_inherit $destsub '$userprop:dest2' $dest"
log_must eval "check_prop_source $destsub quota 0 default"
log_must eval "check_prop_source $destsub compression on default"
log_must eval "check_prop_missing $dest '$userprop:orig'"
log_must eval "check_prop_missing $destsub '$userprop:orig'"
log_must eval "check_prop_source " \
	"$dest@snap1 '$userprop:snap' '$userval' received"
log_must eval "check_prop_source " \
	"$destsub@snap3 '$userprop:snap' '$userval' received"
log_must eval "check_prop_missing $dest@snap3 '$userprop:snap3'"
log_must eval "check_prop_missing $destsub@snap3 '$userprop:snap3'"
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
log_must eval "zfs set '$userprop:orig'='oldval' $orig"
log_must eval "zfs set '$userprop:orig'='oldsubval' $origsub"
log_must eval "zfs send -R $orig@snap1 > $streamfile_repl"
log_must eval "zfs receive $dest < $streamfile_repl"
log_must eval "check_prop_source $dest copies 2 received"
log_must eval "check_prop_inherit $destsub copies $dest"
log_must eval "check_prop_source $dest '$userprop:orig' 'oldval' received"
log_must eval "check_prop_source $destsub '$userprop:orig' 'oldsubval' received"
# Set new custom properties on both source and destination
log_must eval "zfs set copies=3 $orig"
log_must eval "zfs set '$userprop:orig'='newval' $orig"
log_must eval "zfs set '$userprop:orig'='newsubval' $origsub"
log_must eval "zfs set compression=gzip $dest"
log_must eval "zfs set '$userprop:dest'='localval' $dest"
# Receive the new stream, verify we preserve locally set properties
log_must zfs snapshot -r $orig@snap2
log_must zfs snapshot -r $orig@snap3
log_must eval "zfs send -R -I $orig@snap1 $orig@snap3 > $streamfile_incr"
log_must eval "zfs recv -F -x copies -x compression -x '$userprop:orig' " \
	"-x '$userprop:dest' $dest < $streamfile_incr"
log_must eval "check_prop_source $dest '$userprop:dest' 'localval' local"
log_must eval "check_prop_received $dest '$userprop:orig' 'newval'"
log_must eval "check_prop_received $destsub '$userprop:orig' 'newsubval'"
log_must eval "check_prop_missing $dest '$userprop:orig'"
log_must eval "check_prop_missing $destsub '$userprop:orig'"
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
# 3.5 Verify we can exclude non-inheritable properties from a send stream
#
# Create a dataset tree and replication stream
log_must zfs create $orig
log_must zfs create $origsub
log_must zfs snapshot -r $orig@snap1
log_must eval "zfs set quota=123456789 $orig"
log_must eval "zfs send -R $orig@snap1 > $streamfile_repl"
# Receive the stream excluding non-inheritable properties
log_must eval "zfs recv -F -x quota $dest < $streamfile_repl"
log_must eval "check_prop_source $dest quota 0 default"
log_must eval "check_prop_source $destsub quota 0 default"
# Set some non-inheritable properties on the destination, verify we keep them
log_must eval "zfs set quota=123456789 $dest"
log_must eval "zfs set canmount=off $destsub"
log_must zfs snapshot -r $orig@snap2
log_must zfs snapshot -r $orig@snap3
log_must eval "zfs send -R -I $orig@snap1 $orig@snap3 > $streamfile_incr"
log_must eval "zfs recv -F -x quota -x canmount $dest < $streamfile_incr"
log_must eval "check_prop_source $dest quota 123456789 local"
log_must eval "check_prop_source $destsub quota 0 default"
log_must eval "check_prop_source $destsub canmount off local"
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
log_must eval "zfs set atime=off $dest"
log_must eval "zfs set quota=123456789 $dest"
log_must eval "zfs set '$userprop:orig'='$userval' $dest"
log_must eval "zfs set '$userprop:origsub'='$userval' $destsub"
# Create a truncated incremental replication stream
mntpnt=$(get_prop mountpoint $orig)
log_must eval "dd if=/dev/urandom of=$mntpnt/file bs=1024k count=10"
log_must zfs snapshot -r $orig@snap2
log_must zfs snapshot -r $orig@snap3
log_must eval "zfs send -R -I $orig@snap1 $orig@snap3 > $streamfile_incr"
log_must eval "dd if=$streamfile_incr of=$streamfile_trun bs=1024k count=9"
# Receive the truncated stream, verify original properties are kept
log_mustnot eval "zfs recv -F -o copies=3 -o quota=987654321 "\
	"-o '$userprop:new'='badval' $dest < $streamfile_trun"
log_must eval "check_prop_source $dest copies 1 default"
log_must eval "check_prop_source $destsub copies 1 default"
log_must eval "check_prop_source $dest atime off local"
log_must eval "check_prop_inherit $destsub atime $dest"
log_must eval "check_prop_source $dest quota 123456789 local"
log_must eval "check_prop_source $destsub quota 0 default"
log_must eval "check_prop_source $dest '$userprop:orig' '$userval' local"
log_must eval "check_prop_inherit $destsub '$userprop:orig' $dest"
log_must eval "check_prop_source $destsub '$userprop:origsub' '$userval' local"
log_must eval "check_prop_missing $dest '$userprop:new'"
# Cleanup
log_must zfs destroy -r -f $orig
log_must zfs destroy -r -f $dest

#
# 3.7 Verify we can receive a send stream excluding but not overriding
#     properties invalid for the dataset type, in which case only the
#     appropriate properties are set on the destination.
log_must zfs create -V 128K -s $orig
log_must zfs snapshot $orig@snap1
log_must eval "zfs send $orig@snap1 > $streamfile_full"
log_mustnot eval "zfs receive -o atime=off $dest < $streamfile_full"
log_mustnot eval "zfs receive -o atime=off -x canmount $dest < $streamfile_full"
log_must eval "zfs receive -x atime -x canmount $dest < $streamfile_full"
log_must eval "check_prop_source $dest type volume -"
log_must eval "check_prop_source $dest atime - -"
log_must eval "check_prop_source $dest canmount - -"
log_must_busy zfs destroy -r -f $orig
log_must_busy zfs destroy -r -f $dest
# Recursive sends also accept (and ignore) such overrides
log_must zfs create $orig
log_must zfs create -V 128K -s $origsub
log_must zfs snapshot -r $orig@snap1
log_must eval "zfs send -R $orig@snap1 > $streamfile_repl"
log_must eval "zfs receive -o atime=off $dest < $streamfile_repl"
log_must eval "check_prop_source $dest type filesystem -"
log_must eval "check_prop_source $dest atime off local"
log_must eval "check_prop_source $destsub type volume -"
log_must eval "check_prop_source $destsub atime - -"
# Cleanup
block_device_wait
log_must_busy zfs destroy -r -f $orig
log_must_busy zfs destroy -r -f $dest

#
# 3.8 Verify 'zfs recv -x|-o' works correctly when used in conjunction with -d
#     and -e options.
#
log_must zfs create -p $orig/1/2/3/4
log_must eval "zfs set copies=2 $orig"
log_must eval "zfs set atime=on $orig"
log_must eval "zfs set '$userprop:orig'='oldval' $orig"
log_must zfs snapshot -r $orig@snap1
log_must eval "zfs send -R $orig/1/2@snap1 > $streamfile_repl"
# Verify 'zfs recv -e'
log_must zfs create $dest
log_must eval "zfs receive -e -o copies=3 -x atime "\
	"-o '$userprop:orig'='newval' $dest < $streamfile_repl"
log_must datasetexists $dest/2/3/4
log_must eval "check_prop_source $dest/2 copies 3 local"
log_must eval "check_prop_inherit $dest/2/3/4 copies $dest/2"
log_must eval "check_prop_source $dest/2/3/4 atime on default"
log_must eval "check_prop_source $dest/2 '$userprop:orig' 'newval' local"
log_must eval "check_prop_inherit $dest/2/3/4 '$userprop:orig' $dest/2"
log_must zfs destroy -r -f $dest
# Verify 'zfs recv -d'
log_must zfs create $dest
typeset fs="$(echo $orig | awk -F'/' '{print $NF}')"
log_must eval "zfs receive -d -o copies=3 -x atime "\
	"-o '$userprop:orig'='newval' $dest < $streamfile_repl"
log_must datasetexists $dest/$fs/1/2/3/4
log_must eval "check_prop_source $dest/$fs/1/2 copies 3 local"
log_must eval "check_prop_inherit $dest/$fs/1/2/3/4 copies $dest/$fs/1/2"
log_must eval "check_prop_source $dest/$fs/1/2/3/4 atime on default"
log_must eval "check_prop_source $dest/$fs/1/2 '$userprop:orig' 'newval' local"
log_must eval "check_prop_inherit $dest/$fs/1/2/3/4 '$userprop:orig' $dest/$fs/1/2"
# We don't need to cleanup here

log_pass "ZFS receive property override and exclude options passed."
