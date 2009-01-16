#!/bin/bash

. ./common.sh
PROG=create-zpool.sh

usage() {
cat << EOF
USAGE:
$0 [hvcp]

DESCRIPTION:
        Create one of several predefined zpool configurations.

OPTIONS:
        -h      Show this message
        -v      Verbose
        -c      Zpool configuration
        -p      Zpool name

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

while getopts 'hvc:p:' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	c)
		ZPOOL_CONFIG=${TOPDIR}/scripts/zpool-config/${OPTARG}
		;;
	p)
		ZPOOL_NAME=${OPTARG}
		;;
	?)
		usage
		exit
		;;
	esac
done

check_config || die "${ERROR}"
. ${ZPOOL_CONFIG}

if [ ${VERBOSE} ]; then
	echo
	echo "zpool list"
	${CMDDIR}/zpool/zpool list || exit 1

	echo
	echo "zpool status ${ZPOOL_NAME}"
	${CMDDIR}/zpool/zpool status ${ZPOOL_NAME} || exit 1
}

exit 0
