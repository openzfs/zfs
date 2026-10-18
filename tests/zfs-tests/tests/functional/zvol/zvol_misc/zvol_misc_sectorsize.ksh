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
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
# Verify that the "volsectorsize" property sets the logical block size of a
# volume's block device, and that volumes created without it keep 512-byte
# sectors.
#
# STRATEGY:
# 1. Create a volume without the property and verify that its device reports
#    a 512-byte logical and a volblocksize physical block size.
# 2. Create volumes with each valid sector size and verify their devices.
# 3. Verify that invalid values are refused, and that the property cannot be
#    changed after creation.
# 4. Write and read back data through a 4 KiB sector device, and verify
#    that 512-byte direct I/O is refused there.
# 5. Verify that a clone and a received copy keep the sector size.
# 6. Verify that a forced receive replacing a volume of another sector size
#    is refused, open or not, and leaves nothing behind, while one of the
#    same sector size still works on an open volume.
# 7. Verify that the sector size persists across export and import.
#

verify_runnable "global"

function cleanup
{
	exec 5<&-
	for ds in $RECVS $RECVF $RECV $CLONE $VOL $DEFVOL; do
		datasetexists $ds && destroy_dataset $ds -r
	done
	for ds in $TESTPOOL/ss512 $TESTPOOL/ss1024 $TESTPOOL/ss2048 \
	    $TESTPOOL/ss4096 $TESTPOOL/bad $TESTPOOL/badfs \
	    $TESTPOOL/badclone; do
		datasetexists $ds && destroy_dataset $ds
	done
	rm -f $datafile $readfile $errfile
	block_device_wait
	udev_cleanup
}

# Verify the logical and physical block size of a volume's device.
function check_sizes # volume logical physical
{
	typeset dev=$ZVOL_DEVDIR/$1
	typeset lbs pbs

	block_device_wait $dev
	blockdev_exists $dev
	lbs=$(blockdev --getss $dev)
	pbs=$(blockdev --getpbsz $dev)
	if [[ "$lbs" != "$2" || "$pbs" != "$3" ]]; then
		log_fail "$1: logical/physical $lbs/$pbs, expected $2/$3"
	fi
	log_note "$1: logical/physical $lbs/$pbs"
}

# A forced receive of $VOL@snap over a 512-byte sector volume is refused.
function recv_refused # volume
{
	log_mustnot eval "zfs send $VOL@snap | zfs receive -F -s $1 2>$errfile"
	log_must grep -q "volume sector size differs" $errfile
	log_must test "$(get_prop receive_resume_token $1)" = "-"
	log_must test "$(get_prop volsectorsize $1)" = "512"
	log_mustnot datasetexists $1@snap
	sync_pool $TESTPOOL
	log_must eval "zdb -d $TESTPOOL | grep -q $1"
	log_mustnot eval "zdb -d $TESTPOOL | grep -q %recv"
}

if ! is_linux; then
	log_unsupported "Only Linux reports the logical block size this way"
fi

log_assert "Verify that 'volsectorsize' sets the logical block size of a zvol"
log_onexit cleanup

DEFVOL=$TESTPOOL/defvol
VOL=$TESTPOOL/vol4k
CLONE=$TESTPOOL/clone4k
RECV=$TESTPOOL/recv4k
RECVF=$TESTPOOL/recvf
RECVS=$TESTPOOL/recvs
datafile=$TEST_BASE_DIR/zvol_misc_sectorsize.data
readfile=$TEST_BASE_DIR/zvol_misc_sectorsize.read
errfile=$TEST_BASE_DIR/zvol_misc_sectorsize.err

# 1. The default is unchanged: 512-byte sectors, volblocksize physical blocks.
log_must zfs create -V 64M -s -b 16K $DEFVOL
check_sizes $DEFVOL 512 16384

# 2. Every valid sector size, with physical blocks of the volblocksize.
for ss in 512 1024 2048 4096; do
	log_must zfs create -V 64M -s -b 8K -o volsectorsize=$ss \
	    $TESTPOOL/ss$ss
	log_must test "$(get_prop volsectorsize $TESTPOOL/ss$ss)" = "$ss"
	check_sizes $TESTPOOL/ss$ss $ss 8192
done
log_must zfs create -V 64M -s -b 4K -o volsectorsize=4K $VOL
log_must test "$(get_prop volsectorsize $VOL)" = "4096"
log_must test "$(get_prop volsectorsize $DEFVOL)" = "512"
check_sizes $VOL 4096 4096

# 3. Invalid values, and values larger than the volblocksize, are refused,
# as is any change after creation.
for ss in 0 256 768 3000 8192 64K; do
	log_mustnot zfs create -V 64M -s -o volsectorsize=$ss $TESTPOOL/bad
done
log_mustnot zfs create -V 64M -s -b 2K -o volsectorsize=4K $TESTPOOL/bad
log_mustnot zfs create -o volsectorsize=4K $TESTPOOL/badfs
log_mustnot datasetexists $TESTPOOL/bad
log_mustnot datasetexists $TESTPOOL/badfs
log_mustnot zfs set volsectorsize=512 $VOL
log_mustnot zfs set volsectorsize=4096 $DEFVOL
log_mustnot zfs inherit volsectorsize $VOL
log_must test "$(get_prop volsectorsize $VOL)" = "4096"
log_must test "$(get_prop volsectorsize $DEFVOL)" = "512"

# 4. Data written through 4 KiB sectors reads back intact.
log_must dd if=/dev/urandom of=$datafile bs=4096 count=256
log_must dd if=$datafile of=$ZVOL_DEVDIR/$VOL bs=4096 oflag=direct
log_must dd if=$ZVOL_DEVDIR/$VOL of=$readfile bs=4096 count=256 \
    iflag=direct
log_must cmp $datafile $readfile
log_mustnot dd if=$ZVOL_DEVDIR/$VOL of=/dev/null bs=512 count=1 iflag=direct

# 5. The sector size travels with the volume's data.
log_must zfs snapshot $VOL@snap
log_must zfs clone $VOL@snap $CLONE
log_mustnot zfs clone -o volsectorsize=512 $VOL@snap $TESTPOOL/badclone
check_sizes $CLONE 4096 4096
log_must eval "zfs send $VOL@snap | zfs receive $RECV"
log_must test "$(get_prop volsectorsize $RECV)" = "4096"
check_sizes $RECV 4096 4096
log_must dd if=$ZVOL_DEVDIR/$RECV of=$readfile bs=4096 count=256 \
    iflag=direct
log_must cmp $datafile $readfile

# 6. A forced receive over a volume of another sector size is refused,
# whether the volume is open or not; over an open volume of the same sector
# size it works as before.
log_must zfs create -V 64M -s -b 4K $RECVF
check_sizes $RECVF 512 4096
exec 5< $ZVOL_DEVDIR/$RECVF
recv_refused $RECVF
exec 5<&-
recv_refused $RECVF
check_sizes $RECVF 512 4096
log_must zfs create -V 64M -s -b 4K -o volsectorsize=4K $RECVS
check_sizes $RECVS 4096 4096
exec 5< $ZVOL_DEVDIR/$RECVS
log_must eval "zfs send $VOL@snap | zfs receive -F $RECVS"
exec 5<&-
check_sizes $RECVS 4096 4096
log_must dd if=$ZVOL_DEVDIR/$RECVS of=$readfile bs=4096 count=256 \
    iflag=direct
log_must cmp $datafile $readfile

# 7. The device is created with the same sector size after an import.
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
check_sizes $DEFVOL 512 16384
check_sizes $VOL 4096 4096
check_sizes $RECVF 512 4096

log_pass "'volsectorsize' sets the logical block size of a zvol"
