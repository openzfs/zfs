#!/bin/ksh

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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Exercise gang block IO paths for non-encrypted and encrypted datasets.
#

verify_runnable "both"
log_assert "Verify IO when file system is full and ganging."

function cleanup
{
        log_must set_tunable64 METASLAB_FORCE_GANGING $metaslab_force_ganging
	default_cleanup_noexit
}

log_onexit cleanup

default_setup_noexit $DISKS

typeset metaslab_force_ganging=$(get_tunable METASLAB_FORCE_GANGING)
shift=$(random_int_between 15 17)
log_must set_tunable64 METASLAB_FORCE_GANGING $((2**$shift))

keyfile=/$TESTPOOL/keyencfods
log_must eval "echo 'password' > $keyfile"
bs=1024k
count=512

log_must dd if=/dev/urandom of=$TESTDIR/data bs=$bs count=$count
data_checksum=$(xxh128digest $TESTDIR/data)

# Test common large block configuration.
log_must zfs create -o recordsize=1m -o primarycache=metadata $TESTPOOL/gang
mntpnt=$(get_prop mountpoint $TESTPOOL/gang)

log_must dd if=$TESTDIR/data of=$mntpnt/file bs=$bs count=$count
sync_pool $TESTPOOL
log_must dd if=$mntpnt/file of=$TESTDIR/out bs=$bs count=$count
out_checksum=$(xxh128digest $TESTDIR/out)

if [[ "$data_checksum" != "$out_checksum" ]]; then
    log_fail "checksum mismatch ($data_checksum != $out_checksum)"
fi

log_must rm -f $TESTDIR/out
log_must zfs destroy $TESTPOOL/gang

# Test common large block configuration with encryption.
log_must zfs create \
	-o recordsize=1m \
	-o primarycache=metadata \
	-o compression=off \
	-o encryption=on \
	-o keyformat=passphrase \
	-o keylocation=file://$keyfile \
	-o copies=2 \
	$TESTPOOL/gang
mntpnt=$(get_prop mountpoint $TESTPOOL/gang)

log_must dd if=$TESTDIR/data of=$mntpnt/file bs=$bs count=$count
sync_pool $TESTPOOL
log_must dd if=$mntpnt/file of=$TESTDIR/out bs=$bs count=$count
out_checksum=$(xxh128digest $TESTDIR/out)

if [[ "$data_checksum" != "$out_checksum" ]]; then
    log_fail "checksum mismatch ($data_checksum != $out_checksum)"
fi

log_must rm -f $TESTDIR/out
log_must zfs destroy $TESTPOOL/gang

log_pass "Verified IO when file system is full and ganging."
