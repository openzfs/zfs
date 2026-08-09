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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
#	User property could be set via creation time by 'zfs snapshot -o'
#
# STRATEGY:
#	1. Create snapshot and give '-o property=value'
#	2. Verify the snapshot be created and user property have been set.
#

verify_runnable "both"

function cleanup
{
	for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
		typeset fssnap=$fs@snap
		datasetexists $fssnap && destroy_dataset $fssnap -rf
	done
	cleanup_user_prop $TESTPOOL
}

function nonexist_user_prop
{
	typeset user_prop=$1
	typeset dtst=$2

	typeset source=$(get_source $user_prop $dtst)
	typeset value=$(get_prop $user_prop $dtst)
	if [[ $source == '-' && $value == '-' ]]; then
		return 0
	else
		return 1
	fi
}

log_assert "User property could be set upon snapshot via 'zfs snapshot -o'."
log_onexit cleanup

typeset snap_property=

for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
	typeset fssnap=$fs@snap
	prop_name=$(valid_user_property 10)
	value=$(user_property_value 16)

	log_must eval "zfs snapshot -o $prop_name='$value' $fssnap"
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap

	log_must zfs destroy -f $fssnap

	prop_name2=$(valid_user_property 10)
	value2=$(user_property_value 16)

	log_must eval "zfs snapshot -o $prop_name='$value' -o $prop_name2='$value2' $fssnap"
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap
	log_mustnot nonexist_user_prop $prop_name2 $fssnap
done

cleanup

prop_name=$(valid_user_property 10)
value=$(user_property_value 16)

log_must eval "zfs snapshot -r -o $prop_name='$value' $TESTPOOL@snap"
for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
	typeset fssnap=$fs@snap
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap

	log_must zfs destroy -rf $fssnap
done

cleanup

prop_name2=$(valid_user_property 10)
value2=$(user_property_value 16)

log_must eval "zfs snapshot -r -o $prop_name='$value' -o $prop_name2='$value2' $TESTPOOL@snap"
for fs in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL $TESTPOOL ; do
	typeset fssnap=$fs@snap
	log_must snapexists $fssnap
	log_mustnot nonexist_user_prop $prop_name $fssnap
	log_mustnot nonexist_user_prop $prop_name2 $fssnap

	log_must zfs destroy -rf $fssnap
done

log_pass "User property could be set upon snapshot via 'zfs snapshot -o'."
