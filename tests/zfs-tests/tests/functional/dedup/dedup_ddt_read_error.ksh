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
# DESCRIPTION:
#	A DDT entry that cannot be read must neither panic the system nor
#	free a block that is still in use. Freeing such a block leaks it,
#	writing the same data again does not deduplicate it, and cloning it
#	references it in the BRT.
#
# STRATEGY:
#	For a legacy and a fast dedup table:
#	1. Write a file and a copy of it, so that every entry is in the DDT
#	   ZAP of the duplicate class.
#	2. Export and import the pool, so that the ZAP leaf is not cached,
#	   and inject checksum errors on reads of that leaf.
#	3. Remove the copy, write the data again (the new blocks must not
#	   have the dedup bit) and clone the file (the clone must keep it).
#	4. Clear the injection, export and import, and check the data of
#	   every file, a scrub, and that zdb finds no block claimed twice.
#	   Space may be leaked. Remove every file and check zdb again.
#	5. On a new pool, repeat 1 and 2, clone the file only, clear the
#	   injection, and remove the three files at once: the DDT then
#	   holds one reference less than is freed in that txg.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "an unreadable DDT entry does not panic or free a block in use"

typeset seed=$TEST_BASE_DIR/dedup_ddt_read_error.seed

function cleanup
{
	zinject -c all >/dev/null 2>&1
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	rm -f $seed
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
	if tunable_exists BCLONE_ENABLED ; then
		log_must restore_tunable BCLONE_ENABLED
	fi
}

log_onexit cleanup

# Flush the DDT log every txg, so the entries land in the ZAP.
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000
if tunable_exists BCLONE_ENABLED ; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi

# Object number of the DDT ZAP of the duplicate class.
function ddt_dup_object
{
	typeset dir=1

	if [[ $(get_pool_prop feature@fast_dedup $TESTPOOL) == "active" ]]
	then
		dir=$(zdb -dddd $TESTPOOL 1 | awk '/DDT-sha256 / {print $NF}')
	fi
	zdb -dddd $TESTPOOL $dir | \
	    awk '/DDT-sha256-zap-duplicate/ {print $NF}'
}

# Number of reads the injection handler has failed.
function inject_count
{
	zinject | awk -v pool=$TESTPOOL '$2 == pool {print $NF}'
}

# Number of L0 block pointers of a file that have the dedup bit.
function dedup_l0_count
{
	zdb -ddddddbbbbbb $TESTPOOL/ "$(get_objnum $1)" | grep ' L0 ' | \
	    grep -c ' dedup '
}

# zdb -b may report space leaked by the frees that were refused, but must
# never find a block claimed twice. It exits 2 on leaks, 3 on errors.
function check_zdb
{
	typeset out
	typeset -i rc

	out=$(zdb -b $TESTPOOL 2>&1)
	rc=$?
	echo "$out"
	(( rc == 0 || rc == 2 )) || log_fail "zdb -b exited $rc"
	if echo "$out" | grep -Eq 'block claim error|DOUBLE ALLOC|DOUBLE FREE'
	then
		log_fail "zdb -b found a block claimed twice"
	fi
}

# Create the pool with f1 and a copy f2, and make reads of the leaf of the
# DDT ZAP that holds their entries fail.
function setup_pool # <fast_dedup feature state>
{
	typeset obj

	log_note "feature@fast_dedup=$1"
	log_must zpool create -f -o feature@fast_dedup=$1 -O dedup=on \
	    -O compression=off -O recordsize=128k -O xattr=sa \
	    $TESTPOOL $DISKS

	# dd rather than cp, so that the copy is written and not cloned
	log_must dd if=$seed of=$mnt/f1 bs=128k
	sync_pool $TESTPOOL
	log_must dd if=$seed of=$mnt/f2 bs=128k
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	log_must eval "zdb -D $TESTPOOL | \
	    grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"
	log_mustnot eval "zdb -D $TESTPOOL | grep -q 'DDT-log-.*entries=[1-9]'"

	obj=$(ddt_dup_object)
	[[ -n "$obj" ]] || log_fail "no DDT ZAP for the duplicate class"
	# block 0 is the ZAP header, block 1 its first leaf
	log_must test "$(zdb -ddddd $TESTPOOL $obj | grep -c ' L0 ')" -ge 2

	# The leaf was cached when it was written, and zinject -a cannot
	# evict it from the ARC while a dbuf holds it. Export drops the dbufs
	# and the import gets a new ARC identity; it only reads the header.
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	log_must zinject -b "$(printf '0:%x:0:1' $obj)" -e checksum $TESTPOOL
}

function check_leaf_read
{
	typeset count

	count=$(inject_count)

	log_note "reads of the DDT leaf failed: $count"
	[[ -n "$count" ]] && (( count > 0 )) || \
	    log_fail "the DDT leaf was not read"
}

function clear_and_reimport
{
	log_must zinject -c all
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
}

function test_free_write_clone # <fast_dedup feature state>
{
	typeset f

	setup_pool $1

	# Unfixed, a debug build panics in ddt_lookup() and a release build
	# in ddt_sync(), after freeing a block that f1 still uses.
	log_must rm $mnt/f2
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	check_leaf_read

	log_must dd if=$seed of=$mnt/f3 bs=128k
	sync_pool $TESTPOOL

	log_must clonefile -f $mnt/f1 $mnt/f4
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL

	clear_and_reimport
	for f in f1 f3 f4; do
		log_must cmp $seed $mnt/$f
	done
	log_must test "$(dedup_l0_count $mnt/f1)" -eq 4
	log_must test "$(dedup_l0_count $mnt/f3)" -eq 0
	log_must test "$(dedup_l0_count $mnt/f4)" -eq 4
	log_must eval "zdb -D $TESTPOOL | \
	    grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"

	log_must zpool scrub -w $TESTPOOL
	log_must eval "zpool status $TESTPOOL | grep -q 'with 0 errors'"
	check_zdb

	log_must rm $mnt/f1 $mnt/f3 $mnt/f4
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	check_zdb

	destroy_pool $TESTPOOL
}

function test_clone_then_free_all # <fast_dedup feature state>
{
	typeset f

	setup_pool $1

	# Unfixed, this panics in ddt_lookup() or ddt_addref(). Fixed, the
	# clone is referenced in the BRT, and the DDT keeps two references.
	log_must clonefile -f $mnt/f1 $mnt/f4
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	check_leaf_read

	clear_and_reimport
	for f in f1 f2 f4; do
		log_must cmp $seed $mnt/$f
	done
	log_must test "$(dedup_l0_count $mnt/f4)" -eq 4
	check_zdb

	# Three frees in one txg against two DDT references: the third must
	# not underflow the refcount (an assertion on debug builds).
	log_must rm $mnt/f1 $mnt/f2 $mnt/f4
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	check_zdb

	destroy_pool $TESTPOOL
}

typeset mnt=/$TESTPOOL

log_must dd if=/dev/urandom of=$seed bs=128k count=4

for fast_dedup in disabled enabled; do
	test_free_write_clone $fast_dedup
	test_clone_then_free_all $fast_dedup
done

log_pass "an unreadable DDT entry does not panic or free a block in use"
