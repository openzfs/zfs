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
#	A sub-page write racing fadvise(WILLNEED) readahead must not leave
#	the rest of the written page stale.  If readahead gives up on the
#	page while the writer holds its range lock, the page stays cached
#	and not up to date; update_pages() must not then copy in only the
#	bytes written and mark the whole page up to date.
#
# STRATEGY:
#	1. Keep file data out of the ARC (primarycache=metadata) and write
#	   a file of four random records, keeping a copy outside the pool.
#	2. Fill a file outside the pool with 0xA5, so that freed page cache
#	   pages hold a non-zero pattern.
#	3. Fault in the first page of record 2 through a shared mapping.
#	4. Delay reads on the pool's vdev by DELAY_MS.
#	5. Write 512 bytes at offset 1000 of record 1, then fadvise(WILLNEED)
#	   over record 1 and the cached page.
#	6. Clear the delay, then compare the first page of record 1 through
#	   the mapping and through read() with the original content plus the
#	   write.  Fail on any difference.
#

verify_runnable "global"

typeset NAME=mmap_fadvise_readahead_subpage
typeset TESTFILE=$TESTDIR/$NAME.$$
typeset READY=$TESTDIR/$NAME.ready.$$
typeset GO=$TESTDIR/$NAME.go.$$
typeset CLEARED=$TESTDIR/$NAME.cleared.$$
typeset OUT=$TESTDIR/$NAME.out.$$
typeset STALE=$TEST_BASE_DIR/$NAME.stale.$$
typeset ORIG=$TEST_BASE_DIR/$NAME.orig.$$
typeset VDEV=${DISKS%% *}
typeset -i RECSZ=131072
typeset -i DELAY_MS=5000
typeset -i CLEAR_S=$((2 * DELAY_MS / 1000 + 2))
typeset -i TIMEOUT_S=120
typeset helper_pid=""

function cleanup
{
	zinject -c all >/dev/null 2>&1
	touch $CLEARED
	if [[ -n "$helper_pid" ]]; then
		typeset -i i=0
		while kill -0 $helper_pid 2>/dev/null && ((i < 60)); do
			sleep 1
			((i++))
		done
		kill -9 $helper_pid 2>/dev/null
	fi
	log_must zfs inherit primarycache $TESTPOOL/$TESTFS
	log_must zfs inherit recordsize $TESTPOOL/$TESTFS
	rm -f $TESTFILE $READY $GO $CLEARED $OUT $STALE $ORIG
}

log_assert "a sub-page write racing fadvise(WILLNEED) leaves no stale bytes"
log_onexit cleanup

log_must zfs set recordsize=$RECSZ $TESTPOOL/$TESTFS
log_must zfs set primarycache=metadata $TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=$ORIG bs=$RECSZ count=4
log_must dd if=$ORIG of=$TESTFILE bs=$RECSZ
sync_pool $TESTPOOL

dd if=/dev/zero bs=1M count=64 2>/dev/null | tr '\000' '\245' >$STALE ||
    log_fail "cannot create $STALE"
log_note "init_on_alloc: $(grep -o 'init_on_alloc=[^ ]*' /proc/cmdline ||
    echo default)"

$NAME $TESTFILE $RECSZ $READY $GO $CLEARED $STALE $ORIG >$OUT 2>&1 &
helper_pid=$!

typeset -i i=0
while [[ ! -e $READY ]] && kill -0 $helper_pid 2>/dev/null && ((i < 300)); do
	sleep 0.1
	((i++))
done
[[ -e $READY ]] || log_fail "helper did not get ready: $(<$OUT)"

log_must zinject -d $VDEV -D $DELAY_MS:8 -T read $TESTPOOL
log_must touch $GO

typeset -i waited=0
while kill -0 $helper_pid 2>/dev/null && ((waited < TIMEOUT_S)); do
	sleep 1
	((waited++))
	if ((waited == CLEAR_S)); then
		log_must zinject -c all
		log_must touch $CLEARED
	fi
done
if kill -0 $helper_pid 2>/dev/null; then
	log_fail "helper still running after ${TIMEOUT_S}s: $(<$OUT)"
fi

wait $helper_pid
typeset -i rc=$?
helper_pid=""
log_must zinject -c all
log_note "$(<$OUT)"

case $rc in
0)
	log_pass "the written page matches through mmap and read()"
	;;
4)
	log_fail "the written page has stale bytes"
	;;
3)
	log_fail "the read delay did not hold the writer, nothing was tested"
	;;
*)
	log_fail "$NAME failed with status $rc"
	;;
esac
