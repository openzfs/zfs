#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# DESCRIPTION:
#	NetBSD FIOSEEKDATA / FIOSEEKHOLE on ZFS regular files.
#
# STRATEGY:
#	1. Enable hole reporting for dirty files.
#	2. For several recordsizes, write [data][hole][data], a fully
#	   sparse file, and a fully dense file.
#	3. The fioseek helper issues FIOSEEKDATA/FIOSEEKHOLE (not
#	   lseek -- SEEK_HOLE is EINVAL on NetBSD 11 sys_lseek).
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if ! is_netbsd; then
	log_unsupported "FIOSEEKHOLE/FIOSEEKDATA is a NetBSD file ioctl"
fi

if ! command -v fioseek >/dev/null 2>&1; then
	log_unsupported "fioseek helper is not installed"
fi

function cleanup
{
	log_must zfs set recordsize=128k $TESTPOOL/$TESTFS
	log_must rm -f $TESTDIR/test-fioseek
	log_must set_tunable64 DMU_OFFSET_NEXT_SYNC $dmu_offset_next_sync
}

log_assert "FIOSEEKDATA/FIOSEEKHOLE report data and holes on ZFS files"

log_onexit cleanup

typeset dmu_offset_next_sync=$(get_tunable DMU_OFFSET_NEXT_SYNC)
log_must set_tunable64 DMU_OFFSET_NEXT_SYNC 1

for bs in 4096 8192 16384 32768 65536 131072; do
	log_must zfs set recordsize=$bs $TESTPOOL/$TESTFS
	log_must fioseek $TESTDIR/test-fioseek $bs
	log_must rm -f $TESTDIR/test-fioseek
done

log_pass "FIOSEEKDATA/FIOSEEKHOLE report data and holes on ZFS files"
