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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
# An encrypted objset written while its user accounting is incomplete
# keeps a local MAC over its accounting dnodes, so that releases that
# don't check OBJSET_FLAG_USERACCOUNTING_COMPLETE can still read it.
#
# STRATEGY:
# 1. In a pool with projectmac=off, create an encrypted source dataset
#    with files owned by a user
# 2. Raw send it to a target, change the target and snapshot it
# 3. Raw send the target's snapshot back to the source, without mounting
#    it, so that the source's user accounting is incomplete and its local
#    MAC is zeroed
# 4. Write to the source without mounting it, by setting a user quota
# 5. Export the pool and check that the source's objset still has
#    incomplete user accounting, and that its local MAC is no longer zero
# 6. Import the pool, mount the source and check its data and accounting
#

function cleanup
{
	destroy_pool $POOLNAME
	rm -rf $FILEDIR
}

log_onexit cleanup

FILEDIR="$TEST_BASE_DIR/userspace_incomplete_mac"
FILEDEV="$FILEDIR/dev"
POOLNAME="testpool$$"
ENC_SOURCE="$POOLNAME/source"
ENC_TARGET="$POOLNAME/target"
NFILES=2000

# Offsets in objset_phys_t
OS_FLAGS_OFF=712
OS_LOCAL_MAC_OFF=752

#
# Print bytes of the objset_phys_t of an exported pool's dataset, in hex.
#
function objset_bytes # <dataset> <offset> <count>
{
	typeset dva=$(zdb -e -p $FILEDIR -dddd $1 | head -1 | sed -n \
	    's/.*rootbp DVA\[0\]=<\([0-9a-f]*:[0-9a-f]*:[0-9a-f]*\)>.*/\1/p')

	[[ -n $dva ]] || log_fail "no rootbp for $1"
	zdb -e -p $FILEDIR -R $POOLNAME $dva:r | \
	    od -v -A n -t x1 -j $2 -N $3 | tr -d ' \n'
}

log_assert "An objset written while its user accounting is incomplete" \
	"keeps a local MAC over its accounting dnodes"

# The form with project accounting is zeroed while accounting is incomplete.
log_must mkdir -p $FILEDIR
log_must truncate -s 200m $FILEDEV
log_must zpool create -O compression=off -o feature@encryption=enabled \
	-o projectmac=off \
	$POOLNAME $FILEDEV
log_must eval "echo 'password' | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $ENC_SOURCE"

# 1.
mkmount_writable $ENC_SOURCE
mntpnt=$(get_prop mountpoint $ENC_SOURCE)
typeset -i i=0
while ((i < NFILES)); do
	: > $mntpnt/f$i
	((i++))
done
log_must mkfile 1m $mntpnt/file1
log_must chown -R $QUSER1 $mntpnt
sync_pool $POOLNAME
log_must zfs snap $ENC_SOURCE@base

# 2.
log_must eval "zfs send -w $ENC_SOURCE@base | zfs recv $ENC_TARGET"
log_must eval "echo 'password' | zfs load-key $ENC_TARGET"
log_must zfs mount $ENC_TARGET
log_must user_run $QUSER1 mkfile 1m $(get_prop mountpoint $ENC_TARGET)/file2
sync_pool $POOLNAME
log_must zfs snap $ENC_TARGET@s1

# 3.
log_must zfs umount $ENC_SOURCE
log_must zfs unload-key $ENC_SOURCE
log_must eval "zfs send -w -i @base $ENC_TARGET@s1 | zfs recv -u $ENC_SOURCE"

# 4.
log_must eval "echo 'password' | zfs load-key $ENC_SOURCE"
log_must zfs set userquota@$QUSER1=50m $ENC_SOURCE
log_must zpool export $POOLNAME

# 5.
# os_flags is a native-endian uint64_t whose flags all fit in its low byte,
# so test the first and last bytes.
flags=$(objset_bytes $ENC_SOURCE $OS_FLAGS_OFF 8)
log_note "os_flags: $flags"
(( ((16#${flags:0:2} | 16#${flags:14:2}) & 1) == 0 )) || \
	log_unsupported "user accounting finished before the pool was exported"
mac=$(objset_bytes $ENC_SOURCE $OS_LOCAL_MAC_OFF 32)
log_note "os_local_mac: $mac"
[[ ${#mac} -eq 64 ]] || log_fail "couldn't read the local MAC"
[[ $mac != +(0) ]] || \
	log_fail "local MAC was zeroed while user accounting was incomplete"

# 6.
log_must zpool import -d $FILEDIR $POOLNAME
log_must eval "echo 'password' | zfs load-key $ENC_SOURCE"
log_must zfs mount $ENC_SOURCE
log_must test -f $mntpnt/file2
# The accounting upgrade that mounting starts runs asynchronously.
typeset -i tries=0
while ! zfs userspace -H $ENC_SOURCE 2>/dev/null | grep -q $QUSER1; do
	((tries++ < 30)) || log_fail "zfs userspace $ENC_SOURCE didn't recover"
	sleep 1
done

log_pass "An objset written while its user accounting is incomplete" \
	"keeps a local MAC over its accounting dnodes"
