#!/bin/ksh -p
# shellcheck disable=SC2066
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
# Copyright 2025 Ivan Shapovalov <intelfx@intelfx.name>
#

. "$STF_SUITE/include/libtest.shlib"

#
# DESCRIPTION:
# 	`zfs clone -u` should leave the new file system unmounted.
#
# STRATEGY:
#	1. Prepare snapshots
#	2. Clone a snapshot using `-u` and make sure the clone is not mounted.
#

verify_runnable "both"

function setup_all
{
	log_note "Creating snapshots..."

	for snap in "$SNAPFS" ; do
		if ! snapexists "$snap" ; then
			log_must zfs snapshot "$snap"
		fi
	done

	return 0
}

function cleanup_all
{
	datasetexists "$fs" && destroy_dataset "$fs"

	for snap in "$SNAPFS" ; do
		snapexists "$snap" && destroy_dataset "$snap" -Rf
	done

	return 0
}

log_onexit cleanup_all
log_must setup_all

log_assert "zfs clone -u should leave the new file system unmounted"

typeset fs="$TESTPOOL/clonefs$$"

log_must zfs clone -u "$SNAPFS" "$fs"
log_mustnot ismounted "$fs"

log_pass "zfs clone -u leaves the new file system unmounted"
