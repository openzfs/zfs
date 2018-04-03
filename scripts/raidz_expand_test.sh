#!/bin/bash -x

combrec=1
BASE_DIR=$(dirname "$0")/..

echo 1 >/sys/module/zfs/parameters/zfs_prefetch_disable

zpool destroy test
zpool create filepool sdb

zfs destroy -R filepool/files
zfs create -o compression=on filepool/files

dir=/filepool/files

for (( i=0; i<7; i=i+1 )); do
	truncate -s 512M $dir/$i
done

function wait_completion
{
	while zpool status test | grep "in progress"; do
		sleep 5
	done
}

function dotest
{
	nparity=$1

	zpool create -o cachefile=none test raidz$nparity $dir/[0-5]
	zfs set primarycache=metadata test

	zfs create test/fs
	dd if=/dev/urandom of=/test/fs/file bs=1024k count=1

	zfs create -o compress=on test/fs2
	cp -r $BASE_DIR /test/fs2/
	#truncate -s 100m /test/fs2/file
	#/net/pharos/export/home/mahrens/randwritecomp-linux /test/fs2/file 10000

	zfs create -o compress=on -o recordsize=8k test/fs3
	cp -r $BASE_DIR /test/fs3/
	#truncate -s 100m /test/fs3/file
	#/net/pharos/export/home/mahrens/randwritecomp-linux /test/fs3/file 10000

	zfs snapshot filepool/files@pre-attach

	sum /test/fs/file
	sum /test/fs2/file
	sum /test/fs3/file

	zfs list test
	zpool list -v test

	sleep 2

	zpool attach test raidz$nparity-0 $dir/6

	wait_completion

	zfs list test
	zpool list -v test
	# should indicate new device is present, pool is larger size

	zfs snapshot filepool/files@post-attach

	zpool export test
	zpool import -o cachefile=none -d $dir test

	zfs snapshot filepool/files@post-import

	sum /test/fs/file
	sum /test/fs2/file
	sum /test/fs3/file
	zfs list -r test
	zpool list -v test
	zpool status -v test
	zpool scrub test
	wait_completion
	zpool status -v test

	zpool export test
	zpool import -o cachefile=none -d $dir test

	for (( i=0; i<$nparity; i=i+1 )); do
		if [[ ! $combrec ]]; then
			zpool offline test $dir/$i
		fi
		dd conv=notrunc if=/dev/zero of=$dir/$i bs=1024k seek=4 count=500
	done
	sum /test/fs/file
	zpool status -v test
	
	if [[ $combrec ]]; then
		zpool scrub test
	else
		for (( i=0; i<$nparity; i=i+1 )); do
			zpool replace -f test $dir/$i
		done
	fi
	wait_completion
	zpool status -v test
	zpool clear test

	for (( i=$nparity; i<$nparity*2; i=i+1 )); do
		if [[ ! $combrec ]]; then
			zpool offline test $dir/$i
		fi
		dd conv=notrunc if=/dev/zero of=$dir/$i bs=1024k seek=4 count=500
	done
	# XXX sometimes, scrub was already started
	# XXX some READ (not CKSUM) errors reported
	zpool status -v test
	if [[ $combrec ]]; then
		# XXX if scrub already started above, this scrub doesn't seem to repair everything, some
		# repairs happen in final scrub
		zpool scrub test
	else
		for (( i=0; i<$nparity; i=i+1 )); do
			zpool replace -f test $dir/$i
		done
	fi
	wait_completion
	zpool status -v test
	zpool clear test

	sum /test/fs3/file

	zpool scrub test
	wait_completion
	zpool status -v test

	zpool destroy test
}

dotest 2
dotest 3
dotest 1
