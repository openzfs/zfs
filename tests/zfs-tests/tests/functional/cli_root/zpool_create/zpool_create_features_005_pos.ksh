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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

################################################################################
#
# Specifically disabling a feature, all other features should be enabled.
#
# 1. Loop through all existing features:
#    a. Create a new pool with '-o feature@XXX=disabled'.
#    b. Verify that every other feature is 'enabled' or 'active'.
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && log_must zpool destroy $TESTPOOL
}

function check_features
{
	typeset feature="${1}"

	zpool get all ${TESTPOOL} | grep feature@ | while read line; do
		set -- $(echo "${line}")

		if [[ "feature@${feature}" == "${2}" ]]; then
			# Failure passed feature must be disabled.
			if [[ "${3}" != "disabled" ]]; then
				return 1;
			fi
		else
			# Failure other features must be enabled or active.
			if [[ "${3}" != "enabled" && "${3}" != "active" ]]; then
				return 2;
			fi
		fi
	done

	# All features enabled or active except the expected one.
	return 0
}

log_onexit cleanup

# Several representative features are tested to keep the test time short.
# The features 'extensible_dataset' and 'enabled_txg' are intentionally
# excluded because other features depend on them.
set -A features \
    "hole_birth" \
    "large_blocks"  \
    "large_dnode" \
    "userobj_accounting"

typeset -i i=0
while (( $i < ${#features[*]} )); do
	log_assert "'zpool create' creates pools with ${features[i]} disabled"

	log_must zpool create -f -o "feature@${features[i]}=disabled" \
	    $TESTPOOL $DISKS
	log_must check_features "${features[i]}"
	log_must zpool destroy -f $TESTPOOL
	(( i = i+1 ))
done

log_pass "'zpool create -o feature@feature=disabled' disables features"
