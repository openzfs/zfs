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
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
#	'zfs get' reads the ZPL properties of an encrypted filesystem
#	after its pool is imported, before and after its key is
#	loaded, and gets the same values as once it is mounted.
#
# STRATEGY:
#	1. Enable compressed ARC. Create a pool with ashift=9 on a
#	   file vdev, with an encrypted root filesystem.
#	2. Export the pool and import it without loading the key.
#	3. Check with zdb that the master node block is compressed.
#	4. Read the ZPL properties.
#	5. Load the key and read them again.
#	6. Mount the filesystem and read them a third time.
#	7. Check that all three reads match.
#

verify_runnable "both"

typeset props="version,normalization,utf8only,casesensitivity"
typeset passphrase="password"

function cleanup
{
	destroy_pool $TESTPOOL1
	log_must rm -f $VDEV0
	log_must mkfile $FILE_SIZE $VDEV0
	restore_tunable COMPRESSED_ARC_ENABLED
}
log_onexit cleanup

log_assert "'zfs get' reads the ZPL properties of an encrypted" \
	"filesystem before its key is loaded"

log_must save_tunable COMPRESSED_ARC_ENABLED
log_must set_tunable64 COMPRESSED_ARC_ENABLED 1

log_must eval "echo $passphrase | zpool create -o ashift=9" \
	"-O encryption=on -O keyformat=passphrase" \
	"-O keylocation=prompt $TESTPOOL1 $VDEV0"
log_must zpool export $TESTPOOL1
log_must zpool import -d $DEVICE_DIR $TESTPOOL1
log_must test "$(get_prop keystatus $TESTPOOL1)" = "unavailable"

typeset sizes=$(zdb -dddddd $TESTPOOL1 1 | \
	grep -m1 -oE '[0-9a-f]+L/[0-9a-f]+P')
typeset lsize=${sizes%%L/*}
typeset psize=${sizes#*L/}
psize=${psize%P}
log_note "master node block: $sizes"
if [[ -z $lsize || -z $psize || $lsize == $psize ]]; then
	log_fail "master node block is not compressed: '$sizes'"
fi

typeset nokey
nokey=$(zfs get -H -o value $props $TESTPOOL1) || \
	log_fail "zfs get failed with the key not loaded"

log_must eval "echo $passphrase | zfs load-key $TESTPOOL1"
typeset loaded
loaded=$(zfs get -H -o value $props $TESTPOOL1) || \
	log_fail "zfs get failed with the key loaded"

log_must zfs mount $TESTPOOL1
log_must mounted $TESTPOOL1
typeset mnt
mnt=$(zfs get -H -o value $props $TESTPOOL1) || \
	log_fail "zfs get failed with the filesystem mounted"

log_note "key not loaded:" $nokey
log_note "key loaded:" $loaded
log_note "mounted:" $mnt
[[ "$nokey" == "$mnt" ]] || \
	log_fail "values read without the key differ from mounted"
[[ "$loaded" == "$mnt" ]] || \
	log_fail "values read with the key loaded differ from mounted"

log_pass "'zfs get' reads the ZPL properties of an encrypted" \
	"filesystem before its key is loaded"
