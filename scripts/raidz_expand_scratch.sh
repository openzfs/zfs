#!/bin/bash -x

p=1
w=30

rm -r /var/tmp/expand_vdevs
mkdir /var/tmp/expand_vdevs

for (( i=0; i<$w-1; i=i+1 )); do
	truncate -s 128m /var/tmp/expand_vdevs/$i
done

# disable embedded slog so that the beginning of the disk can be used
echo 99999 >/sys/module/zfs/parameters/zfs_embedded_slog_min_ms

# make sure we can fill almost all the space
echo 10 >/sys/module/zfs/parameters/spa_slop_shift

zpool create -o cachefile=none -O version=4 test raidz$p /var/tmp/expand_vdevs/*

truncate -s 128m /var/tmp/expand_vdevs/$(($w-1))

zfs create test/fs

dd if=/dev/urandom of=/test/fs/file bs=1024k

echo 4 >/sys/module/zfs/parameters/raidz_expand_max_offset_pause

zpool attach test raidz$p-0 /var/tmp/expand_vdevs/$((w-1))

sleep 5

less /proc/spl/kstat/zfs/dbgmsg
# should see that scratch space is copied

# XXX test suite can't reboot.  May need to change this test to run in
# ztest instead.  Or make "zpool export" work while in this state?
echo "rebooting in 5 seconds..."
sleep 5
echo c > /proc/sysrq-trigger

# XXX after reboot, check that uberblock has reflow state=1 [IN_USE] and
# that we can import the pool and it continues expanding
