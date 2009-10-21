#!/bin/bash
#
# Common support functions for testing scripts.  If a .script-config
# files is available it will be sourced so in-tree kernel modules and
# utilities will be used.  If no .script-config can be found then the
# installed kernel modules and utilities will be used.

SCRIPT_CONFIG=.script-config
if [ -f ../${SCRIPT_CONFIG} ]; then
. ../${SCRIPT_CONFIG}
else
MODULES=(zlib_deflate spl zavl znvpair zunicode zcommon zfs)
fi

PROG="<define PROG>"
VERBOSE=
VERBOSE_FLAG=
FORCE=
FORCE_FLAG=
DUMP_LOG=
ERROR=
UPATH="/dev/disk/zpool"
RAID0S=()
RAID10S=()
RAIDZS=()
RAIDZ2S=()

UDEVDIR=${UDEVDIR:-/usr/libexec/zfs/udev-rules}
ZPOOLDIR=${ZPOOLDIR:-/usr/libexec/zfs/zpool-config}
ZPIOSDIR=${ZPIOSDIR:-/usr/libexec/zfs/zpios-test}
ZPIOSPROFILEDIR=${ZPIOSPROFILEDIR:-/usr/libexec/zfs/zpios-profile}

ZDB=${ZDB:-/usr/sbin/zdb}
ZFS=${ZFS:-/usr/sbin/zfs}
ZINJECT=${ZINJECT:-/usr/sbin/zinject}
ZPOOL=${ZPOOL:-/usr/sbin/zpool}
ZTEST=${ZTEST:-/usr/sbin/ztest}
ZPIOS=${ZPIOS:-/usr/sbin/zpios}

COMMON_SH=${COMMON_SH:-/usr/libexec/zfs/common.sh}
ZFS_SH=${ZFS_SH:-/usr/libexec/zfs/zfs.sh}
ZPOOL_CREATE_SH=${ZPOOL_CREATE_SH:-/usr/libexec/zfs/zpool-create.sh}
ZPIOS_SH=${ZPIOS_SH:-/usr/libexec/zfs/zpios.sh}
ZPIOS_SURVEY_SH=${ZPIOS_SURVEY_SH:-/usr/libexec/zfs/zpios-survey.sh}

LDMOD=${LDMOD:-/sbin/modprobe}
LSMOD=${LSMOD:-/sbin/lsmod}
RMMOD=${RMMOD:-/sbin/rmmod}
INFOMOD=${INFOMOD:-/sbin/modinfo}
LOSETUP=${LOSETUP:-/sbin/losetup}
SYSCTL=${SYSCTL:-/sbin/sysctl}

die() {
	echo -e "${PROG}: $1" >&2
	exit 1
}

msg() {
	if [ ${VERBOSE} ]; then
		echo "$@"
	fi
}

pass() {
	echo "PASS"
}

fail() {
	echo "FAIL ($1)"
	exit $1
}

spl_dump_log() {
	${SYSCTL} -w kernel.spl.debug.dump=1 &>/dev/null
	local NAME=`dmesg | tail -n 1 | cut -f5 -d' '`
	${SPLBUILD}/cmd/spl ${NAME} >${NAME}.log
	echo
	echo "Dumped debug log: ${NAME}.log"
	tail -n1 ${NAME}.log
	echo
	return 0
}

check_modules() {
	local LOADED_MODULES=()
	local MISSING_MODULES=()

	for MOD in ${MODULES[*]}; do
		local NAME=`basename $MOD .ko`

		if ${LSMOD} | egrep -q "^${NAME}"; then
			LOADED_MODULES=(${NAME} ${LOADED_MODULES[*]})
		fi

		if [ ${INFOMOD} ${MOD} 2>/dev/null ]; then
			MISSING_MODULES=("\t${MOD}\n" ${MISSING_MODULES[*]})
		fi
	done

	if [ ${#LOADED_MODULES[*]} -gt 0 ]; then
		ERROR="Unload these modules with '${PROG} -u':\n"
		ERROR="${ERROR}${LOADED_MODULES[*]}"
		return 1
	fi

	if [ ${#MISSING_MODULES[*]} -gt 0 ]; then
		ERROR="The following modules can not be found,"
		ERROR="${ERROR} ensure your source trees are built:\n"
		ERROR="${ERROR}${MISSING_MODULES[*]}"
		return 1
	fi

	return 0
}

load_module() {
	local NAME=`basename $1 .ko`

	if [ ${VERBOSE} ]; then
		echo "Loading ${NAME} ($@)"
	fi

	${LDMOD} $* || ERROR="Failed to load $1" return 1

	return 0
}

load_modules() {
	mkdir -p /etc/zfs

	for MOD in ${MODULES[*]}; do
		local NAME=`basename ${MOD} .ko`
		local VALUE=

		for OPT in "$@"; do
			OPT_NAME=`echo ${OPT} | cut -f1 -d'='`
			
			if [ ${NAME} = "${OPT_NAME}" ]; then
				VALUE=`echo ${OPT} | cut -f2- -d'='`
			fi
		done

		load_module ${MOD} ${VALUE} || return 1
	done

	if [ ${VERBOSE} ]; then
		echo "Successfully loaded ZFS module stack"
	fi

	return 0
}

unload_module() {
	local NAME=`basename $1 .ko`

	if [ ${VERBOSE} ]; then
		echo "Unloading ${NAME} ($@)"
	fi

	${RMMOD} ${NAME} || ERROR="Failed to unload ${NAME}" return 1

	return 0
}

unload_modules() {
	local MODULES_REVERSE=( $(echo ${MODULES[@]} |
		awk '{for (i=NF;i>=1;i--) printf $i" "} END{print ""}') )

	for MOD in ${MODULES_REVERSE[*]}; do
		local NAME=`basename ${MOD} .ko`

		if ${LSMOD} | egrep -q "^${NAME}"; then

			if [ "${DUMP_LOG}" -a ${NAME} = "spl" ]; then
				spl_dump_log
			fi

			unload_module ${MOD} || return 1
		fi
	done

	if [ ${VERBOSE} ]; then
		echo "Successfully unloaded ZFS module stack"
	fi

	return 0
}

unused_loop_device() {
	for DEVICE in `ls -1 /dev/loop*`; do
		${LOSETUP} ${DEVICE} &>/dev/null
		if [ $? -ne 0 ]; then
			echo ${DEVICE}
			return
		fi
	done

	die "Error: Unable to find unused loopback device"
}

#
# The following udev helper functions assume that the provided
# udev rules file will create a /dev/disk/zpool/<CHANNEL><RANK>
# disk mapping.  In this mapping each CHANNEL is represented by
# the letters a-z, and the RANK is represented by the numbers
# 1-n.  A CHANNEL should identify a group of RANKS which are all
# attached to a single controller, each RANK represents a disk.
# This provides a simply mechanism to locate a specific drive
# given a known hardware configuration.
#
udev_setup() {
	local SRC_PATH=$1
	local DST_FILE=`basename ${SRC_PATH} | cut -f1-2 -d'.'`
	local DST_PATH=/etc/udev/rules.d/${DST_FILE}

	cp -f ${SRC_PATH} ${DST_PATH}

	udevadm trigger
	udevadm settle

	return 0
}

udev_cr2d() {
	local CHANNEL=`echo "obase=16; $1+96" | bc`
	local RANK=$2

	printf "\x${CHANNEL}${RANK}"
}

udev_raid0_setup() {
	local RANKS=$1
	local CHANNELS=$2
	local IDX=0

	RAID0S=()
	for RANK in `seq 1 ${RANKS}`; do
		for CHANNEL in `seq 1 ${CHANNELS}`; do
			DISK=`udev_cr2d ${CHANNEL} ${RANK}`
			RAID0S[${IDX}]="${UPATH}/${DISK}"
			let IDX=IDX+1
		done
	done

	return 0
}

udev_raid10_setup() {
	local RANKS=$1
	local CHANNELS=$2
	local IDX=0

	RAID10S=()
	for RANK in `seq 1 ${RANKS}`; do
		for CHANNEL1 in `seq 1 2 ${CHANNELS}`; do
			let CHANNEL2=CHANNEL1+1
			DISK1=`udev_cr2d ${CHANNEL1} ${RANK}`
			DISK2=`udev_cr2d ${CHANNEL2} ${RANK}`
			GROUP="${UPATH}/${DISK1} ${UPATH}/${DISK2}"
			RAID10S[${IDX}]="mirror ${GROUP}"
			let IDX=IDX+1
		done
	done

	return 0
}

udev_raidz_setup() {
	local RANKS=$1
	local CHANNELS=$2
	
	RAIDZS=()
	for RANK in `seq 1 ${RANKS}`; do
		RAIDZ=("raidz")

		for CHANNEL in `seq 1 ${CHANNELS}`; do
			DISK=`udev_cr2d ${CHANNEL} ${RANK}`
			RAIDZ[${CHANNEL}]="${UPATH}/${DISK}"
		done

		RAIDZS[${RANK}]="${RAIDZ[*]}"
	done

	return 0
}

udev_raidz2_setup() {
	local RANKS=$1
	local CHANNELS=$2

	RAIDZ2S=()
	for RANK in `seq 1 ${RANKS}`; do
		RAIDZ2=("raidz2")

		for CHANNEL in `seq 1 ${CHANNELS}`; do
			DISK=`udev_cr2d ${CHANNEL} ${RANK}`
			RAIDZ2[${CHANNEL}]="${UPATH}/${DISK}"
		done

		RAIDZ2S[${RANK}]="${RAIDZ2[*]}"
	done

	return 0
}
