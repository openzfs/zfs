#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
