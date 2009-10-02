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

	echo -n "test 1 - persistent zpool.cache: "

	# Create a pool save its status for comparison.
	${ZFS_SH} || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 || fail 2
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE1} || fail 3

	# Unload/load the module stack to clear any configuration state
	# then verify that the pool can be imported and is online.
	${ZFS_SH} -u || fail 4
	${ZFS_SH} || fail 5
	${ZPOOL} import ${POOL_NAME} || fail 6
	${ZPOOL} status ${POOL_NAME} >${TMP_FILE2} || fail 7

	# Compare the original and imported pool status they should match
	cmp ${TMP_FILE1} ${TMP_FILE2} || fail 8

	# Cleanup the test pool and temporary file
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c lo-raidz2 -d || fail 9
	rm -f ${TMP_FILE1} ${TMP_FILE2} || fail 10
	${ZFS_SH} -u || fail 11

	pass
}

zconfig_test1

exit 0
