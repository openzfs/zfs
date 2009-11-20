#!/bin/bash
#
# ZFS/ZPOOL configuration test script.

SCRIPT_COMMON=common.sh
if [ -f ./${SCRIPT_COMMON} ]; then
. ./${SCRIPT_COMMON}
elif [ -f /usr/libexec/zfs/${SCRIPT_COMMON} ]; then
. /usr/libexec/zfs/${SCRIPT_COMMON}
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zconfig.sh

usage() {
cat << EOF
USAGE:
$0 [hv]

DESCRIPTION:
	ZFS/ZPOOL configuration tests

OPTIONS:
	-h      Show this message
	-v      Verbose

EOF
}

while getopts 'hv' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
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

# Validate persistent zpool.cache configuration.
zconfig_test1() {
	POOL_NAME=test1
	TMP_FILE1=`mktemp`
	TMP_FILE2=`mktemp`
	TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 1 - persistent zpool.cache: "

	# Create a pool save its status for comparison.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1} || fail 3

	# Unload/load the module stack to clear any configuration state
	# then verify the pool is defined in the cache file, it can be
	# imported without error, and it matches the original pool.
	${ZFS_SH} -u || fail 4
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 5
	${ZPOOL} import -c ${TMP_CACHE} | grep ${POOL_NAME} >/dev/null||fail 6
	${ZPOOL} import -c ${TMP_CACHE} ${POOL_NAME} || fail 7
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 8
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 9

	# Cleanup the test pool and temporary files
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 10
	rm -f ${TMP_FILE1} ${TMP_FILE2} ${TMP_CACHE} || fail 11
	${ZFS_SH} -u || fail 12

	pass
}
zconfig_test1

# Validate ZFS disk scanning and import w/out zpool.cache configuration.
zconfig_test2() {
	POOL_NAME=test2
	TMP_FILE1=`mktemp`
	TMP_FILE2=`mktemp`
	TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 2 - scan disks for pools to import: "

	# Create a pool save its status for comparison.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1} || fail 3

	# Unload/load the module stack to clear any configuration state
	# then remove the cache file, probe the disks for pools, import
	# the pool without error, and match it against the original pool.
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

# ZVOL sanity check
zconfig_test3() {
	POOL_NAME=tank
	ZVOL_NAME=fish
	FULL_NAME=${POOL_NAME}/${ZVOL_NAME}
	SRC_DIR=/bin/
	TMP_FILE1=`mktemp`
	TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`

	echo -n "test 3 - ZVOL sanity: "

	# Create a pool and volume.
	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZFS} create -V 400M ${FULL_NAME} || fail 3

	# Partition the volume, for a 400M volume there will be
	# 812 cylinders, 16 heads, and 63 sectors per track.
	/sbin/sfdisk -q /dev/${FULL_NAME} << EOF &>${TMP_FILE1} || fail 4
,812
;
;
;
EOF

	# Format the partition with ext3.
	/sbin/mkfs.ext3 /dev/${FULL_NAME}1 &>${TMP_FILE1} || fail 5

	# Mount the ext3 filesystem and copy some data to it.
	mkdir -p /tmp/${ZVOL_NAME} || fail 6
	mount /dev/${FULL_NAME}1 /tmp/${ZVOL_NAME} || fail 7
	cp -RL ${SRC_DIR} /tmp/${ZVOL_NAME} || fail 8

	# Verify the copied files match the original files.
	diff -ur ${SRC_DIR} /tmp/${ZVOL_NAME}${SRC_DIR} || fail 9

	# Remove the files, umount, destroy the volume and pool.
	rm -Rf /tmp/${ZVOL_NAME}${SRC_DIR}* || fail 10
	umount /tmp/${ZVOL_NAME} || fail 11
	${ZFS} destroy ${FULL_NAME} || fail 12
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 13
	rm -f ${TMP_FILE1} || fail 14
	${ZFS_SH} -u || fail 15

	pass
}
zconfig_test3

exit 0
