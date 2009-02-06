#!/bin/bash

. ./common.sh
PROG=zpool-create.sh

usage() {
cat << EOF
USAGE:
$0 [hvcp]

DESCRIPTION:
        Create one of several predefined zpool configurations.

OPTIONS:
        -h      Show this message
        -v      Verbose
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

		for CFG in `ls ${TOPDIR}/scripts/zpool-config/`; do
			local NAME=`basename ${CFG} .sh`
			ERROR="${ERROR}${NAME}\n"
		done

		return 1
	fi

	return 0
}

ZPOOL_CONFIG=zpool_config.sh
ZPOOL_NAME=tank
ZPOOL_DESTROY=
ZPOOL_OPTIONS=""
ZFS_OPTIONS=""

while getopts 'hvc:p:dl:s:' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	c)
		ZPOOL_CONFIG=${TOPDIR}/scripts/zpool-config/${OPTARG}.sh
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
			echo "${CMDDIR}/zpool/zpool ${ZPOOL_OPTIONS} ${ZPOOL_NAME}"
		fi
		${CMDDIR}/zpool/zpool ${ZPOOL_OPTIONS} ${ZPOOL_NAME} || exit 1
	fi

	if [ "${ZFS_OPTIONS}" ]; then
		if [ ${VERBOSE} ]; then
			echo
			echo "${CMDDIR}/zfs/zfs ${ZFS_OPTIONS} ${ZPOOL_NAME}"
		fi
		${CMDDIR}/zfs/zfs ${ZFS_OPTIONS} ${ZPOOL_NAME} || exit 1
	fi

	if [ ${VERBOSE} ]; then
		echo
		echo "zpool list"
		${CMDDIR}/zpool/zpool list || exit 1

		echo
		echo "zpool status ${ZPOOL_NAME}"
		${CMDDIR}/zpool/zpool status ${ZPOOL_NAME} || exit 1
	fi
fi

exit 0
