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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Releases before 7c546ef8e5a accepted RAID-Z expansions that the
#	scratch area cannot hold, which then failed a VERIFY in
#	raidz_reflow_scratch_sync() on every read-write import.  A pool
#	holding such an expansion must import read-write with the
#	expansion stopped, and stay usable.
#
# STRATEGY:
#	1. Import the stored pool, whose 56-wide raidz1 vdev at ashift 16
#	   holds an expansion to 57 children that never started.
#	2. Verify that the expansion is stopped: one "reflow too wide"
#	   record in the history, 0.00% done with no estimate, and
#	   'zpool wait -t raidz_expand' still waiting.
#	3. Verify that another attach and a checkpoint are refused.
#	4. Write a file and scrub, then export and import again, and
#	   verify the file, the second record and the expansion state.
#	5. Export, and verify with zdb that nothing leaked.
#
# The pool was created by a program linked with the libzpool of OpenZFS
# 2.4.4: one raidz1 of 56 sparse 64 MiB files at ashift 16, with the
# features of compatibility=openzfs-2.3 and canmount=off on the root
# dataset, which has no file system.  That release accepted the attach of
# a 57th file, and the next txg failed the VERIFY: the pool was never
# exported, and the expansion is recorded with nothing reflowed.  The 57
# files were packed with "tar --sparse".
#
# Unless spa_raidz_expand_thread() stops the expansion, the import does
# not return on Linux (txg_sync stops at the VERIFY) and FreeBSD panics.
# The commands that wait for the pool's first txgs run in the background,
# so that the test fails rather than hangs; a wedged pool is left alone.
#

verify_runnable "global"

typeset -r pool=too_wide
typeset -r record="reflow too wide vdev=0 width=57"
typeset wedged=false

function cleanup
{
	if $wedged; then
		log_note "Leaving $pool as it is: it may be wedged"
		return
	fi
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

#
# Run a command in the background and give up on it after $1 seconds.
#
function bounded # seconds command...
{
	typeset -ri secs=$1
	typeset -i left=$1
	typeset -i pid
	typeset -i rc
	shift

	rm -f $workdir/bounded.rc
	("$@"; echo $? > $workdir/bounded.rc) > $workdir/bounded.out 2>&1 \
	    < /dev/null &
	pid=$!
	while [[ ! -s $workdir/bounded.rc ]] && ((left-- > 0)); do
		sleep 1
	done
	cat $workdir/bounded.out
	if [[ ! -s $workdir/bounded.rc ]]; then
		# A zpool command stuck in the kernel outlives this.
		kill $pid 2>/dev/null
		wedged=true
		log_fail "'$*' did not return in $secs seconds"
	fi
	wait $pid
	rc=$(<$workdir/bounded.rc)
	return $rc
}

#
# Import the pool, and wait until the expansion thread has stopped the
# expansion $1 times in all.
#
function import_stopped # count [import options]
{
	typeset -i count=$1
	shift

	zpool import "$@" -o cachefile=none -d $workdir $pool || return
	until (($(zpool history -i $pool | grep -c "$record") >= count)); do
		sleep 1
	done
}

function check_stopped # count
{
	typeset -i n

	n=$(zpool history -i $pool | grep -c "$record")
	((n == $1)) || log_fail "$n '$record' records, expected $1"
	log_must eval "zpool status $pool > $workdir/status"
	log_must grep -q "expansion of raidz1-0 in progress" $workdir/status
	log_must grep -q "0.00% done, (copy is slow, no estimated time)" \
	    $workdir/status
	log_must check_pool_status $pool "errors" "No known data errors"
}

log_assert "A pool holding a RAID-Z expansion too wide for the scratch" \
    "area imports with the expansion stopped"
workdir=$(mktemp -d $TEST_BASE_DIR/raidz_expand_too_wide.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

typeset -r blockfiles=$STF_SUITE/tests/functional/raidz/blockfiles
log_must eval "bzcat $blockfiles/too_wide_pool.tar.bz2" \
    "| tar -xf - -C $workdir"

# -f: the pool was never exported.
log_must bounded 300 import_stopped 1 -f
check_stopped 1

# The expansion stays in progress, so waiting for it does not end.
timeout 10 zpool wait -t raidz_expand $pool
typeset -i rc=$?
((rc == 124)) || log_fail "zpool wait -t raidz_expand exited $rc, not 124"
log_note "zpool wait -t raidz_expand is still waiting after 10 seconds"

log_must truncate -s 64M $workdir/d57
log_mustnot_expect "raidz expansion in progress" \
    zpool attach -f $pool raidz1-0 $workdir/d57
log_must rm $workdir/d57
log_mustnot_expect "raidz expansion in progress" zpool checkpoint $pool
typeset -i ndevs=$(zpool status -P $pool | grep -c "$workdir/d")
((ndevs == 57)) || log_fail "$pool has $ndevs devices, expected 57"

log_must zfs create $pool/fs
log_must dd if=/dev/urandom of=/$pool/fs/file bs=1024k count=128
typeset sum=$(xxh128digest /$pool/fs/file)
log_must zpool scrub -w $pool
log_must check_pool_status $pool "scan" "repaired 0B"
log_must check_pool_status $pool "scan" "with 0 errors"
check_stopped 1

log_must zpool export $pool
log_must bounded 300 import_stopped 2
check_stopped 2
[[ $(xxh128digest /$pool/fs/file) == "$sum" ]] ||
    log_fail "/$pool/fs/file changed across export and import"

log_must zpool export $pool
log_must zdb -e -p $workdir -bcc $pool

log_pass "A pool holding a RAID-Z expansion too wide for the scratch" \
    "area imports with the expansion stopped"
