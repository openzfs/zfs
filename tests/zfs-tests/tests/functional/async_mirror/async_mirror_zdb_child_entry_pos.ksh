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
#	The MOS has one entry for each lagging child, with the guid of the
#	leaf as its key: the per-child entry.  It exists while the leaf is
#	the lagging child, an import finds the lagging child there, and
#	"zdb -m" prints it.  The label mark of the lagging child is the
#	number of its lagging set.  The record of a txg names no leaf.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror.  Verify that "zdb -m" prints
#	   no per-child entry, and that the MOS directory does not name the
#	   object of the entries.
#	2. Turn the async mirror on and write files.  Verify that "zdb -m"
#	   prints one per-child entry, that of the lagging child, with
#	   version 1, lagging set 1, the lagging flag alone, and a start
#	   txg.  Verify that a record line names a txg and a space map, and
#	   no leaf.  Verify that the label mark of the lagging child is 1,
#	   and that the leader has no mark.
#	3. Export the pool.  Verify the same entry on the exported pool,
#	   and that the MOS leak check of "zdb -d" finds no leaked object:
#	   zdb knows the object of the entries.
#	4. Import the pool.  Verify that the async mirror resumes on the
#	   leaf of the entry.
#	5. Turn the async mirror off, and wait until the records are
#	   released.  Verify that the entry and its object are gone.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# Print a field of the per-child entry of a leaf, as "zdb -m" prints it, or
# nothing if the leaf has no entry.
#
# $1 guid of the leaf
# $2 field
# $3... options of zdb
#
function child_field # guid field [zdb options]
{
	typeset guid=$1
	typeset field=$2
	shift 2

	zdb "$@" -m $TESTPOOL | awk -v g="$guid:" -v f=$field '
	    /^Asynchronous Mirror Child / { on = ($4 == g); next }
	    /^[^ \t]/ { on = 0 }
	    on && $1 == f { print $3; exit }'
}

# Print the number of per-child entries.
function child_count # [zdb options]
{
	zdb "$@" -m $TESTPOOL | grep -c '^Asynchronous Mirror Child '
}

# Succeed if the MOS directory names the object of the per-child entries.
function has_child_object # [zdb options]
{
	zdb "$@" -dddd $TESTPOOL 1 | grep -Fq 'org.openzfs:async_mirror_child'
}

#
# Verify the per-child entry of the lagging child.
#
# $1 guid of the leaf
# $2... options of zdb
#
function verify_child_entry # guid [zdb options]
{
	typeset guid=$1
	shift

	log_must has_child_object "$@"
	log_must test "$(child_count "$@")" -eq 1
	log_must test "$(child_field $guid version "$@")" == 1
	log_must test "$(child_field $guid set "$@")" == 1
	log_must test "$(child_field $guid flags "$@")" == 0x1
	log_must test "$(child_field $guid start_txg "$@")" -gt 0
	log_must async_mirror_child_has_flag lagging "$@"
	log_mustnot async_mirror_child_has_flag frozen "$@"
	log_mustnot async_mirror_child_has_flag off "$@"
}

async_mirror_create_pool
sync_pool $TESTPOOL
log_must test "$(child_count)" -eq 0
log_mustnot has_child_object

async_mirror_enable
async_mirror_write_files a 2
typeset digests="$(async_mirror_digests a 2)"
log_must wait_async_mirror_idle
sync_pool $TESTPOOL
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)

verify_child_entry $guid
log_must eval "zdb -m $TESTPOOL | \
    grep -Eq '^[[:space:]]txg [0-9]+ space map [0-9]+ length'"
log_mustnot eval "zdb -m $TESTPOOL | grep -Eq '^[[:space:]]txg [0-9]+ leaf '"
log_must eval "zdb -l $ASYNC_MIRROR_LAGGING | \
    grep -q 'async_mirror_lagging: 1\$'"
#
# Each label holds the vdev tree of the whole mirror, so the leader's label
# shows the mark on the entry of the lagging child too (see
# async_mirror_config_mark_pos), with the same lagging-set number.
#
log_must eval "zdb -l $ASYNC_MIRROR_LEADER | \
    grep -q 'async_mirror_lagging: 1\$'"

log_must zpool export $TESTPOOL
verify_child_entry $guid -e -p $ASYNC_MIRROR_DIR
typeset out
out=$(zdb -e -p $ASYNC_MIRROR_DIR -d $TESTPOOL 2>&1)
typeset -i rc=$?
log_mustnot eval "echo \"\$out\" | grep -q 'MOS object'"
log_must test $rc -eq 0

async_mirror_import
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must test "$(get_async_mirror_prop state)" != off
log_must test "$(get_async_mirror_prop state $ASYNC_MIRROR_LEADER)" == leader
log_must test "$digests" == "$(async_mirror_digests a 2)"

async_mirror_disable
log_must wait_async_mirror_released
sync_pool $TESTPOOL
log_must test "$(child_count)" -eq 0
log_mustnot has_child_object

log_pass "zdb prints the per-child entry of the lagging child."
