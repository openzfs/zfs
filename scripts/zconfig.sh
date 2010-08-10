#!/bin/bash
#
# ZFS/ZPOOL configuration test script.

basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zconfig.sh

usage() {
cat << EOF
USAGE:
$0 [hvc]

DESCRIPTION:
	ZFS/ZPOOL configuration tests

OPTIONS:
	-h      Show this message
	-v      Verbose
	-c      Cleanup lo+file devices at start

EOF
}

while getopts 'hvc?' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	c)
		CLEANUP=1
		;;
	?)
		usage
		exit
		;;
	esac
done

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

# Perform pre-cleanup is requested
if [ ${CLEANUP} ]; then
	cleanup_loop_devices
	rm -f /tmp/zpool.cache.*
fi

zconfig_partition() {
	local DEVICE=$1
	local START=$2
	local END=$3
	local TMP_FILE=`mktemp`

	/sbin/sfdisk -q ${DEVICE} << EOF &>${TMP_FILE} || fail 4
${START},${END}
;
;
;
EOF

	rm ${TMP_FILE}
}

# Validate persistent zpool.cache configuration.
zconfig_test1() {
	local POOL_NAME=test1
	local TMP_FILE1=`mktemp`
	local TMP_FILE2=`mktemp`
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 1 - persistent zpool.cache: "

	# Create a pool save its status for comparison.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1} || fail 3

	# Unload/load the module stack and verify the pool persists.
	${ZFS_SH} -u || fail 4
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 5
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 6
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 7

	# Cleanup the test pool and temporary files
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 8
	rm -f ${TMP_FILE1} ${TMP_FILE2} ${TMP_CACHE} || fail 9
	${ZFS_SH} -u || fail 10

	pass
}
zconfig_test1

# Validate ZFS disk scanning and import w/out zpool.cache configuration.
zconfig_test2() {
	local POOL_NAME=test2
	local TMP_FILE1=`mktemp`
	local TMP_FILE2=`mktemp`
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 2 - scan disks for pools to import: "

	# Create a pool save its status for comparison.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1} || fail 3

	# Unload the module stack, remove the cache file, load the module
	# stack and attempt to probe the disks to import the pool.  As
	# a cross check verify the old pool state against the imported.
	${ZFS_SH} -u || fail 4
	rm -f ${TMP_CACHE} || fail 5
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 6
	${ZPOOL} import | grep ${POOL_NAME} >/dev/null || fail 7
	${ZPOOL} import ${POOL_NAME} || fail 8
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 9
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 10

	# Cleanup the test pool and temporary files
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 11
	rm -f ${TMP_FILE1} ${TMP_FILE2} || fail 12
	${ZFS_SH} -u || fail 13

	pass
}
zconfig_test2

zconfig_zvol_device_stat() {
	local EXPECT=$1
	local POOL_NAME=/dev/$2
	local ZVOL_NAME=/dev/$3
	local SNAP_NAME=/dev/$4
	local CLONE_NAME=/dev/$5
	local COUNT=0

	# Briefly delay for udev
	sleep 1

	# Pool exists
	stat ${POOL_NAME} &>/dev/null   && let COUNT=$COUNT+1

	# Volume and partitions
	stat ${ZVOL_NAME}  &>/dev/null  && let COUNT=$COUNT+1
	stat ${ZVOL_NAME}1 &>/dev/null  && let COUNT=$COUNT+1
	stat ${ZVOL_NAME}2 &>/dev/null  && let COUNT=$COUNT+1

	# Snapshot with partitions
	stat ${SNAP_NAME}  &>/dev/null  && let COUNT=$COUNT+1
	stat ${SNAP_NAME}1 &>/dev/null  && let COUNT=$COUNT+1
	stat ${SNAP_NAME}2 &>/dev/null  && let COUNT=$COUNT+1

	# Clone with partitions
	stat ${CLONE_NAME}  &>/dev/null && let COUNT=$COUNT+1
	stat ${CLONE_NAME}1 &>/dev/null && let COUNT=$COUNT+1
	stat ${CLONE_NAME}2 &>/dev/null && let COUNT=$COUNT+1

	if [ $EXPECT -ne $COUNT ]; then
		return 1
	fi

	return 0
}

# zpool import/export device check
# (1 volume, 2 partitions, 1 snapshot, 1 clone)
zconfig_test3() {
	local POOL_NAME=tank
	local ZVOL_NAME=volume
	local SNAP_NAME=snap
	local CLONE_NAME=clone
	local FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	local FULL_CLONE_NAME=${POOL_NAME}/${CLONE_NAME}
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 3 - zpool import/export device: "

	# Create a pool, volume, partition, snapshot, and clone.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 100M ${FULL_ZVOL_NAME} || fail 3
	zconfig_partition /dev/${FULL_ZVOL_NAME} 0 64 || fail 4
	${ZFS} snapshot ${FULL_SNAP_NAME} || fail 5
	${ZFS} clone ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 6

	# Verify the devices were created
	zconfig_zvol_device_stat 10 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 7

	# Export the pool
	${ZPOOL} export ${POOL_NAME} || fail 8

	# verify the devices were removed
	zconfig_zvol_device_stat 0 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 9

	# Import the pool, wait 1 second for udev
	${ZPOOL} import ${POOL_NAME} || fail 10

	# Verify the devices were created
	zconfig_zvol_device_stat 10 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 11

	# Destroy the pool and consequently the devices
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 12

	# verify the devices were removed
	zconfig_zvol_device_stat 0 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 13

	${ZFS_SH} -u || fail 14
	rm -f ${TMP_CACHE} || fail 15

	pass
}
zconfig_test3

# zpool insmod/rmmod device check (1 volume, 1 snapshot, 1 clone)
zconfig_test4() {
	POOL_NAME=tank
	ZVOL_NAME=volume
	SNAP_NAME=snap
	CLONE_NAME=clone
	FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	FULL_CLONE_NAME=${POOL_NAME}/${CLONE_NAME}
	TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 4 - zpool insmod/rmmod device: "

	# Create a pool, volume, snapshot, and clone
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 100M ${FULL_ZVOL_NAME} || fail 3
	zconfig_partition /dev/${FULL_ZVOL_NAME} 0 64 || fail 4
	${ZFS} snapshot ${FULL_SNAP_NAME} || fail 5
	${ZFS} clone ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 6

	# Verify the devices were created
	zconfig_zvol_device_stat 10 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 7

	# Unload the modules
	${ZFS_SH} -u || fail 8

	# Verify the devices were removed
	zconfig_zvol_device_stat 0 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 9

	# Load the modules, wait 1 second for udev
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 10

	# Verify the devices were created
	zconfig_zvol_device_stat 10 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 11

	# Destroy the pool and consequently the devices
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 12

	# Verify the devices were removed
	zconfig_zvol_device_stat 0 ${POOL_NAME} ${FULL_ZVOL_NAME} \
	    ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 13

	${ZFS_SH} -u || fail 14
	rm -f ${TMP_CACHE} || fail 15

	pass
}
zconfig_test4

# ZVOL volume sanity check
zconfig_test5() {
	local POOL_NAME=tank
	local ZVOL_NAME=fish
	local FULL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local SRC_DIR=/bin/
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 5 - zvol+ext3 volume: "

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 400M ${FULL_NAME} || fail 3

	# Partition the volume, for a 400M volume there will be
	# 812 cylinders, 16 heads, and 63 sectors per track.
	zconfig_partition /dev/${FULL_NAME} 0 812

	# Format the partition with ext3.
	/sbin/mkfs.ext3 -q /dev/${FULL_NAME}1 || fail 5

	# Mount the ext3 filesystem and copy some data to it.
	mkdir -p /tmp/${ZVOL_NAME}1 || fail 6
	mount /dev/${FULL_NAME}1 /tmp/${ZVOL_NAME}1 || fail 7
	cp -RL ${SRC_DIR} /tmp/${ZVOL_NAME}1 || fail 8
	sync

	# Verify the copied files match the original files.
	diff -ur ${SRC_DIR} /tmp/${ZVOL_NAME}1${SRC_DIR} &>/dev/null || fail 9

	# Remove the files, umount, destroy the volume and pool.
	rm -Rf /tmp/${ZVOL_NAME}1${SRC_DIR}* || fail 10
	umount /tmp/${ZVOL_NAME}1 || fail 11
	rmdir /tmp/${ZVOL_NAME}1 || fail 12

	${ZFS} destroy ${FULL_NAME} || fail 13
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 14
	${ZFS_SH} -u || fail 15
	rm -f ${TMP_CACHE} || fail 16

	pass
}
zconfig_test5

# ZVOL snapshot sanity check
zconfig_test6() {
	local POOL_NAME=tank
	local ZVOL_NAME=fish
	local SNAP_NAME=pristine
	local FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	local SRC_DIR=/bin/
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 6 - zvol+ext2 snapshot: "

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 400M ${FULL_ZVOL_NAME} || fail 3

	# Partition the volume, for a 400M volume there will be
	# 812 cylinders, 16 heads, and 63 sectors per track.
	zconfig_partition /dev/${FULL_ZVOL_NAME} 0 812

	# Format the partition with ext2 (no journal).
	/sbin/mkfs.ext2 -q /dev/${FULL_ZVOL_NAME}1 || fail 5

	# Mount the ext3 filesystem and copy some data to it.
	mkdir -p /tmp/${ZVOL_NAME}1 || fail 6
	mount /dev/${FULL_ZVOL_NAME}1 /tmp/${ZVOL_NAME}1 || fail 7

	# Snapshot the pristine ext2 filesystem and mount it read-only.
	${ZFS} snapshot ${FULL_SNAP_NAME} && sleep 1 || fail 8
	mkdir -p /tmp/${SNAP_NAME}1 || fail 9
	mount /dev/${FULL_SNAP_NAME}1 /tmp/${SNAP_NAME}1 &>/dev/null || fail 10

	# Copy to original volume
	cp -RL ${SRC_DIR} /tmp/${ZVOL_NAME}1 || fail 11
	sync

	# Verify the copied files match the original files,
	# and the copied files do NOT appear in the snapshot.
	diff -ur ${SRC_DIR} /tmp/${ZVOL_NAME}1${SRC_DIR} &>/dev/null || fail 12
	diff -ur ${SRC_DIR} /tmp/${SNAP_NAME}1${SRC_DIR} &>/dev/null && fail 13

	# umount, destroy the snapshot, volume, and pool.
	umount /tmp/${SNAP_NAME}1 || fail 14
	rmdir /tmp/${SNAP_NAME}1 || fail 15
	${ZFS} destroy ${FULL_SNAP_NAME} || fail 16

	umount /tmp/${ZVOL_NAME}1 || fail 17
	rmdir /tmp/${ZVOL_NAME}1 || fail 18
	${ZFS} destroy ${FULL_ZVOL_NAME} || fail 19

	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 20
	${ZFS_SH} -u || fail 21
	rm -f ${TMP_CACHE} || fail 22

	pass
}
zconfig_test6

# ZVOL clone sanity check
zconfig_test7() {
	local POOL_NAME=tank
	local ZVOL_NAME=fish
	local SNAP_NAME=pristine
	local CLONE_NAME=clone
	local FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	local FULL_CLONE_NAME=${POOL_NAME}/${CLONE_NAME}
	local SRC_DIR=/bin/
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 7 - zvol+ext2 clone: "

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 400M ${FULL_ZVOL_NAME} || fail 3

	# Partition the volume, for a 400M volume there will be
	# 812 cylinders, 16 heads, and 63 sectors per track.
	zconfig_partition /dev/${FULL_ZVOL_NAME} 0 812

	# Format the partition with ext2 (no journal).
	/sbin/mkfs.ext2 -q /dev/${FULL_ZVOL_NAME}1 || fail 5

	# Mount the ext3 filesystem and copy some data to it.
	mkdir -p /tmp/${ZVOL_NAME}1 || fail 6
	mount /dev/${FULL_ZVOL_NAME}1 /tmp/${ZVOL_NAME}1 || fail 7

	# Snapshot the pristine ext2 filesystem and mount it read-only.
	${ZFS} snapshot ${FULL_SNAP_NAME} && sleep 1 || fail 8
	mkdir -p /tmp/${SNAP_NAME}1 || fail 9
	mount /dev/${FULL_SNAP_NAME}1 /tmp/${SNAP_NAME}1 &>/dev/null || fail 10

	# Copy to original volume.
	cp -RL ${SRC_DIR} /tmp/${ZVOL_NAME}1 || fail 11
	sync

	# Verify the copied files match the original files,
	# and the copied files do NOT appear in the snapshot.
	diff -ur ${SRC_DIR} /tmp/${ZVOL_NAME}1${SRC_DIR} &>/dev/null || fail 12
	diff -ur ${SRC_DIR} /tmp/${SNAP_NAME}1${SRC_DIR} &>/dev/null && fail 13

	# Clone from the original pristine snapshot
	${ZFS} clone ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} && sleep 1 || fail 14
	mkdir -p /tmp/${CLONE_NAME}1 || fail 15
	mount /dev/${FULL_CLONE_NAME}1 /tmp/${CLONE_NAME}1 || fail 16

	# Verify the clone matches the pristine snapshot,
	# and the files copied to the original volume are NOT there.
	diff -ur /tmp/${SNAP_NAME}1 /tmp/${CLONE_NAME}1 &>/dev/null || fail 17
	diff -ur /tmp/${ZVOL_NAME}1 /tmp/${CLONE_NAME}1 &>/dev/null && fail 18

	# Copy to cloned volume.
	cp -RL ${SRC_DIR} /tmp/${CLONE_NAME}1 || fail 19
	sync

	# Verify the clone matches the modified original volume.
	diff -ur /tmp/${ZVOL_NAME}1 /tmp/${CLONE_NAME}1 &>/dev/null || fail 20

	# umount, destroy the snapshot, volume, and pool.
	umount /tmp/${CLONE_NAME}1 || fail 21
	rmdir /tmp/${CLONE_NAME}1 || fail 22
	${ZFS} destroy ${FULL_CLONE_NAME} || fail 23

	umount /tmp/${SNAP_NAME}1 || fail 24
	rmdir /tmp/${SNAP_NAME}1 || fail 25
	${ZFS} destroy ${FULL_SNAP_NAME} || fail 26

	umount /tmp/${ZVOL_NAME}1 || fail 27
	rmdir /tmp/${ZVOL_NAME}1 || fail 28
	${ZFS} destroy ${FULL_ZVOL_NAME} || fail 29

	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 30
	${ZFS_SH} -u || fail 31
	rm -f ${TMP_CACHE} || fail 32

	pass
}
zconfig_test7

exit 0
