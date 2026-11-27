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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	An import checks async mirror records, child entries and lineage before
#	it uses them.  A damaged record can make the release thread panic.  A
#	bad deferred block pointer can also cause a panic.  The import must
#	refuse these records.  A later layout can write flags and entries that
#	this software does not know.  Its feature flag protects the pool from
#	this software.  The import must refuse any unknown value.
#
# STRATEGY:
#	For each kind of damage, and first for none:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   For record damage, stop the release, write a file twice, and export
#	   the pool: the records with the held ranges of the first copy stay.
#	   For lineage damage, import the leader alone to make a fork, then
#	   export the pool with both children visible.
#	2. Run "zhack async-mirror damage" to change the selected metadata:
#	   - none: no damage.
#	   - free-overlap: a held range is in its record twice.
#	   - same-object: two records name one space map object.
#	   - not-space-map: the object of a record is not a space map.
#	   - deferred: the block pointer of a deferred block is a hole.
#	   - record-flag: a record has a flag that no software knows.
#	   - child-flag: the per-child entry of the lagging child has such
#	     a flag.
#	   - child-second: the pool has per-child entries of two lagging
#	     children.
#	   - child-gone: the pool has records, and no per-child entry that
#	     names their lagging child.
#	   - lineage-count: the fork has no absent children in its count.
#	   - lineage-zero-guid: the fork has a zero guid in its absent list.
#	   - lineage-flag: the fork has an unknown flag.
#	3. With no damage, verify that the pool imports, that the async
#	   mirror is on and that the file is intact: the visit of zhack
#	   alone does not fail an import.  With damage, verify that the
#	   import fails.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

typeset kind digest

for kind in none free-overlap same-object not-space-map deferred \
    record-flag child-flag child-second child-gone \
    lineage-count lineage-zero-guid lineage-flag; do
	log_note "damage: $kind"
	if [[ $kind == lineage-* ]]; then
		async_mirror_create_pool
		async_mirror_enable
		async_mirror_write_files a 2
		log_must wait_async_mirror_idle
		log_must zpool export $TESTPOOL
		async_mirror_import_leader
		log_must test "$(get_async_mirror_lineage lineage_parent)" != 0
		log_must test "$(get_async_mirror_lineage lineage_absent)" != 0
		log_must zpool export $TESTPOOL
		async_mirror_unhide_lagging
	else
		async_mirror_create_pool
		async_mirror_enable
		async_mirror_suspend_release 1
		async_mirror_write /$TESTPOOL/f 16
		sync_pool $TESTPOOL
		log_must wait_async_mirror_idle
		async_mirror_write /$TESTPOOL/f 16
		sync_pool $TESTPOOL
		log_must wait_async_mirror_idle
		log_must test "$(get_async_mirror_stat held_bytes)" -gt 0
		digest=$(xxh128digest /$TESTPOOL/f)
		log_must zpool export $TESTPOOL
	fi

	log_must zhack -o zfs_vdev_async_mirror_suspend_copy=1 \
	    -o zfs_vdev_async_mirror_suspend_release=1 \
	    -d $ASYNC_MIRROR_DIR async-mirror damage $TESTPOOL $kind

	if [[ $kind == none ]]; then
		async_mirror_import
		log_must test "$(get_async_mirror_stat active)" -eq 1
		log_must test "$digest" == "$(xxh128digest /$TESTPOOL/f)"
		async_mirror_suspend_release 0
		destroy_pool $TESTPOOL
	else
		log_mustnot zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
		log_mustnot poolexists $TESTPOOL
	fi
	log_must rm -f $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_LAGGING
done

log_pass "An import fails on damaged async mirror metadata."
