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
$0 [hvcts]

DESCRIPTION:
	ZFS/ZPOOL configuration tests

OPTIONS:
	-h      Show this message
	-v      Verbose
	-c      Cleanup lo+file devices at start
	-t <#>  Run listed tests
	-s <#>  Skip listed tests

EOF
}

while getopts 'hvct:s:?' OPTION; do
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
	t)
		TESTS_RUN=($OPTARG)
		;;
	s)
		TESTS_SKIP=($OPTARG)
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

# Initialize the test suite
init

# Perform pre-cleanup is requested
if [ ${CLEANUP} ]; then
	${ZFS_SH} -u
	cleanup_md_devices
	cleanup_loop_devices
	rm -f /tmp/zpool.cache.*
fi

# Check if we need to skip the tests that require scsi_debug and lsscsi.
SCSI_DEBUG=0
${INFOMOD} scsi_debug &>/dev/null && SCSI_DEBUG=1
HAVE_LSSCSI=0
test -f ${LSSCSI} && HAVE_LSSCSI=1
if [ ${SCSI_DEBUG} -eq 0 ] || [ ${HAVE_LSSCSI} -eq 0 ]; then
	echo "Skipping test 10 which requires the scsi_debug " \
		"module and the ${LSSCSI} utility"
fi

# Validate persistent zpool.cache configuration.
test_1() {
	local POOL_NAME=test1
	local TMP_FILE1=`mktemp`
	local TMP_FILE2=`mktemp`
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	# Create a pool save its status for comparison.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1} || fail 3

	# Unload/load the module stack and verify the pool persists.
	${ZFS_SH} -u || fail 4
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 5
	${ZPOOL} import -c ${TMP_CACHE} ${POOL_NAME} || fail 5
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 6
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 7

	# Cleanup the test pool and temporary files
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 8
	rm -f ${TMP_FILE1} ${TMP_FILE2} ${TMP_CACHE} || fail 9
	${ZFS_SH} -u || fail 10

	pass
}
run_test 1 "persistent zpool.cache"

# Validate ZFS disk scanning and import w/out zpool.cache configuration.
test_2() {
	local POOL_NAME=test2
	local TMP_FILE1=`mktemp`
	local TMP_FILE2=`mktemp`
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

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
	${ZPOOL} import -d /dev ${POOL_NAME} || fail 8
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 9
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 10

	# Cleanup the test pool and temporary files
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 11
	rm -f ${TMP_FILE1} ${TMP_FILE2} || fail 12
	${ZFS_SH} -u || fail 13

	pass
}
run_test 2 "scan disks for pools to import"

zconfig_zvol_device_stat() {
	local EXPECT=$1
	local POOL_NAME=/dev/zvol/$2
	local ZVOL_NAME=/dev/zvol/$3
	local SNAP_NAME=/dev/zvol/$4
	local CLONE_NAME=/dev/zvol/$5
	local COUNT=0

	# Briefly delay for udev
	udev_trigger

	# Pool exists
	stat ${POOL_NAME} &>/dev/null   && let COUNT=$COUNT+1

	# Volume and partitions
	stat ${ZVOL_NAME}  &>/dev/null  && let COUNT=$COUNT+1
	stat ${ZVOL_NAME}-part1 &>/dev/null  && let COUNT=$COUNT+1
	stat ${ZVOL_NAME}-part2 &>/dev/null  && let COUNT=$COUNT+1

	# Snapshot with partitions
	stat ${SNAP_NAME}  &>/dev/null  && let COUNT=$COUNT+1
	stat ${SNAP_NAME}-part1 &>/dev/null  && let COUNT=$COUNT+1
	stat ${SNAP_NAME}-part2 &>/dev/null  && let COUNT=$COUNT+1

	# Clone with partitions
	stat ${CLONE_NAME}  &>/dev/null && let COUNT=$COUNT+1
	stat ${CLONE_NAME}-part1 &>/dev/null && let COUNT=$COUNT+1
	stat ${CLONE_NAME}-part2 &>/dev/null && let COUNT=$COUNT+1

	if [ $EXPECT -ne $COUNT ]; then
		return 1
	fi

	return 0
}

# zpool import/export device check
# (1 volume, 2 partitions, 1 snapshot, 1 clone)
test_3() {
	local POOL_NAME=tank
	local ZVOL_NAME=volume
	local SNAP_NAME=snap
	local CLONE_NAME=clone
	local FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	local FULL_CLONE_NAME=${POOL_NAME}/${CLONE_NAME}
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	# Create a pool, volume, partition, snapshot, and clone.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 100M ${FULL_ZVOL_NAME} || fail 3
	${ZFS} set snapdev=visible ${FULL_ZVOL_NAME} || fail 3
	label /dev/zvol/${FULL_ZVOL_NAME} msdos || fail 4
	partition /dev/zvol/${FULL_ZVOL_NAME} primary 1% 50% || fail 4
	partition /dev/zvol/${FULL_ZVOL_NAME} primary 51% -1 || fail 4
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
run_test 3 "zpool import/export device"

# zpool insmod/rmmod device check (1 volume, 1 snapshot, 1 clone)
test_4() {
	POOL_NAME=tank
	ZVOL_NAME=volume
	SNAP_NAME=snap
	CLONE_NAME=clone
	FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	FULL_CLONE_NAME=${POOL_NAME}/${CLONE_NAME}
	TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	# Create a pool, volume, snapshot, and clone
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 100M ${FULL_ZVOL_NAME} || fail 3
	${ZFS} set snapdev=visible ${FULL_ZVOL_NAME} || fail 3
	label /dev/zvol/${FULL_ZVOL_NAME} msdos || fail 4
	partition /dev/zvol/${FULL_ZVOL_NAME} primary 1% 50% || fail 4
	partition /dev/zvol/${FULL_ZVOL_NAME} primary 51% -1 || fail 4
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

	# Load the modules, list the pools to ensure they are opened
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 10
	${ZPOOL} import -c ${TMP_CACHE} ${POOL_NAME} || fail 10
	${ZPOOL} list &>/dev/null

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
run_test 4 "zpool insmod/rmmod device"

# ZVOL volume sanity check
test_5() {
	local POOL_NAME=tank
	local ZVOL_NAME=fish
	local FULL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raid0 || fail 2
	${ZFS} create -V 800M ${FULL_NAME} || fail 3
	label /dev/zvol/${FULL_NAME} msdos || fail 4
	partition /dev/zvol/${FULL_NAME} primary 1 -1 || fail 4
	format /dev/zvol/${FULL_NAME}-part1 ext2 || fail 5

	# Mount the ext2 filesystem and copy some data to it.
	mkdir -p /tmp/${ZVOL_NAME}-part1 || fail 6
	mount /dev/zvol/${FULL_NAME}-part1 /tmp/${ZVOL_NAME}-part1 || fail 7
	cp -RL ${SRC_DIR} /tmp/${ZVOL_NAME}-part1 || fail 8
	sync

	# Verify the copied files match the original files.
	diff -ur ${SRC_DIR} /tmp/${ZVOL_NAME}-part1/${SRC_DIR##*/} \
		&>/dev/null || fail 9

	# Remove the files, umount, destroy the volume and pool.
	rm -Rf /tmp/${ZVOL_NAME}-part1/${SRC_DIR##*/} || fail 10
	umount /tmp/${ZVOL_NAME}-part1 || fail 11
	rmdir /tmp/${ZVOL_NAME}-part1 || fail 12

	${ZFS} destroy ${FULL_NAME} || fail 13
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 14
	${ZFS_SH} -u || fail 15
	rm -f ${TMP_CACHE} || fail 16

	pass
}
run_test 5 "zvol+ext2 volume"

# ZVOL snapshot sanity check
test_6() {
	local POOL_NAME=tank
	local ZVOL_NAME=fish
	local SNAP_NAME=pristine
	local FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raid0 || fail 2
	${ZFS} create -V 800M ${FULL_ZVOL_NAME} || fail 3
	${ZFS} set snapdev=visible ${FULL_ZVOL_NAME} || fail 3
	label /dev/zvol/${FULL_ZVOL_NAME} msdos || fail 4
	partition /dev/zvol/${FULL_ZVOL_NAME} primary 1 -1 || fail 4
	format /dev/zvol/${FULL_ZVOL_NAME}-part1 ext2 || fail 5

	# Mount the ext2 filesystem and copy some data to it.
	mkdir -p /tmp/${ZVOL_NAME}-part1 || fail 6
	mount /dev/zvol/${FULL_ZVOL_NAME}-part1 /tmp/${ZVOL_NAME}-part1 \
		|| fail 7

	# Snapshot the pristine ext2 filesystem and mount it read-only.
	${ZFS} snapshot ${FULL_SNAP_NAME} || fail 8
	wait_udev /dev/zvol/${FULL_SNAP_NAME}-part1 30 || fail 8
	mkdir -p /tmp/${SNAP_NAME}-part1 || fail 9
	mount /dev/zvol/${FULL_SNAP_NAME}-part1 /tmp/${SNAP_NAME}-part1 \
		&>/dev/null || fail 10

	# Copy to original volume
	cp -RL ${SRC_DIR} /tmp/${ZVOL_NAME}-part1 || fail 11
	sync

	# Verify the copied files match the original files,
	# and the copied files do NOT appear in the snapshot.
	diff -ur ${SRC_DIR} /tmp/${ZVOL_NAME}-part1/${SRC_DIR##*/} \
		&>/dev/null || fail 12
	diff -ur ${SRC_DIR} /tmp/${SNAP_NAME}-part1/${SRC_DIR##*/} \
		&>/dev/null && fail 13

	# umount, destroy the snapshot, volume, and pool.
	umount /tmp/${SNAP_NAME}-part1 || fail 14
	rmdir /tmp/${SNAP_NAME}-part1 || fail 15
	${ZFS} destroy ${FULL_SNAP_NAME} || fail 16

	umount /tmp/${ZVOL_NAME}-part1 || fail 17
	rmdir /tmp/${ZVOL_NAME}-part1 || fail 18
	${ZFS} destroy ${FULL_ZVOL_NAME} || fail 19

	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 20
	${ZFS_SH} -u || fail 21
	rm -f ${TMP_CACHE} || fail 22

	pass
}
run_test 6 "zvol+ext2 snapshot"

# ZVOL clone sanity check
test_7() {
	local POOL_NAME=tank
	local ZVOL_NAME=fish
	local SNAP_NAME=pristine
	local CLONE_NAME=clone
	local FULL_ZVOL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local FULL_SNAP_NAME=${POOL_NAME}/${ZVOL_NAME}@${SNAP_NAME}
	local FULL_CLONE_NAME=${POOL_NAME}/${CLONE_NAME}
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 300M ${FULL_ZVOL_NAME} || fail 3
	${ZFS} set snapdev=visible ${FULL_ZVOL_NAME} || fail 3
	label /dev/zvol/${FULL_ZVOL_NAME} msdos || fail 4
	partition /dev/zvol/${FULL_ZVOL_NAME} primary 1 -1 || fail 4
	format /dev/zvol/${FULL_ZVOL_NAME}-part1 ext2 || fail 5

	# Snapshot the pristine ext2 filesystem.
	${ZFS} snapshot ${FULL_SNAP_NAME} || fail 6
	wait_udev /dev/zvol/${FULL_SNAP_NAME}-part1 30 || fail 7

	# Mount the ext2 filesystem so some data can be copied to it.
	mkdir -p /tmp/${ZVOL_NAME}-part1 || fail 7
	mount /dev/zvol/${FULL_ZVOL_NAME}-part1 \
		/tmp/${ZVOL_NAME}-part1 || fail 8

	# Mount the pristine ext2 snapshot.
	mkdir -p /tmp/${SNAP_NAME}-part1 || fail 9
	mount /dev/zvol/${FULL_SNAP_NAME}-part1 \
		/tmp/${SNAP_NAME}-part1 &>/dev/null || fail 10

	# Copy to original volume.
	cp -RL ${SRC_DIR} /tmp/${ZVOL_NAME}-part1 || fail 11
	sync

	# Verify the copied files match the original files,
	# and the copied files do NOT appear in the snapshot.
	diff -ur ${SRC_DIR} /tmp/${ZVOL_NAME}-part1/${SRC_DIR##*/} \
		&>/dev/null || fail 12
	diff -ur ${SRC_DIR} /tmp/${SNAP_NAME}-part1/${SRC_DIR##*/} \
		&>/dev/null && fail 13

	# Clone from the original pristine snapshot
	${ZFS} clone ${FULL_SNAP_NAME} ${FULL_CLONE_NAME} || fail 14
	wait_udev /dev/zvol/${FULL_CLONE_NAME}-part1 30 || fail 14
	mkdir -p /tmp/${CLONE_NAME}-part1 || fail 15
	mount /dev/zvol/${FULL_CLONE_NAME}-part1 \
		/tmp/${CLONE_NAME}-part1 || fail 16

	# Verify the clone matches the pristine snapshot,
	# and the files copied to the original volume are NOT there.
	diff -ur /tmp/${SNAP_NAME}-part1 /tmp/${CLONE_NAME}-part1 \
		&>/dev/null || fail 17
	diff -ur /tmp/${ZVOL_NAME}-part1 /tmp/${CLONE_NAME}-part1 \
		&>/dev/null && fail 18

	# Copy to cloned volume.
	cp -RL ${SRC_DIR} /tmp/${CLONE_NAME}-part1 || fail 19
	sync

	# Verify the clone matches the modified original volume.
	diff -ur /tmp/${ZVOL_NAME}-part1 /tmp/${CLONE_NAME}-part1 \
		&>/dev/null || fail 20

	# umount, destroy the snapshot, volume, and pool.
	umount /tmp/${CLONE_NAME}-part1 || fail 21
	rmdir /tmp/${CLONE_NAME}-part1 || fail 22
	${ZFS} destroy ${FULL_CLONE_NAME} || fail 23

	umount /tmp/${SNAP_NAME}-part1 || fail 24
	rmdir /tmp/${SNAP_NAME}-part1 || fail 25
	${ZFS} destroy ${FULL_SNAP_NAME} || fail 26

	umount /tmp/${ZVOL_NAME}-part1 || fail 27
	rmdir /tmp/${ZVOL_NAME}-part1 || fail 28
	${ZFS} destroy ${FULL_ZVOL_NAME} || fail 29

	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 30
	${ZFS_SH} -u || fail 31
	rm -f ${TMP_CACHE} || fail 32

	pass
}
run_test 7 "zvol+ext2 clone"

# Send/Receive sanity check
test_8() {
	local POOL_NAME1=tank1
	local POOL_NAME2=tank2
	local ZVOL_NAME=fish
	local SNAP_NAME=snap
	local FULL_ZVOL_NAME1=${POOL_NAME1}/${ZVOL_NAME}
	local FULL_ZVOL_NAME2=${POOL_NAME2}/${ZVOL_NAME}
	local FULL_SNAP_NAME1=${POOL_NAME1}/${ZVOL_NAME}@${SNAP_NAME}
	local FULL_SNAP_NAME2=${POOL_NAME2}/${ZVOL_NAME}@${SNAP_NAME}
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	# Create two pools and a volume
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME1} -c lo-raidz2 || fail 2
	${ZPOOL_CREATE_SH} -p ${POOL_NAME2} -c lo-raidz2 || fail 2
	${ZFS} create -V 300M ${FULL_ZVOL_NAME1} || fail 3
	${ZFS} set snapdev=visible ${FULL_ZVOL_NAME1} || fail 3
	label /dev/zvol/${FULL_ZVOL_NAME1} msdos || fail 4
	partition /dev/zvol/${FULL_ZVOL_NAME1} primary 1 -1 || fail 4
	format /dev/zvol/${FULL_ZVOL_NAME1}-part1 ext2 || fail 5

	# Mount the ext2 filesystem and copy some data to it.
	mkdir -p /tmp/${FULL_ZVOL_NAME1}-part1 || fail 6
	mount /dev/zvol/${FULL_ZVOL_NAME1}-part1 \
		/tmp/${FULL_ZVOL_NAME1}-part1 || fail 7
	cp -RL ${SRC_DIR} /tmp/${FULL_ZVOL_NAME1}-part1 || fail 8

	# Unmount, snapshot, mount the ext2 filesystem so it may be sent.
	# We only unmount to ensure the ext2 filesystem is clean.
	umount /tmp/${FULL_ZVOL_NAME1}-part1 || fail 9
	${ZFS} snapshot ${FULL_SNAP_NAME1} || fail 10
	wait_udev /dev/zvol/${FULL_SNAP_NAME1} 30 || fail 10
	mount /dev/zvol/${FULL_ZVOL_NAME1}-part1 \
		/tmp/${FULL_ZVOL_NAME1}-part1 || 11

	# Send/receive the snapshot from POOL_NAME1 to POOL_NAME2
	(${ZFS} send ${FULL_SNAP_NAME1} | \
	${ZFS} receive ${FULL_ZVOL_NAME2}) || fail 12
	wait_udev /dev/zvol/${FULL_ZVOL_NAME2}-part1 30 || fail 12

	# Mount the sent ext2 filesystem.
	mkdir -p /tmp/${FULL_ZVOL_NAME2}-part1 || fail 13
	mount /dev/zvol/${FULL_ZVOL_NAME2}-part1 \
		/tmp/${FULL_ZVOL_NAME2}-part1 || fail 14

	# Verify the contents of the volumes match
	diff -ur /tmp/${FULL_ZVOL_NAME1}-part1 /tmp/${FULL_ZVOL_NAME2}-part1 \
	    &>/dev/null || fail 15

	# Umount, destroy the volume and pool.
	umount /tmp/${FULL_ZVOL_NAME1}-part1 || fail 16
	umount /tmp/${FULL_ZVOL_NAME2}-part1 || fail 17
	rmdir /tmp/${FULL_ZVOL_NAME1}-part1 || fail 18
	rmdir /tmp/${FULL_ZVOL_NAME2}-part1 || fail 19
	rmdir /tmp/${POOL_NAME1} || fail 20
	rmdir /tmp/${POOL_NAME2} || fail 21

	${ZFS} destroy ${FULL_SNAP_NAME1} || fail 22
	${ZFS} destroy ${FULL_SNAP_NAME2} || fail 23
	${ZFS} destroy ${FULL_ZVOL_NAME1} || fail 24
	${ZFS} destroy ${FULL_ZVOL_NAME2} || fail 25
	${ZPOOL_CREATE_SH} -p ${POOL_NAME1} -c lo-raidz2 -d || fail 26
	${ZPOOL_CREATE_SH} -p ${POOL_NAME2} -c lo-raidz2 -d || fail 27
	${ZFS_SH} -u || fail 28
	rm -f ${TMP_CACHE} || fail 29

	pass
}
run_test 8 "zfs send/receive"

# zpool event sanity check
test_9() {
	local POOL_NAME=tank
	local ZVOL_NAME=fish
	local FULL_NAME=${POOL_NAME}/${ZVOL_NAME}
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	local TMP_EVENTS=`mktemp -p /tmp zpool.events.XXXXXXXX`

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 300M ${FULL_NAME} || fail 3
	udev_trigger

	# Dump the events, there should be at least 5 lines.
	${ZPOOL} events >${TMP_EVENTS} || fail 4
	EVENTS=`wc -l ${TMP_EVENTS} | cut -f1 -d' '`
	[ $EVENTS -lt 5 ] && fail 5

	# Clear the events and ensure there are none.
	${ZPOOL} events -c >/dev/null || fail 6
	${ZPOOL} events >${TMP_EVENTS} || fail 7
	EVENTS=`wc -l ${TMP_EVENTS} | cut -f1 -d' '`
	[ $EVENTS -gt 1 ] && fail 8

	${ZFS} destroy ${FULL_NAME} || fail 9
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 10
	${ZFS_SH} -u || fail 11
	rm -f ${TMP_CACHE} || fail 12
	rm -f ${TMP_EVENTS} || fail 13

	pass
}
run_test 9 "zpool events"

zconfig_add_vdev() {
	local POOL_NAME=$1
	local TYPE=$2
	local DEVICE=$3
	local TMP_FILE1=`mktemp`
	local TMP_FILE2=`mktemp`
	local TMP_FILE3=`mktemp`

	BASE_DEVICE=`basename ${DEVICE}`

	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1}
	${ZPOOL} add -f ${POOL_NAME} ${TYPE} ${DEVICE} 2>/dev/null || return 1
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2}
	diff ${TMP_FILE1} ${TMP_FILE2} > ${TMP_FILE3}

	[ `wc -l ${TMP_FILE3}|${AWK} '{print $1}'` -eq 3 ] || return 1

	PARENT_VDEV=`tail -2 ${TMP_FILE3} | head -1 | ${AWK} '{print $NF}'`
	case $TYPE in
	cache)
		[ "${PARENT_VDEV}" = "${TYPE}" ] || return 1
		;;
	log)
		[ "${PARENT_VDEV}" = "logs" ] || return 1
		;;
	esac

	if ! tail -1 ${TMP_FILE3} |
	    egrep -q "^>[[:space:]]+${BASE_DEVICE}[[:space:]]+ONLINE" ; then
		return 1
	fi
	rm -f ${TMP_FILE1} ${TMP_FILE2} ${TMP_FILE3}

	return 0
}

# zpool add and remove sanity check
test_10() {
	local POOL_NAME=tank
	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	local TMP_FILE1=`mktemp`
	local TMP_FILE2=`mktemp`

	if [ ${SCSI_DEBUG} -eq 0 ] || [ ${HAVE_LSSCSI} -eq 0 ] ; then
		skip
		return
	fi

	test `${LSMOD} | grep -c scsi_debug` -gt 0 && \
		(${RMMOD} scsi_debug || exit 1)

	/sbin/modprobe scsi_debug dev_size_mb=128 ||
		die "Error $? creating scsi_debug device"
	udev_trigger

	SDDEVICE=`${LSSCSI}|${AWK} '/scsi_debug/ { print $6; exit }'`
	BASE_SDDEVICE=`basename $SDDEVICE`

	# Create a pool
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1} || fail 3

	# Add and remove a cache vdev by full path
	zconfig_add_vdev ${POOL_NAME} cache ${SDDEVICE} || fail 4
	${ZPOOL} remove ${POOL_NAME} ${SDDEVICE} || fail 5
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 6
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 7
	sleep 1

	# Add and remove a cache vdev by shorthand path
	zconfig_add_vdev ${POOL_NAME} cache ${BASE_SDDEVICE} || fail 8
	${ZPOOL} remove ${POOL_NAME} ${BASE_SDDEVICE} || fail 9
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 10
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 11
	sleep 1

	# Add and remove a log vdev
	zconfig_add_vdev ${POOL_NAME} log ${BASE_SDDEVICE} || fail 12
	${ZPOOL} remove ${POOL_NAME} ${BASE_SDDEVICE} || fail 13
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 14
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 15

	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 16
	${ZFS_SH} -u || fail 17
	${RMMOD} scsi_debug || fail 18

	rm -f ${TMP_FILE1} ${TMP_FILE2} ${TMP_CACHE} || fail 19

	pass
}
run_test 10 "zpool add/remove vdev"

exit 0
