#!/bin/bash -x

# XXX this test is good to run in a loop since it varies its config.
# For test suite we may want to run each config?  (recordsize and
# NPARITY and width) XXX maybe just use a fixed width?

function wait_expand_paused
{
	oldcopied='0'
	newcopied='1'
	while [[ $oldcopied != $newcopied ]]; do
		oldcopied=$newcopied
		sleep 5
		newcopied=$(zpool status $TESTPOOL | \
		    grep 'copied out of' | \
		    awk '{print $1}')
	done
}


p=$(( (RANDOM % 3) + 1 ))
w=$(( (RANDOM % 10) + p * 3 ))

zpool destroy test

rm -r /var/tmp/expand_vdevs
mkdir /var/tmp/expand_vdevs

for (( i=0; i<$w-1; i=i+1 )); do
	truncate -s 128m /var/tmp/expand_vdevs/$i
done

# disable embedded slog so that the beginning of the disk can be used
echo 99999 >/sys/module/zfs/parameters/zfs_embedded_slog_min_ms

zpool create -o cachefile=none test raidz$p /var/tmp/expand_vdevs/*

truncate -s 128m /var/tmp/expand_vdevs/$(($w-1))

# keep lots of history
#echo 0 >/proc/spl/kstat/zfs/dbgmsg
#echo 2000000000 >/sys/module/zfs/parameters/zfs_dbgmsg_maxsize
#flags=$(cat /sys/module/zfs/parameters/zfs_flags)
#echo $(( flags | 512 )) >/sys/module/zfs/parameters/zfs_flags

if (( RANDOM % 2 )); then
	zfs create -o recordsize=8k test/fs
else
	zfs create -o recordsize=128k test/fs
fi

dd if=/dev/urandom of=/test/fs/file1 bs=1024k count=100
dd if=/dev/urandom of=/test/fs/file2 bs=1024k count=1
dd if=/dev/urandom of=/test/fs/file3 bs=1024k count=100
dd if=/dev/urandom of=/test/fs/file4 bs=1024k count=1
dd if=/dev/urandom of=/test/fs/file bs=1024k
rm /test/fs/file[24]

zpool attach test raidz$p-0 /var/tmp/expand_vdevs/$((w-1))

sleep 10

zpool status test

i=$(( RANDOM % (w-p) ))
for (( j=0; j<p; j=j+1 )); do
	zpool offline test /var/tmp/expand_vdevs/$((i+j))
	dd if=/dev/zero of=/var/tmp/expand_vdevs/$((i+j)) bs=1024k count=128 conv=notrunc
done

sleep 3

zpool status test
for (( j=0; j<p; j=j+1 )); do
	zpool replace test /var/tmp/expand_vdevs/$((i+j))
done

zpool wait -t replace test
zpool status test
zpool status test | grep 'with 0 errors' || exit 2

zpool wait -t raidz_expand test

zpool clear test
zpool scrub -w test
zpool status test
zpool status test | grep 'repaired 0B in' || exit 2
