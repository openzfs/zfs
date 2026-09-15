#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License (CDDL), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the CDDL should have accompanied this source. A copy is also
# available at https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	zfs set primarycache=all $TESTPOOL/$TESTFS
	rm -f "$TESTDIR/zstd-dctx-cache"
}

log_assert "Concurrent zstd reads reuse initialized decompression contexts"
log_onexit cleanup

log_must zfs set compression=zstd-3 $TESTPOOL/$TESTFS
log_must zfs set primarycache=metadata $TESTPOOL/$TESTFS
log_must file_write -o create -f "$TESTDIR/zstd-dctx-cache" -b 128K \
	-c 4096 -d 13
log_must sync

typeset create_before=$(kstat zstd.decompress_context_create)

log_must zinject -a
typeset -a pids
for i in $(seq 1 32); do
	dd if="$TESTDIR/zstd-dctx-cache" of=/dev/null bs=128K &
	pids+=($!)
done
for pid in ${pids[*]}; do
	log_must wait $pid
done

typeset create_after=$(kstat zstd.decompress_context_create)
typeset reuse_after=$(kstat zstd.decompress_context_reuse)
(( create_after > create_before )) || \
	log_fail "concurrent reads did not create a decompression context"

# A failed read must release the cached context before it can be reused.
typeset reuse_before_failure=$reuse_after
log_must zinject -a -t data -e decompress -f 100 \
	"$TESTDIR/zstd-dctx-cache"
log_mustnot dd if="$TESTDIR/zstd-dctx-cache" of=/dev/null bs=128K
log_must zinject -c all
log_must dd if="$TESTDIR/zstd-dctx-cache" of=/dev/null bs=128K
typeset reuse_after_failure=$(kstat zstd.decompress_context_reuse)
(( reuse_after_failure > reuse_before_failure )) || \
	log_fail "failed read did not leave a reusable decompression context"

log_must zinject -a
log_must dd if="$TESTDIR/zstd-dctx-cache" of=/dev/null bs=128K
typeset reuse_final=$(kstat zstd.decompress_context_reuse)

(( reuse_final > reuse_after )) || \
	log_fail "repeated reads did not reuse a decompression context"

log_pass "Concurrent zstd reads reused initialized decompression contexts"
