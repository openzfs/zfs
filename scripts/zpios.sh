#!/bin/bash
#
# Wrapper script for easily running zpios based tests
#

basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zpios.sh
DATE=`date +%Y%m%d-%H%M%S`
if [ "${ZPIOS_MODULES}" ]; then
	MODULES=(${ZPIOS_MODULES[*]})
else
	MODULES=(zpios)
fi

usage() {
cat << EOF
USAGE:
$0 [hvp] <-c config> <-t test>

DESCRIPTION:
        Helper script for easy zpios benchmarking.

OPTIONS:
        -h      Show this message
        -v      Verbose
        -f      Force everything
        -p      Enable profiling
        -c      Zpool configuration
        -t      Zpios test
        -o      Additional zpios options
        -l      Additional zpool options
        -s      Additional zfs options

EOF
}

unload_die() {
	unload_modules
	while [ -c /dev/zpios ]; do
		sleep 1
	done

	exit 1
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
	${SYSCTL} -A | grep spl

	if [ -d /sys/module/spl/parameters ]; then
		grep [0-9] /sys/module/spl/parameters/*
	else
		grep [0-9] /sys/module/spl/*
	fi

	echo
}

print_zfs_info() {
	echo --------------------- ZFS Tunings ------------------------------
	${SYSCTL} -A | grep zfs

	if [ -d /sys/module/zfs/parameters ]; then
		grep [0-9] /sys/module/zfs/parameters/*
	else
		grep [0-9] /sys/module/zfs/*
	fi

	echo
}

print_stats() {
	echo ---------------------- Statistics -------------------------------
	${SYSCTL} -A | grep spl | grep stack_max

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

	echo
}

check_test() {

	if [ ! -f ${ZPIOS_TEST} ]; then
		local NAME=`basename ${ZPIOS_TEST} .sh`
		ERROR="Unknown test '${NAME}', available tests are:\n"

		for TST in `ls ${ZPIOSDIR}/ | grep ".sh"`; do
			local NAME=`basename ${TST} .sh`
			ERROR="${ERROR}${NAME}\n"
		done

		return 1
	fi

	return 0
}

zpios_profile_config() {
cat > ${PROFILE_DIR}/zpios-config.sh << EOF
#
# Zpios Profiling Configuration
#

PROFILE_DIR=/tmp/zpios/${ZPOOL_CONFIG}+${ZPIOS_TEST_ARG}+${DATE}
PROFILE_PRE=${ZPIOSPROFILEDIR}/zpios-profile-pre.sh
PROFILE_POST=${ZPIOSPROFILEDIR}/zpios-profile-post.sh
PROFILE_USER=${ZPIOSPROFILEDIR}/zpios-profile.sh
PROFILE_PIDS=${ZPIOSPROFILEDIR}/zpios-profile-pids.sh
PROFILE_DISK=${ZPIOSPROFILEDIR}/zpios-profile-disk.sh
PROFILE_ARC_PROC=/proc/spl/kstat/zfs/arcstats
PROFILE_VDEV_CACHE_PROC=/proc/spl/kstat/zfs/vdev_cache_stats

OPROFILE_KERNEL="/boot/vmlinux-`uname -r`"
OPROFILE_KERNEL_DIR="/lib/modules/`uname -r`/kernel/"
OPROFILE_SPL_DIR=${SPLBUILD}/module/
OPROFILE_ZFS_DIR=${MODDIR}

EOF
}

zpios_profile_start() {
	PROFILE_DIR=/tmp/zpios/${ZPOOL_CONFIG}+${ZPIOS_TEST_ARG}+${DATE}

	mkdir -p ${PROFILE_DIR}
	zpios_profile_config
	. ${PROFILE_DIR}/zpios-config.sh

	ZPIOS_OPTIONS="${ZPIOS_OPTIONS} --log=${PROFILE_DIR}"
	ZPIOS_OPTIONS="${ZPIOS_OPTIONS} --prerun=${PROFILE_PRE}"
	ZPIOS_OPTIONS="${ZPIOS_OPTIONS} --postrun=${PROFILE_POST}"

	/usr/bin/opcontrol --init
	/usr/bin/opcontrol --setup --vmlinux=${OPROFILE_KERNEL}
}

zpios_profile_stop() {
	/usr/bin/opcontrol --shutdown
	/usr/bin/opcontrol --deinit
}

PROFILE=
ZPOOL_CONFIG=zpool-config.sh
ZPIOS_TEST=zpios-test.sh
ZPOOL_NAME=zpios
ZPIOS_OPTIONS=
ZPOOL_OPTIONS=""
ZFS_OPTIONS=""

while getopts 'hvfpc:t:o:l:s:' OPTION; do
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
		FORCE_FLAG="-f"
		;;
	p)
		PROFILE=1
		;;
	c)
		ZPOOL_CONFIG=${OPTARG}
		;;
	t)
		ZPIOS_TEST_ARG=${OPTARG}
		ZPIOS_TEST=${ZPIOSDIR}/${OPTARG}.sh
		;;
	o)
		ZPIOS_OPTIONS=${OPTARG}
		;;
	l)	# Passed through to zpool-create.sh 
		ZPOOL_OPTIONS=${OPTARG}
		;;
	s)	# Passed through to zpool-create.sh
		ZFS_OPTIONS=${OPTARG}
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

# Validate and source your test config
check_test || die "${ERROR}"
. ${ZPIOS_TEST}

# Pull in the zpios test module if not loaded.  If this fails, it is
# likely because the full module stack was not yet loaded with zfs.sh
if check_modules; then
	if ! load_modules; then
		die "Run 'zfs.sh' to ensure the full module stack is loaded"
	fi
fi

# Wait for device creation
while [ ! -c /dev/zpios ]; do
	sleep 1
done

if [ ${VERBOSE} ]; then
	print_header
	print_spl_info
	print_zfs_info
fi

# Create the zpool configuration
${ZPOOL_CREATE_SH} ${VERBOSE_FLAG} ${FORCE_FLAG} \
	-p ${ZPOOL_NAME} -c ${ZPOOL_CONFIG} \
	-l "${ZPOOL_OPTIONS}" -s "${ZFS_OPTIONS}" || unload_die

if [ ${PROFILE} ]; then
	zpios_profile_start
fi

zpios_start
zpios_stop

if [ ${PROFILE} ]; then
	zpios_profile_stop
fi

if [ ${VERBOSE} ]; then
	print_stats
fi

# Destroy the zpool configuration
${ZPOOL_CREATE_SH} ${VERBOSE_FLAG} ${FORCE_FLAG} \
	-p ${ZPOOL_NAME} -c ${ZPOOL_CONFIG} -d || unload_die

# Unload the test module stack and wait for device removal
unload_modules
while [ -c /dev/zpios ]; do
	sleep 1
done

exit 0
