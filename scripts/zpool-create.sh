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

EOF
}

check_config() {

	if [ ! -f ${ZPOOL_CONFIG} ]; then
		local NAME=`basename ${ZPOOL_CONFIG} .cfg`
		ERROR="Unknown config '${NAME}', available configs are:\n"

		for CFG in `ls ${TOPDIR}/scripts/zpool-config/`; do
			local NAME=`basename ${CFG} .cfg`
			ERROR="${ERROR}${NAME}\n"
		done

		return 1
	fi

	return 0
}

ZPOOL_CONFIG=zpool_config.cfg
ZPOOL_NAME=tank
ZPOOL_DESTROY=

while getopts 'hvc:p:d' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	c)
		ZPOOL_CONFIG=${TOPDIR}/scripts/zpool-config/${OPTARG}.cfg
		;;
	p)
		ZPOOL_NAME=${OPTARG}
		;;
	d)
		ZPOOL_DESTROY=1
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

check_config || die "${ERROR}"
. ${ZPOOL_CONFIG}

if [ ${ZPOOL_DESTROY} ]; then
	zpool_destroy
else
	zpool_create
fi

if [ ${VERBOSE} ]; then
	echo
	echo "zpool list"
	${CMDDIR}/zpool/zpool list || exit 1

	echo
	echo "zpool status ${ZPOOL_NAME}"
	${CMDDIR}/zpool/zpool status ${ZPOOL_NAME} || exit 1
fi

exit 0
