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
# Copyright (c) 2026 Ivan Shapovalov <intelfx@intelfx.name>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION
#       `zfs rename -pp` should create the parent of the new filesystem with `canmount=off`.
#
# STRATEGY:
#	1. Make sure the parent of the rename target does not exist
#	2. Make sure that `zfs rename -pp` works the same as `-p`
#	3. Make sure that the newly created parent has `canmount=off`
#

verify_runnable "both"

function cleanup
{
	datasetexists "$TESTPOOL/notexist" && \
		destroy_dataset "$TESTPOOL/notexist" -Rf

	datasetexists "$TESTPOOL/$TESTFS" && \
		destroy_dataset "$TESTPOOL/$TESTFS" -Rf

	log_must zfs create "$TESTPOOL/$TESTFS"

	if is_global_zone ; then
		datasetexists "$TESTPOOL/$TESTVOL" && \
			destroy_dataset "$TESTPOOL/$TESTVOL" -Rf

		log_must zfs create -V "$VOLSIZE" "$TESTPOOL/$TESTVOL"
	fi

	return 0
}

log_onexit cleanup

log_assert "'zfs rename -pp' should work as expected."

log_must_not datasetexists "$TESTPOOL/notexist"
log_must_not datasetexists "$TESTPOOL/notexist/new"
log_must_not datasetexists "$TESTPOOL/notexist/new2"

log_must verify_opt_p_ops "rename" "fs" "$TESTPOOL/$TESTFS" \
	"$TESTPOOL/notexist/new/$TESTFS1" "-pp"

log_must dataset_has_prop canmount off "$TESTPOOL/notexist"
log_must dataset_has_prop canmount off "$TESTPOOL/notexist/new"
log_mustnot ismounted "$TESTPOOL/notexist"
log_mustnot ismounted "$TESTPOOL/notexist/new"

if is_global_zone; then
	log_must verify_opt_p_ops "rename" "vol" "$TESTPOOL/$TESTVOL" \
		"$TESTPOOL/notexist/new2/$TESTVOL1" "-pp"

	log_must dataset_has_prop canmount off "$TESTPOOL/notexist/new2"
	log_mustnot ismounted "$TESTPOOL/notexist/new2"
fi

log_pass "'zfs rename -pp' works as expected."
