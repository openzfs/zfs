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
#    b. Verify that every other features is in the 'enabled' state.
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && log_must $ZPOOL destroy $TESTPOOL
}

function check_features
{
	feature="${1}"
	feature_set=false
	other_feature_set=false

	${ZPOOL} get all ${TESTPOOL} | \
	    grep feature@ | \
	    while read line; do
		set -- $(echo "${line}")

		if [[ "${3}" == "enabled" || "${3}" == "active" ]]; then
			if [[ "feature@${feature}" == "${2}" ]]; then
				feature_set=true
			else
				other_feature_set=true
			fi
		fi
	    done

	if [[ "${feature_set}" == "true" ]]; then
		# This is a success
		if [[ "${other_feature_set}" == "true" ]]; then
			# .. but if _any_ of the other features is enabled,
			# it's a failure!
			return 0
		else
			# All good - feature is enabled, all other disabled.
			return 1
		fi
	else
		# Feature is not set - failure.
		return 1
	fi
}

log_onexit cleanup

for feature in async_destroy bookmarks embedded_data empty_bpobj enabled_txg \
               extensible_dataset filesystem_limits hole_birth large_blocks  \
               lz4_compress spacemap_histogram
do
	log_assert "'zpool create' creates pools with ${feature} disabled"

	log_must $ZPOOL create -f -o "feature@${feature}=disabled" $TESTPOOL $DISKS
	check_features ${feature}
	log_must $ZPOOL destroy -f $TESTPOOL

	log_pass "'zpool create' creates pools with ${feature} disabled"
done
