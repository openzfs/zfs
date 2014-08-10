#!/bin/bash

basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zpool-create.sh

usage() {
cat << EOF
USAGE:
$0 [hvfxcp]

DESCRIPTION:
        Create one of several predefined zpool configurations.

OPTIONS:
        -h      Show this message
        -v      Verbose
        -f      Force everything
        -x      Disable all zpool features
        -c      Configuration for zpool
        -p      Name for zpool
        -d      Destroy zpool (default create)
        -l      Additional zpool options
        -s      Additional zfs options

EOF
}

check_config() {

	if [ ! -f ${ZPOOL_CONFIG} ]; then
		local NAME=`basename ${ZPOOL_CONFIG} .sh`
		ERROR="Unknown config '${NAME}', available configs are:\n"

		for CFG in `ls ${ZPOOLDIR}/ | grep ".sh"`; do
			local NAME=`basename ${CFG} .sh`
			ERROR="${ERROR}${NAME}\n"
		done

		return 1
	fi

	return 0
}

ZPOOL_CONFIG=unknown
ZPOOL_NAME=tank
ZPOOL_DESTROY=
ZPOOL_FLAGS=${ZPOOL_FLAGS:-""}
ZPOOL_OPTIONS=""
ZFS_OPTIONS=""

while getopts 'hvfxc:p:dl:s:' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		VERBOSE_FLAG="-v"
		;;
	f)
		FORCE=1
		ZPOOL_FLAGS="$ZPOOL_FLAGS -f"
		;;
	x)
		NO_FEATURES=1
		ZPOOL_FLAGS="$ZPOOL_FLAGS -d"
		;;
	c)
		ZPOOL_CONFIG=${ZPOOLDIR}/${OPTARG}.sh
		;;
	p)
		ZPOOL_NAME=${OPTARG}
		;;
	d)
		ZPOOL_DESTROY=1
		;;
	l)
		ZPOOL_OPTIONS=${OPTARG}
		;;
	s)
		ZFS_OPTIONS=${OPTARG}
		;;
	?)
		usage
		exit 1
		;;
	esac
done

if [ $(id -u) != 0 ]; then
        die "Must run as root"
fi

check_config || die "${ERROR}"
. ${ZPOOL_CONFIG}

if [ ${ZPOOL_DESTROY} ]; then
	zpool_destroy
else
	zpool_create

	if [ "${ZPOOL_OPTIONS}" ]; then
		if [ ${VERBOSE} ]; then
			echo
			echo "${ZPOOL} ${ZPOOL_OPTIONS} ${ZPOOL_NAME}"
		fi
		${ZPOOL} ${ZPOOL_OPTIONS} ${ZPOOL_NAME} || exit 1
	fi

	if [ "${ZFS_OPTIONS}" ]; then
		if [ ${VERBOSE} ]; then
			echo
			echo "${ZFS} ${ZFS_OPTIONS} ${ZPOOL_NAME}"
		fi
		${ZFS} ${ZFS_OPTIONS} ${ZPOOL_NAME} || exit 1
	fi

	if [ ${VERBOSE} ]; then
		echo
		echo "zpool list"
		${ZPOOL} list || exit 1

		echo
		echo "zpool status ${ZPOOL_NAME}"
		${ZPOOL} status ${ZPOOL_NAME} || exit 1
	fi
fi

exit 0
