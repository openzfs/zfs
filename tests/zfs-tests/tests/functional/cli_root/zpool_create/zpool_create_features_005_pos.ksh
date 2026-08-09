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
			if [[ "feature@dynamic_gang_header" == "${2}" ]]; then
				continue
			fi
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
    "large_dnode" \
    "longname"  \
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
