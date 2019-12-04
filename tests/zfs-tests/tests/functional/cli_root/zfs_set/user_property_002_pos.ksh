#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016, 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
#	User defined property are always inherited from its parent dataset
#	directly.
#
# STRATEGY:
#	1. Create pool, fs, volume, fsclone & volclone.
#	2. Get random user property name and set to the pool
#	3. Verify all dataset user property inherit from pool.
#	4. Set intermediate dataset and verify its children will inherit user
#	   property from it directly.
#

verify_runnable "both"

function cleanup
{
	datasetexists $new_vol && log_must zfs rename $new_vol $vol

	typeset dtst
	for dtst in $new_fsclone $new_volclone $fsclone $volclone \
	    $fssnap $volsnap; do
		destroy_dataset "$dtst" "-f"
	done

	cleanup_user_prop $pool
}

#
# Verify options datasets (3-n) inherit from the inherited dataset $2.
#
# $1 user property
# $2 inherited dataset
# $3-n datasets
#
function inherit_check
{
	typeset prop=$1
	typeset inherited_dtst=$2
	shift 2
	[[ -z $@ ]] && return 1

	typeset inherited_value=$(get_prop $prop $inherited_dtst)
	for dtst in $@; do
		typeset value=$(get_prop $prop $dtst)
		typeset source=$(get_source $prop $dtst)
		if [[ "$value" != "$inherited_value" || \
		      "$source" != "inherited from $inherited_dtst" ]]
		then
			return 1
		fi

		shift
	done

	return 0
}

log_assert "User defined property inherited from its parent."
log_onexit cleanup

pool=$TESTPOOL; fs=$pool/$TESTFS; vol=$pool/$TESTVOL
fssnap=$fs@snap; volsnap=$vol@snap;
log_must zfs snapshot $fssnap
log_must zfs snapshot $volsnap
fsclone=$pool/fsclone; volclone=$pool/volclone
log_must zfs clone $fssnap $fsclone
log_must zfs clone $volsnap $volclone

prop_name=$(valid_user_property 10)
value=$(user_property_value 16)
log_must eval "zfs set $prop_name='$value' $pool"
log_must eval "check_user_prop $pool $prop_name '$value'"
log_must inherit_check $prop_name $pool $fs $vol $fsclone $volclone

new_fsclone=$fs/fsclone; new_volclone=$fs/volclone
log_must zfs rename $fsclone $new_fsclone
log_must zfs rename $volclone $new_volclone
log_must inherit_check $prop_name $pool $fs $new_fsclone $new_volclone

log_note "Set intermediate dataset will change the inherited relationship."
new_value=$(user_property_value 16)
log_must eval "zfs set $prop_name='$new_value' $fs"
log_must eval "check_user_prop $fs $prop_name '$new_value'"
log_must inherit_check $prop_name $fs $new_fsclone $new_volclone

log_pass "User defined property inherited from its parent passed."
