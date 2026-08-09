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
# Copyright (c) 2020 Lawrence Livermore National Security, LLC.
# Copyright (c) 2026 Seagate Technology, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify creation of several failure groups in one big width row.
#
# STRATEGY:
# 1) Test valid stripe/spare/children/width combinations.
# 2) Test invalid stripe/spare/children/width combinations outside the
#    allowed limits.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	rm -f $draid_vdevs
	rmdir $TESTDIR
}

log_assert "'zpool create <pool> draid:#d:#c:#w:#s <vdevs>'"

log_onexit cleanup

mkdir $TESTDIR

# Generate 10 random valid configurations to test.
for (( i = 0; i < 10; i++ )); do
	parity=$(random_int_between 1 3)
	spares=$(random_int_between 0 3)
	data=$(random_int_between 1 10)
	n=$(random_int_between 2 4)

	(( min_children = (data + parity + spares) ))
	(( max_children = 64 / n ))
	children=$(random_int_between $min_children $max_children)
	(( width = (children * n) ))
	(( spares *= n ))

	draid="draid${parity}:${data}d:${children}c:${width}w:${spares}s"

	draid_vdevs=$(echo $TESTDIR/file.{1..$width})
	log_must truncate -s $MINVDEVSIZE $draid_vdevs

	log_must zpool create $TESTPOOL $draid $draid_vdevs
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL

	# create the same pool with fgroup keywords
	draid_fgrp_vdevs=""
	for (( g = 0; g < n; g++ )); do
		draid_fgrp_vdevs+="fgroup "
		for (( c = 0; c < children; c++ )); do
			draid_fgrp_vdevs+="$TESTDIR/file.$((c + (g * children) + 1)) "
		done
	done

	log_must zpool create $TESTPOOL $draid $draid_fgrp_vdevs
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL

	# create the same pool with fdomain keywords
	draid_fdom_vdevs=""
	for (( c = 0; c < children; c++ )); do
		draid_fdom_vdevs+="fdomain "
		for (( g = 0; g < n; g++ )); do
			draid_fdom_vdevs+="$TESTDIR/file.$((c + (g * children) + 1)) "
		done
	done

	log_must zpool create $TESTPOOL $draid $draid_fgrp_vdevs
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL

	rm -f $draid_vdevs
done

children=32
draid_vdevs=$(echo $TESTDIR/file.{1..$children})
draid_vdevs0=$(echo $TESTDIR/file.{1..$((children / 2))})
draid_vdevs1=$(echo $TESTDIR/file.{$((children / 2 + 1))..$children})
draid_vdevs0_less=$(echo $TESTDIR/file.{1..$((children / 2 - 1))})
draid_vdevs1_more=$(echo $TESTDIR/file.{$((children / 2))..$children})
log_must truncate -s $MINVDEVSIZE $draid_vdevs

mkdir $TESTDIR
log_must truncate -s $MINVDEVSIZE $draid_vdevs

# Exceeds maximum data disks (limited by total children)
log_must zpool create $TESTPOOL draid2:14d:32w $draid_vdevs
log_must destroy_pool $TESTPOOL
log_mustnot zpool create $TESTPOOL draid2:14d:33w $draid_vdevs
log_mustnot zpool create $TESTPOOL draid2:14d:31w $draid_vdevs

# One fdomain or fgroup keyword is not enough
log_mustnot zpool create $TESTPOOL draid2:14d:32w fdomain $draid_vdevs
log_mustnot zpool create $TESTPOOL draid2:14d:32w fgroup $draid_vdevs

# The number of devices should be equal after each fdomain or fgroup
log_mustnot zpool create $TESTPOOL draid2:14d:32w fdomain $draid_vdevs0_less fdomain $draid_vdevs1_more
log_mustnot zpool create $TESTPOOL draid2:14d:32w fgroup $draid_vdevs0_less fgroup $draid_vdevs1_more

# Keywords cannot be mixed
log_mustnot zpool create $TESTPOOL draid2:14d:32w fdomain $draid_vdevs0 fgroup $draid_vdevs1

# Failure groups and domains can be inferred from keywords
log_must zpool create $TESTPOOL draid2:14d fgroup $draid_vdevs0 fgroup $draid_vdevs1
log_must poolexists $TESTPOOL
log_must test "$(get_vdev_prop failure_group $TESTPOOL draid2:14d:16c:32w-0)" == "-"
log_must destroy_pool $TESTPOOL
log_must zpool create $TESTPOOL draid1 fdomain $draid_vdevs0 fdomain $draid_vdevs1
log_must poolexists $TESTPOOL
log_must test "$(get_vdev_prop failure_domain $TESTPOOL draid1:1d:2c:32w-0)" == "-"
log_must destroy_pool $TESTPOOL

# Width matches vdevs, but it must be multiple of children
log_mustnot zpool create $TESTPOOL draid2:13d:15c:32w $draid_vdevs

log_pass "'zpool create <pool> draid:#d:#c:#w:#s <vdevs>'"
