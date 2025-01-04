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

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# 'zpool split' should only work on mirrors. Every other VDEV layout is not
# supported.
#
# STRATEGY:
# Create pools with various VDEV layouts and verify only mirrors can be split
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	rm -fd $FILEDEV_PREFIX* $altroot
}

#
# Given a vdev type generate a pool configuration which can be immediately
# used as "zpool create $poolname $config" or "zpool add $poolname $config".
# Supported vdev types are:
#  "d" - single disk
#  "t" - stripe
#  "m" - mirror
# "m3" - 3-way mirror
# "z1" - raidz1
# "z2" - raidz2
# "z3" - raidz3
#  "s" - spare
#  "l" - log
# "ll" - mirrored log
#  "c" - cache
#  "sc" - special class
#
function pool_config # <vdev-type>
{
	typeset config=""
	typeset -A disks
	disks[d]="d1"
	disks[t]="t1 t2"
	disks[m]="m1 m2"
	disks[m3]="m1 m2 m3"
	disks[z1]="z1 z2"
	disks[z2]="z1 z2 z3"
	disks[z3]="z1 z2 z3 z4"
	disks[s]="s1"
	disks[l]="l1"
	disks[ll]="l1 l2"
	disks[c]="c1"
	disks[sc]="sc1 sc2"
	case $1 in
	d|t) # single disk or stripe
		vdev='' ;;
	m|m3) # 2-way or 3-way mirror
		vdev='mirror';;
	z1) # raidz1
		vdev='raidz1';;
	z2) # raidz2
		vdev='raidz2';;
	z3) # raidz3
		vdev='raidz3';;
	s) # spare
		vdev='spare';;
	l) # log
		vdev='log';;
	ll) # mirrored log
		vdev='log mirror';;
	c) # cache
		vdev='cache';;
	sc) # mirrored special class
		vdev='special mirror';;
	*)
		log_fail "setup_pool: unsupported vdev type '$1'"
	esac
	config="$vdev"
	for tok in ${disks[$1]}; do
		filedev="$FILEDEV_PREFIX-$tok"
		# if $filedev exists we are requesting the same vdev type twice
		# in a row (eg. pool of striped mirrors): add a random suffix.
		while [[ -f $filedev ]]; do
			filedev="$filedev.$RANDOM"
		done
		truncate -s $SPA_MINDEVSIZE "$filedev"
		config="$config $filedev"
	done
	echo "$config"
}

log_assert "'zpool split' should work only on mirror VDEVs"
log_onexit cleanup

# "good" and "bad" pool layouts
# first token is always used with "zpool create"
# second to last tokens, if any, are used with "zpool add"
typeset -a goodconfs=("m" "m l" "m s" "m c" "m m" "m3" "m3 m3" "m m3 l s c" "m m sc")
typeset -a badconfs=("d" "z1" "z2" "z3" "m d" "m3 d" "m z1" "m z2" "m z3")
typeset FILEDEV_PREFIX="$TEST_BASE_DIR/filedev"
typeset altroot="$TESTDIR/altroot-$TESTPOOL2"

# Create pools with various VDEV layouts and verify only mirrors can be split
for config in "${goodconfs[@]}"
do
	create_config="${config%% *}"
	add_config="$(awk '{$1=""; print $0}' <<< $config)"
	log_must zpool create $TESTPOOL $(pool_config $create_config)
	for vdev in $add_config; do
		log_must zpool add -f $TESTPOOL $(pool_config $vdev)
	done
	log_must zpool split -R $altroot $TESTPOOL $TESTPOOL2
	log_must poolexists $TESTPOOL2
	log_must test "$(get_pool_prop 'altroot' $TESTPOOL2)" == "$altroot"
	cleanup
done

# Every other pool layout should *not* be splittable
for config in "${badconfs[@]}"
do
	create_config="${config%% *}"
	add_config="$(awk '{$1=""; print $0}' <<< $config)"
	log_must zpool create $TESTPOOL $(pool_config $create_config)
	for vdev in $add_config; do
		log_must zpool add -f $TESTPOOL $(pool_config $vdev)
	done
	log_mustnot zpool split -R $altroot $TESTPOOL $TESTPOOL2
	log_mustnot poolexists $TESTPOOL2
	cleanup
done

log_pass "'zpool split' works only on mirror VDEVs"
