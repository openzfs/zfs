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
# Copyright (c) 2026, Klara, Inc.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	Verify the 'destroy:snapshot' sub-permission allows destroying
#	snapshots and bookmarks, without granting the ability to destroy
#	the dataset (or volume) itself.
#
# STRATEGY:
#	1. Verify a user with no permissions can destroy neither a
#	   snapshot nor a bookmark.
#	2. Verify the full 'destroy' permission still allows destroying
#	   snapshots and bookmarks (back-compat).
#	3. Verify 'destroy:snapshot' allows destroying snapshots and
#	   bookmarks, but not the dataset/volume itself, even combined
#	   with 'mount'.
#

verify_runnable "both"

log_assert "Verify destroy:snapshot limits destroy to snapshots and " \
	"bookmarks."
log_onexit restore_root_datasets

for dtst in $DATASETS; do
	# With no permissions at all, nothing can be destroyed.
	log_must restore_root_datasets
	log_must verify_noperm $dtst "destroy:snapshot" $STAFF1
	log_must verify_noperm $dtst "destroy_bookmark" $STAFF1

	# Full 'destroy' permission also allows destroying snapshots and
	# bookmarks.
	log_must restore_root_datasets
	log_must zfs allow $STAFF1 "destroy" $dtst
	log_must verify_perm $dtst "destroy:snapshot" $STAFF1
	log_must verify_perm $dtst "destroy_bookmark" $STAFF1

	# 'destroy:snapshot' allows destroying snapshots and bookmarks...
	log_must restore_root_datasets
	log_must zfs allow $STAFF1 "destroy:snapshot" $dtst
	log_must verify_perm $dtst "destroy:snapshot" $STAFF1
	log_must verify_perm $dtst "destroy_bookmark" $STAFF1

	# ...but not the dataset (or volume) itself, even with 'mount'.
	log_must verify_noperm $dtst "destroy" $STAFF1
	log_must zfs allow $STAFF1 "mount" $dtst
	log_must verify_noperm $dtst "destroy" $STAFF1
done

log_must restore_root_datasets

log_pass "Verify destroy:snapshot limits destroy to snapshots and " \
	"bookmarks passed."
