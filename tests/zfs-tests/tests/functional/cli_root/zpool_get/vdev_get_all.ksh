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

#
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
#   zpool get name <pool> all-vdevs works as expected
#
# STRATEGY:
#
#   1. create various kinds of pools
#   2. get all vdev names
#   3. make sure we get all the names back and they look correct
#

verify_runnable "global"

function cleanup {
	zpool destroy -f $TESTPOOL1
	[[ -e $TESTDIR ]] && rm -rf $TESTDIR/*
}
log_onexit cleanup

log_assert "zpool get all-vdevs works as expected"

# map of vdev spec -> summary form
#
# left side is normal args to zpool create; single number will be replaced
# with that number test file
#
# right side is a summary of the vdev tree, one char per vdev
#   !   root
#   0-9 file number
#   m   mirror
#   r   raidz
#   d   draid
typeset -A specs=(
    ["{0..9}"]="!0123456789"
    ["mirror {0..9}"]="!m0123456789"
    ["mirror 0 1 mirror 2 3 mirror 4 5 mirror 6 7"]="!m01m23m45m67"
    ["raidz1 {0..9}"]="!r0123456789"
    ["raidz1 {0..4} raidz1 {5..9}"]="!r01234r56789"
    ["raidz2 {0..9}"]="!r0123456789"
    ["raidz2 {0..4} raidz2 {5..9}"]="!r01234r56789"
    ["raidz3 {0..9}"]="!r0123456789"
    ["raidz3 {0..4} raidz3 {5..9}"]="!r01234r56789"
    ["draid1 {0..9}"]="!d0123456789"
    ["draid2 {0..9}"]="!d0123456789"
    ["draid3 {0..9}"]="!d0123456789"
)

for spec in "${!specs[@]}" ; do
	log_must truncate -s 100M $TESTDIR/$TESTFILE1.{0..9}
	log_must zpool create -f $TESTPOOL1 \
	    $(echo $spec | sed -E "s#(^| )([0-9])#\1$TESTDIR/$TESTFILE1.\2#g")
	typeset desc=$( zpool get -Ho name name $TESTPOOL1 all-vdevs | awk '
	    /^\//    { t = t substr($1,length($1)) ; next }
	    /^root/  { t = t "!" last ; next }
	    /^[a-z]/ { t = t substr($1,0,1) last ; next }
	    END { print t }
	')
	log_must test "${specs[$spec]}" == "$desc"
	cleanup
done

log_pass
