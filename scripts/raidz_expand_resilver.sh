#!/bin/bash -x

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

p=1

rm -r /var/tmp/expand_vdevs
mkdir /var/tmp/expand_vdevs

for (( i=0; i<$w-1; i=i+1 )); do
	truncate -s 128m /var/tmp/expand_vdevs/$i
done

# disable embedded slog so that the beginning of the disk can be used
echo 99999 >/sys/module/zfs/parameters/zfs_embedded_slog_min_ms

zpool create -o cachefile=none test raidz$p /var/tmp/expand_vdevs/*

truncate -s 128m /var/tmp/expand_vdevs/$(($w-1))

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

randbyte=$(( ((RANDOM<<15) + RANDOM) % (128 * (w-1) * 1024 * 1024 / 2) ))

echo $randbyte >/sys/module/zfs/parameters/raidz_expand_max_offset_pause

zpool attach test raidz$p-0 /var/tmp/expand_vdevs/$((w-1))

wait_expand_paused

function test_replace
{
	i=$(( RANDOM % (w) ))
	for (( j=0; j<p; j=j+1 )); do
		zpool offline test /var/tmp/expand_vdevs/$((i+j))
		dd if=/dev/zero of=/var/tmp/expand_vdevs/$((i+j)) bs=1024k count=128 conv=notrunc
	done
	for (( j=0; j<p; j=j+1 )); do
		zpool replace test /var/tmp/expand_vdevs/$((i+j))
	done
	zpool wait test
	zpool status test
	zpool status test | grep 'with 0 errors' || exit 2
	zpool clear test
	zpool scrub -w test
	zpool status test
	zpool status test | grep 'repaired 0B in' || exit 2
}

for (( i=0; i<1; i++ )); do
	test_replace

	randbyte=$(( randbyte + ((RANDOM<<15) + RANDOM) % (16 * (w-1) * 1024 * 1024) ))

	echo $randbyte >/sys/module/zfs/parameters/raidz_expand_max_offset_pause

	wait_expand_paused
done

echo 1000000000000000 >/sys/module/zfs/parameters/raidz_expand_max_offset_pause
wait_expand_paused

zpool scrub -w test
zpool status test
zpool status test | grep 'repaired 0B in' || exit 2
