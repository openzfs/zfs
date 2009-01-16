#!/bin/bash
#
# Wrapper script for easily running zpios based tests
#

. ./common.sh
PROG=zpios.sh

PROFILE_ZPIOS_PRE=${TOPDIR}/scripts/profile-zpios-pre.sh
PROFILE_ZPIOS_POST=${TOPDIR}/scripts/profile-zpios-post.sh

MODULES=(				\
	${MODDIR}/zpios/zpios.ko	\
)

usage() {
cat << EOF
USAGE:
$0 [hvp] [c <config>]

DESCRIPTION:
        Helper script for easy zpios benchmarking.

OPTIONS:
        -h      Show this message
        -v      Verbose
        -p      Enable profiling
        -c     	Specify disk configuration

EOF
}

print_header() {
	echo --------------------- ZPIOS RESULTS ----------------------------
	echo -n "Date: "; date
	echo -n "Kernel: "; uname -r
	dmesg | grep "Loaded Solaris Porting Layer" | tail -n1
	dmesg | grep "Loaded ZFS Filesystem" | tail -n1
	echo
}

print_spl_info() {
	echo --------------------- SPL Tunings ------------------------------
	sysctl -A | grep spl

	if [ -d /sys/module/spl/parameters ]; then
		grep [0-9] /sys/module/spl/parameters/*
	else
		grep [0-9] /sys/module/spl/*
	fi

	echo
}

print_zfs_info() {
	echo --------------------- ZFS Tunings ------------------------------
	sysctl -A | grep zfs

	if [ -d /sys/module/zfs/parameters ]; then
		grep [0-9] /sys/module/zfs/parameters/*
	else
		grep [0-9] /sys/module/zfs/*
	fi

	echo
}

print_stats() {
	echo ---------------------- Statistics -------------------------------
	sysctl -A | grep spl | grep stack_max

	if [ -d /proc/spl/kstat/ ]; then
		if [ -f /proc/spl/kstat/zfs/arcstats ]; then
			echo "* ARC"
			cat /proc/spl/kstat/zfs/arcstats
			echo
		fi

		if [ -f /proc/spl/kstat/zfs/vdev_cache_stats ]; then
			echo "* VDEV Cache"
			cat /proc/spl/kstat/zfs/vdev_cache_stats
			echo
		fi
	fi

	if [ -f /proc/spl/kmem/slab ]; then
		echo "* SPL SLAB"
		cat /proc/spl/kmem/slab
		echo
	fi
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

check_test() {

	if [ ! -f ${ZPIOS_TEST} ]; then
		local NAME=`basename ${ZPIOS_TEST} .cfg`
		ERROR="Unknown test '${NAME}', available tests are:\n"

		for TST in `ls ${TOPDIR}/scripts/zpios-test/`; do
			local NAME=`basename ${TST} .cfg`
			ERROR="${ERROR}${NAME}\n"
		done

		return 1
	fi

	return 0
}

PROFILE=
ZPOOL_CONFIG="zpool-config.cfg"
ZPIOS_TEST="zpios-test.cfg"

while getopts 'hvpc:t:' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	p)
		PROFILE=1
		;;
	c)
		ZPOOL_CONFIG=${TOPDIR}/scripts/zpool-config/${OPTARG}.cfg
		;;
	t)
		ZPIOS_TEST=${TOPDIR}/scripts/zpios-test/${OPTARG}.cfg
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

# Validate your using a known config and test
check_config || die "${ERROR}"
check_test || die "${ERROR}"

# Pull in the zpios test module is not loaded.  If this fails it is
# likely because the full module stack was not yet loaded with zfs.sh
if check_modules; then
	if ! load_modules; then
		die "Run 'zfs.sh' to ensure the full module stack is loaded"
	fi
fi

if [ ${VERBOSE} ]; then
	print_header
	print_spl_info
	print_zfs_info
fi

# Source the zpool configuration
. ${ZPOOL_CONFIG}

msg "${CMDDIR}/zpool/zpool status zpios"
${CMDDIR}/zpool/zpool status zpios || exit 1

msg "Waiting for /dev/zpios to come up..."
while [ ! -c /dev/zpios ]; do
	sleep 1
done

if [ -n "${ZPIOS_PRE}" ]; then
	msg "Executing ${ZPIOS_PRE}"
	${ZPIOS_PRE} || exit 1
fi 

# Source the zpios test configuration
. ${ZPIOS_TEST}

if [ $PROFILE ]; then
	ZPIOS_CMD="${ZPIOS_CMD} --log=${PROFILE_ZPIOS_LOGS}"
	ZPIOS_CMD="${ZPIOS_CMD}	--prerun=${PROFILE_ZPIOS_PRE}"
	ZPIOS_CMD="${ZPIOS_CMD}	--postrun=${PROFILE_ZPIOS_POST}"
fi

echo
date
echo ${ZPIOS_CMD}
$ZPIOS_CMD || exit 1

if [ -n "${ZPIOS_POST}" ]; then
	msg "Executing ${ZPIOS_POST}"
	${ZPIOS_POST} || exit 1
fi 

msg "${CMDDIR}/zpool/zpool destroy zpios"
${CMDDIR}/zpool/zpool destroy zpios

print_stats

exit 0
