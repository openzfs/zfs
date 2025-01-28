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
# Copyright (c) 2026 Ivan Shapovalov <intelfx@intelfx.name>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 	`zfs clone -pp` should create the parent of the new filesystem with `canmount=off`.
#
# STRATEGY:
#	1. Prepare snapshots
#	2. Make sure the parent of the clone target does not exist
#	3. Make sure that `zfs clone -pp` works the same as `-p`
#	4. Make sure that the newly created parent has `canmount=off`
#

verify_runnable "both"

function setup
{
	log_note "Create snapshots and mount them..."

	for snap in $SNAPFS $SNAPFS1 ; do
		if ! snapexists "$snap" ; then
			log_must zfs snapshot "$snap"
		fi
	done

	return 0
}

function cleanup
{

	datasetexists "$TESTPOOL/notexist" && destroy_dataset "$TESTPOOL/notexist" -rRf

	for snap in $SNAPFS $SNAPFS1 ; do
		snapexists "$snap" && destroy_dataset "$snap" -Rf
	done

	return 0
}

log_onexit cleanup

log_assert "'zfs clone -pp' should work as expected."

setup

log_mustnot datasetexists "$TESTPOOL/notexist"
log_mustnot datasetexists "$TESTPOOL/notexist/new"
log_mustnot datasetexists "$TESTPOOL/notexist/new2"

log_must verify_opt_p_ops "clone" "fs" "$SNAPFS" \
	 "$TESTPOOL/notexist/new/clonefs$$" "-pp"

log_must dataset_has_prop canmount off "$TESTPOOL/notexist"
log_must dataset_has_prop canmount off "$TESTPOOL/notexist/new"
log_mustnot ismounted "$TESTPOOL/notexist"
log_mustnot ismounted "$TESTPOOL/notexist/new"

if is_global_zone ; then
	log_must verify_opt_p_ops "clone" "vol" "$SNAPFS1" \
		 "$TESTPOOL/notexist/new2/clonevol$$" "-pp"

	log_must dataset_has_prop canmount off "$TESTPOOL/notexist/new2"
	log_mustnot ismounted "$TESTPOOL/notexist/new2"
fi

log_pass "'zfs clone -pp' works as expected."
