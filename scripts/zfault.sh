#!/bin/bash
#
# ZPOOL fault verification test script.
#
# The current suite of fault tests should not be thought of an exhaustive
# list of failure modes.  Rather it is simply an starting point which trys
# to cover the bulk the of the 'easy' and hopefully common, failure modes.
#
# Additional tests should be added but the current suite as new interesting
# failures modes are observed.  Additional failure modes I'd like to see
# tests for include, but are not limited too:
#
#	* Slow but successful IO.
#	* SCSI sense codes generated as zevents.
#	* 4k sectors
#	* noise
#	* medium error
#	* recovered error
#
# The current infrastructure using the 'mdadm' faulty device and the
# 'scsi_debug' simulated scsi devices.  The idea is to inject the error
# below the zfs stack to validate all the error paths.  More targeted
# failure testing should be added using the 'zinject' command line util.
#
# Requires the following packages:
# * mdadm
# * lsscsi
# * sg3-utils
#

basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zfault.sh

usage() {
cat << EOF
USAGE:
$0 [hvcts]

DESCRIPTION:
	ZPOOL fault verification tests

OPTIONS:
	-h      Show this message
	-v      Verbose
	-c      Cleanup md+lo+file devices at start
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

# Check if we need to skip all md based tests.
MD_PARTITIONABLE=0
check_md_partitionable && MD_PARTITIONABLE=1
if [ ${MD_PARTITIONABLE} -eq 0 ]; then
	echo "Skipping tests 1-7 which require partitionable md devices"
fi

# Check if we need to skip all the scsi_debug tests.
SCSI_DEBUG=0
${INFOMOD} scsi_debug &>/dev/null && SCSI_DEBUG=1
if [ ${SCSI_DEBUG} -eq 0 ]; then
	echo "Skipping tests 8-9 which require the scsi_debug module"
fi

if [ ${MD_PARTITIONABLE} -eq 0 ] || [ ${SCSI_DEBUG} -eq 0 ]; then
	echo
fi

printf "%40s%s\t%s\t%s\t%s\t%s\n" "" "raid0" "raid10" "raidz" "raidz2" "raidz3"

pass_nonewline() {
	echo -n -e "${COLOR_GREEN}Pass${COLOR_RESET}\t"
}

skip_nonewline() {
	echo -n -e "${COLOR_BROWN}Skip${COLOR_RESET}\t"
}

nth_zpool_vdev() {
	local POOL_NAME=$1
	local DEVICE_TYPE=$2
	local DEVICE_NTH=$3

	${ZPOOL} status ${POOL_NAME} | grep ${DEVICE_TYPE} ${TMP_STATUS} |   \
		head -n${DEVICE_NTH} | tail -n1 | ${AWK} "{ print \$1 }"
}

vdev_status() {
	local POOL_NAME=$1
	local VDEV_NAME=$2

	${ZPOOL} status ${POOL_NAME} | ${AWK} "/${VDEV_NAME}/ { print \$2 }"
}

# Required format is x.yz[KMGTP]
expand_numeric_suffix() {
	local VALUE=$1

	VALUE=`echo "${VALUE/%K/*1000}"`
	VALUE=`echo "${VALUE/%M/*1000000}"`
	VALUE=`echo "${VALUE/%G/*1000000000}"`
	VALUE=`echo "${VALUE/%T/*1000000000000}"`
	VALUE=`echo "${VALUE/%P/*1000000000000000}"`
	VALUE=`echo "${VALUE}" | bc | cut -d'.' -f1`

	echo "${VALUE}"
}

vdev_read_errors() {
	local POOL_NAME=$1
	local VDEV_NAME=$2
	local VDEV_ERRORS=`${ZPOOL} status ${POOL_NAME} |
		${AWK} "/${VDEV_NAME}/ { print \\$3 }"`

	expand_numeric_suffix ${VDEV_ERRORS}
}

vdev_write_errors() {
	local POOL_NAME=$1
	local VDEV_NAME=$2
	local VDEV_ERRORS=`${ZPOOL} status ${POOL_NAME} |
		${AWK} "/${VDEV_NAME}/ { print \\$4 }"`

	expand_numeric_suffix ${VDEV_ERRORS}
}

vdev_cksum_errors() {
	local POOL_NAME=$1
	local VDEV_NAME=$2
	local VDEV_ERRORS=`${ZPOOL} status ${POOL_NAME} |
		${AWK} "/${VDEV_NAME}/ { print \\$5 }"`

	expand_numeric_suffix ${VDEV_ERRORS}
}

zpool_state() {
	local POOL_NAME=$1

	${ZPOOL} status ${POOL_NAME} | ${AWK} "/state/ { print \$2; exit }"
}

zpool_event() {
	local EVENT_NAME=$1
	local EVENT_KEY=$2

	SCRIPT1="BEGIN {RS=\"\"; FS=\"\n\"} /${EVENT_NAME}/ { print \$0; exit }"
	SCRIPT2="BEGIN {FS=\"=\"} /${EVENT_KEY}/ { print \$2; exit }"

	${ZPOOL} events -vH | ${AWK} "${SCRIPT1}" | ${AWK} "${SCRIPT2}"
}

zpool_scan_errors() {
	local POOL_NAME=$1

	${ZPOOL} status ${POOL_NAME} | ${AWK} "/scan: scrub/ { print \$8 }"
	${ZPOOL} status ${POOL_NAME} | ${AWK} "/scan: resilver/ { print \$7 }"
}

pattern_create() {
	local PATTERN_BLOCK_SIZE=$1
	local PATTERN_BLOCK_COUNT=$2
	local PATTERN_NAME=`mktemp -p /tmp zpool.pattern.XXXXXXXX`

	echo ${PATTERN_NAME}
	dd if=/dev/urandom of=${PATTERN_NAME} bs=${PATTERN_BLOCK_SIZE}   \
		count=${PATTERN_BLOCK_COUNT} &>/dev/null
	return $?
}

pattern_write() {
	local PATTERN_NAME=$1
	local PATTERN_BLOCK_SIZE=$2
	local PATTERN_BLOCK_COUNT=$3
	local DEVICE_NAME=$4

	dd if=${PATTERN_NAME} of=${DEVICE_NAME} bs=${PATTERN_BLOCK_SIZE} \
		count=${PATTERN_BLOCK_COUNT} oflag=direct &>/dev/null
	return $?
}

pattern_write_bg() {
	local PATTERN_NAME=$1
	local PATTERN_BLOCK_SIZE=$2
	local PATTERN_BLOCK_COUNT=$3
	local DEVICE_NAME=$4

	dd if=${PATTERN_NAME} of=${DEVICE_NAME} bs=${PATTERN_BLOCK_SIZE} \
		count=${PATTERN_BLOCK_COUNT} oflag=direct &>/dev/null &
	return $?
}

pattern_verify() {
	local PATTERN_NAME=$1
	local PATTERN_BLOCK_SIZE=$2
	local PATTERN_BLOCK_COUNT=$3
	local DEVICE_NAME=$4
	local DEVICE_FILE=`mktemp -p /tmp zpool.pattern.XXXXXXXX`

	dd if=${DEVICE_NAME} of=${DEVICE_FILE} bs=${PATTERN_BLOCK_SIZE} \
		count=${PATTERN_BLOCK_COUNT} iflag=direct &>/dev/null
	cmp -s ${PATTERN_NAME} ${DEVICE_FILE}
	RC=$?
	rm -f ${DEVICE_FILE}

	return ${RC}
}

pattern_remove() {
	local PATTERN_NAME=$1

	rm -f ${PATTERN_NAME}
	return $?
}

fault_set_md() {
	local VDEV_FAULTY=$1
	local FAULT_TYPE=$2

	${MDADM} /dev/${VDEV_FAULTY} --grow --level=faulty \
		--layout=${FAULT_TYPE} >/dev/null
	return $?
}

fault_clear_md() {
	local VDEV_FAULTY=$1

	# Clear all failure injection.
	${MDADM} /dev/${VDEV_FAULTY} --grow --level=faulty \
		--layout=clear >/dev/null || return $?
	${MDADM} /dev/${VDEV_FAULTY} --grow --level=faulty \
		--layout=flush >/dev/null || return $?
	return $?
}

fault_set_sd() {
	local OPTS=$1
	local NTH=$2

	echo ${OPTS} >/sys/bus/pseudo/drivers/scsi_debug/opts
	echo ${NTH}  >/sys/bus/pseudo/drivers/scsi_debug/every_nth
}

fault_clear_sd() {
	echo 0 >/sys/bus/pseudo/drivers/scsi_debug/every_nth
	echo 0 >/sys/bus/pseudo/drivers/scsi_debug/opts
}

test_setup() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local ZVOL_NAME=$3
	local TMP_CACHE=$4

	${ZFS_SH} zfs="spa_config_path=${TMP_CACHE}" || fail 1
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c ${POOL_CONFIG} || fail 2
	${ZFS} create -V 64M ${POOL_NAME}/${ZVOL_NAME} || fail 3

	# Trigger udev and re-read the partition table to ensure all of
	# this IO is out of the way before we begin injecting failures.
	udev_trigger || fail 4
	${BLOCKDEV} --rereadpt /dev/${POOL_NAME}/${ZVOL_NAME} || fail 5
}

test_cleanup() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local ZVOL_NAME=$3
	local TMP_CACHE=$4

	${ZFS} destroy ${POOL_NAME}/${ZVOL_NAME} || fail 101
	${ZPOOL_CREATE_SH} -p ${POOL_NAME} -c ${POOL_CONFIG} -d || fail 102
	${ZFS_SH} -u || fail 103
	rm -f ${TMP_CACHE} || fail 104
}

test_write_soft() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"

	if [ ${MD_PARTITIONABLE} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Set soft write failure for first vdev device.
	local VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md 1`
	fault_set_md ${VDEV_FAULTY} write-transient

	# The application must not observe an error.
	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12
	fault_clear_md ${VDEV_FAULTY}

	# Soft errors will not be logged to 'zpool status'
	local WRITE_ERRORS=`vdev_write_errors ${POOL_NAME} ${VDEV_FAULTY}`
	test ${WRITE_ERRORS} -eq 0 || fail 13

	# Soft errors will still generate an EIO (5) event.
	test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 14

	# Verify the known pattern.
	pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 15
	pattern_remove ${TMP_PATTERN} || fail 16

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

# Soft write error.
test_1() {
	test_write_soft tank lo-faulty-raid0  0
	test_write_soft tank lo-faulty-raid10 1
	test_write_soft tank lo-faulty-raidz  1
	test_write_soft tank lo-faulty-raidz2 1
	test_write_soft tank lo-faulty-raidz3 1
	echo
}
run_test 1 "soft write error"

test_write_hard() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"

	if [ ${MD_PARTITIONABLE} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Set hard write failure for first vdev device.
	local VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md 1`
	fault_set_md ${VDEV_FAULTY} write-persistent

	# The application must not observe an error.
	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12
	fault_clear_md ${VDEV_FAULTY}

	local WRITE_ERRORS=`vdev_write_errors ${POOL_NAME} ${VDEV_FAULTY}`
	if [ ${POOL_REDUNDANT} -eq 1 ]; then
		# For redundant configurations hard errors will not be
		# logged to 'zpool status' but will generate EIO events.
		test ${WRITE_ERRORS} -eq 0 || fail 21
		test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 22
	else
		# For non-redundant configurations hard errors will be
		# logged to 'zpool status' and generate EIO events.  They
		# will also trigger a scrub of the impacted sectors.
		sleep 10
		test ${WRITE_ERRORS} -gt 0 || fail 31
		test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 32
		test `zpool_event "zfs.resilver.start" "ena"` != "" || fail 33
		test `zpool_event "zfs.resilver.finish" "ena"` != "" || fail 34
		test `zpool_scan_errors ${POOL_NAME}` -eq 0 || fail 35
	fi

	# Verify the known pattern.
	pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 41
	pattern_remove ${TMP_PATTERN} || fail 42

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

# Hard write error.
test_2() {
	test_write_hard tank lo-faulty-raid0  0
	test_write_hard tank lo-faulty-raid10 1
	test_write_hard tank lo-faulty-raidz  1
	test_write_hard tank lo-faulty-raidz2 1
	test_write_hard tank lo-faulty-raidz3 1
	echo
}
run_test 2 "hard write error"

test_write_all() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"

	if [ ${MD_PARTITIONABLE} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Set all write failures for first vdev device.
	local VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md 1`
	fault_set_md ${VDEV_FAULTY} write-all

	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	if [ ${POOL_REDUNDANT} -eq 1 ]; then
		# The application must not observe an error.
		pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12
	else
		# The application is expected to hang in the background until
		# the faulty device is repaired and 'zpool clear' is run.
		pattern_write_bg ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 13
		sleep 10
	fi
	fault_clear_md ${VDEV_FAULTY}

	local WRITE_ERRORS=`vdev_write_errors ${POOL_NAME} ${VDEV_FAULTY}`
	local VDEV_STATUS=`vdev_status ${POOL_NAME} ${VDEV_FAULTY}`
	local POOL_STATE=`zpool_state ${POOL_NAME}`
	# For all configurations write errors are logged to 'zpool status',
	# and EIO events are generated.  However, only a redundant config
	# will cause the vdev to be FAULTED and pool DEGRADED.  In a non-
	# redundant config the IO will hang until 'zpool clear' is run.
	test ${WRITE_ERRORS} -gt 0 || fail 14
	test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 15

	if [ ${POOL_REDUNDANT} -eq 1 ]; then
		test "${VDEV_STATUS}" = "FAULTED" || fail 21
		test "${POOL_STATE}" = "DEGRADED" || fail 22
	else
		BLOCKED=`ps a | grep "${ZVOL_DEVICE}" | grep -c -v "grep"`
		${ZPOOL} clear  ${POOL_NAME} || fail 31
		test ${BLOCKED} -eq 1 || fail 32
		wait
	fi

	# Verify the known pattern.
	pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 41
	pattern_remove ${TMP_PATTERN} || fail 42

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

# All write errors.
test_3() {
	test_write_all tank lo-faulty-raid0  0
	test_write_all tank lo-faulty-raid10 1
	test_write_all tank lo-faulty-raidz  1
	test_write_all tank lo-faulty-raidz2 1
	test_write_all tank lo-faulty-raidz3 1
	echo
}
run_test 3 "all write errors"

test_read_soft() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"
	local READ_ERRORS=0

	if [ ${MD_PARTITIONABLE} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Create a pattern to be verified during a read error.
	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12

	# Set soft read failure for all the vdevs to ensure we hit it.
	for (( i=1; i<=4; i++ )); do
		fault_set_md `nth_zpool_vdev ${POOL_NAME} md $i` read-transient
	done

	pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 13
	pattern_remove ${TMP_PATTERN} || fail 14

	# Clear all failure injection and sum read errors.
	for (( i=1; i<=4; i++ )); do
		local VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md $i`
		local VDEV_ERRORS=`vdev_read_errors ${POOL_NAME} ${VDEV_FAULTY}`
		let READ_ERRORS=${READ_ERRORS}+${VDEV_ERRORS}
		fault_clear_md ${VDEV_FAULTY}
	done

	# Soft errors will not be logged to 'zpool status'.
	test ${READ_ERRORS} -eq 0 || fail 15

	# Soft errors will still generate an EIO (5) event.
	test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 16

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

# Soft read error.
test_4() {
	test_read_soft tank lo-faulty-raid0  0
	test_read_soft tank lo-faulty-raid10 1
	test_read_soft tank lo-faulty-raidz  1
	test_read_soft tank lo-faulty-raidz2 1
	test_read_soft tank lo-faulty-raidz3 1
	echo
}
run_test 4 "soft read error"

test_read_hard() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"
	local READ_ERRORS=0

	if [ ${MD_PARTITIONABLE} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Create a pattern to be verified during a read error.
	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12

	# Set hard read failure for the fourth vdev.
	local VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md 4`
	fault_set_md ${VDEV_FAULTY} read-persistent

	# For a redundant pool there must be no IO error, for a non-redundant
	# pool we expect permanent damage and an IO error during verify, unless
	# we get exceptionally lucky and have just damaged redundant metadata.
	if [ ${POOL_REDUNDANT} -eq 1 ]; then
		pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 21
		local READ_ERRORS=`vdev_read_errors ${POOL_NAME} ${VDEV_FAULTY}`
		test ${READ_ERRORS} -eq 0 || fail 22
	else
		pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE}
		${ZPOOL} scrub ${POOL_NAME} || fail 32
		local READ_ERRORS=`vdev_read_errors ${POOL_NAME} ${VDEV_FAULTY}`
		test ${READ_ERRORS} -gt 0 || fail 33
		${ZPOOL} status -v ${POOL_NAME} |     \
			grep -A8 "Permanent errors" | \
			grep -q "${POOL_NAME}" || fail 34
	fi
	pattern_remove ${TMP_PATTERN} || fail 41

	# Clear all failure injection and sum read errors.
	fault_clear_md ${VDEV_FAULTY}

	# Hard errors will generate an EIO (5) event.
	test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 42

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

# Hard read error.
test_5() {
	test_read_hard tank lo-faulty-raid0  0
	test_read_hard tank lo-faulty-raid10 1
	test_read_hard tank lo-faulty-raidz  1
	test_read_hard tank lo-faulty-raidz2 1
	test_read_hard tank lo-faulty-raidz3 1
	echo
}
run_test 5 "hard read error"

# Fixable read error.
test_read_fixable() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"
	local READ_ERRORS=0

	if [ ${MD_PARTITIONABLE} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Create a pattern to be verified during a read error.
	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12

	# Set hard read failure for the fourth vdev.
	local VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md 4`
	fault_set_md ${VDEV_FAULTY} read-fixable

	# For a redundant pool there must be no IO error, for a non-redundant
	# pool we expect permanent damage and an IO error during verify, unless
	# we get exceptionally lucky and have just damaged redundant metadata.
	if [ ${POOL_REDUNDANT} -eq 1 ]; then
		pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 21
		local READ_ERRORS=`vdev_read_errors ${POOL_NAME} ${VDEV_FAULTY}`
		test ${READ_ERRORS} -eq 0 || fail 22
	else
		pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE}
		${ZPOOL} scrub ${POOL_NAME} || fail 32
		local READ_ERRORS=`vdev_read_errors ${POOL_NAME} ${VDEV_FAULTY}`
		test ${READ_ERRORS} -gt 0 || fail 33
		${ZPOOL} status -v ${POOL_NAME} |     \
			grep -A8 "Permanent errors" | \
			grep -q "${POOL_NAME}" || fail 34
	fi
	pattern_remove ${TMP_PATTERN} || fail 41

	# Clear all failure injection and sum read errors.
	fault_clear_md ${VDEV_FAULTY}

	# Hard errors will generate an EIO (5) event.
	test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 42

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

# Read errors fixable with a write.
test_6() {
	test_read_fixable tank lo-faulty-raid0  0
	test_read_fixable tank lo-faulty-raid10 1
	test_read_fixable tank lo-faulty-raidz  1
	test_read_fixable tank lo-faulty-raidz2 1
	test_read_fixable tank lo-faulty-raidz3 1
	echo
}
run_test 6 "fixable read error"

test_cksum() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local VDEV_DAMAGE="$4"
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"

	if [ ${MD_PARTITIONABLE} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Create a pattern to be verified.
	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12

	# Verify the pattern and that no vdev has cksum errors.
	pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 13
	for (( i=1; i<4; i++ )); do
		VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md ${i}`
		CKSUM_ERRORS=`vdev_cksum_errors ${POOL_NAME} ${VDEV_FAULTY}`
		test ${CKSUM_ERRORS} -eq 0 || fail 14
	done

	# Corrupt the bulk of a vdev with random garbage, we damage as many
	# vdevs as we have levels of redundancy.  For example for a raidz3
	# configuration we can trash 3 vdevs and still expect correct data.
	# This improves the odds that we read one of the damaged vdevs.
	for VDEV in ${VDEV_DAMAGE}; do
		VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} md $VDEV`
		pattern_write /dev/urandom 1M 64 /dev/${VDEV_FAULTY}p1
	done

	# Verify the pattern is still correct.  For non-redundant pools
	# expect failure and for redundant pools success due to resilvering.
	if [ ${POOL_REDUNDANT} -eq 1 ]; then
		pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 16
	else
		pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} && fail 17
	fi

	CKSUM_ERRORS=`vdev_cksum_errors ${POOL_NAME} ${VDEV_FAULTY}`
	test ${CKSUM_ERRORS} -gt 0 || fail 18
	STATUS=`vdev_status ${POOL_NAME} ${VDEV_FAULTY}`
	test "${STATUS}" = "ONLINE" || fail 19

	# The checksum errors must be logged as an event.
	local CKSUM_ERRORS=`zpool_event "zfs.checksum" "zio_err"`
	test ${CKSUM_ERRORS} = "0x34" || test ${CKSUM_ERRORS} = "0x0" || fail 20

	# Verify permant errors for non-redundant pools, and for redundant
	# pools trigger a scrub and check that all checksums have been fixed.
	if [ ${POOL_REDUNDANT} -eq 1 ]; then
		# Scrub the checksum errors and clear the faults.
		${ZPOOL} scrub ${POOL_NAME} || fail 21
		sleep 3
		${ZPOOL} clear ${POOL_NAME} || fail 22

		# Re-verify the pattern for fixed checksums.
		pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 23
		CKSUM_ERRORS=`vdev_cksum_errors ${POOL_NAME} ${VDEV_FAULTY}`
		test ${CKSUM_ERRORS} -eq 0 || fail 24

		# Re-verify the entire pool for fixed checksums.
		${ZPOOL} scrub ${POOL_NAME} || fail 25
		CKSUM_ERRORS=`vdev_cksum_errors ${POOL_NAME} ${VDEV_FAULTY}`
		test ${CKSUM_ERRORS} -eq 0 || fail 26
	else
		${ZPOOL} status -v ${POOL_NAME} |     \
			grep -A8 "Permanent errors" | \
			grep -q "${POOL_NAME}/${ZVOL_NAME}" || fail 31
		${ZPOOL} clear ${POOL_NAME} || fail 32
	fi
	pattern_remove ${TMP_PATTERN} || fail 41

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

# Silent data corruption
test_7() {
	test_cksum tank lo-faulty-raid0  0 "1"
	test_cksum tank lo-faulty-raid10 1 "1 3"
	test_cksum tank lo-faulty-raidz  1 "4"
	test_cksum tank lo-faulty-raidz2 1 "3 4"
	test_cksum tank lo-faulty-raidz3 1 "2 3 4"
	echo
}
run_test 7 "silent data corruption"

# Soft write timeout at the scsi device layer.
test_write_timeout_soft() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local POOL_NTH=$4
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"

	if [ ${SCSI_DEBUG} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	# Set timeout(0x4) for every nth command.
	fault_set_sd  4 ${POOL_NTH}

	# The application must not observe an error.
	local TMP_PATTERN=`pattern_create 1M 8` || fail 11
	pattern_write ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 12
	fault_clear_sd

	# Intermittent write timeouts even with FAILFAST set may not cause
	# an EIO (5) event.  This is because how FAILFAST is handled depends
	# a log on the low level driver and the exact nature of the failure.
	# We will however see a 'zfs.delay' event logged due to the timeout.
	VDEV_DELAY=`zpool_event "zfs.delay" "zio_delay"`
	test `printf "%d" ${VDEV_DELAY}` -ge 30000 || fail 13

	# Verify the known pattern.
	pattern_verify ${TMP_PATTERN} 1M 8 ${ZVOL_DEVICE} || fail 14
	pattern_remove ${TMP_PATTERN} || fail 15

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

test_8() {
	test_write_timeout_soft tank scsi_debug-raid0  0 50
	test_write_timeout_soft tank scsi_debug-raid10 1 100
	test_write_timeout_soft tank scsi_debug-raidz  1 75
	test_write_timeout_soft tank scsi_debug-raidz2 1 150
	test_write_timeout_soft tank scsi_debug-raidz3 1 300
	echo
}
run_test 8 "soft write timeout"

# Persistent write timeout at the scsi device layer.
test_write_timeout_hard() {
	local POOL_NAME=$1
	local POOL_CONFIG=$2
	local POOL_REDUNDANT=$3
	local POOL_NTH=$4
	local ZVOL_NAME="zvol"
	local ZVOL_DEVICE="/dev/${POOL_NAME}/${ZVOL_NAME}"
	local RESCAN=1

	if [ ${SCSI_DEBUG} -eq 0 ]; then
		skip_nonewline
		return
	fi

	local TMP_CACHE=`mktemp -p /tmp zpool.cache.XXXXXXXX`
	test_setup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}

	local TMP_PATTERN1=`pattern_create 1M 8`
	local TMP_PATTERN2=`pattern_create 1M 8`
	local TMP_PATTERN3=`pattern_create 1M 8`

	# Create three partitions each one gets a unique pattern.  The first
	# pattern is written before the failure, the second pattern during
	# the failure, and the third pattern while the vdev is degraded.
	# All three patterns are verified while the vdev is degraded and
	# then again once it is brought back online.
	${PARTED} -s ${ZVOL_DEVICE} mklabel gpt || fail 11
	${PARTED} -s ${ZVOL_DEVICE} mkpart primary 1M 16M || fail 12
	${PARTED} -s ${ZVOL_DEVICE} mkpart primary 16M 32M || fail 13
	${PARTED} -s ${ZVOL_DEVICE} mkpart primary 32M 48M || fail 14

	wait_udev ${ZVOL_DEVICE}1 30
	wait_udev ${ZVOL_DEVICE}2 30
	wait_udev ${ZVOL_DEVICE}3 30

	# Before the failure.
	pattern_write ${TMP_PATTERN1} 1M 8 ${ZVOL_DEVICE}1 || fail 15

	# Get the faulty vdev name.
	local VDEV_FAULTY=`nth_zpool_vdev ${POOL_NAME} sd 1`

	# Set timeout(0x4) for every nth command.
	fault_set_sd  4 ${POOL_NTH}

	# During the failure.
	pattern_write ${TMP_PATTERN2} 1M 8 ${ZVOL_DEVICE}2 || fail 21

	# Expect write errors to be logged to 'zpool status'
	local WRITE_ERRORS=`vdev_write_errors ${POOL_NAME} ${VDEV_FAULTY}`
	test ${WRITE_ERRORS} -gt 0 || fail 22

	local VDEV_STATUS=`vdev_status ${POOL_NAME} ${VDEV_FAULTY}`
	test "${VDEV_STATUS}" = "UNAVAIL" || fail 23

	# Clear the error and remove it from /dev/.
	fault_clear_sd
	rm -f /dev/${VDEV_FAULTY}[0-9]

	# Verify the first two patterns and write out the third.
	pattern_write ${TMP_PATTERN3} 1M 8 ${ZVOL_DEVICE}3 || fail 31
	pattern_verify ${TMP_PATTERN1} 1M 8 ${ZVOL_DEVICE}1 || fail 32
	pattern_verify ${TMP_PATTERN2} 1M 8 ${ZVOL_DEVICE}2 || fail 33
	pattern_verify ${TMP_PATTERN3} 1M 8 ${ZVOL_DEVICE}3 || fail 34

	# Bring the device back online by rescanning for it.  It must appear
	# in lsscsi and be available to dd before allowing ZFS to bring it
	# online.  This is not required but provides additional sanity.
	while [ ${RESCAN} -eq 1 ]; do
		scsi_rescan
		wait_udev /dev/${VDEV_FAULTY} 30

		if [ `${LSSCSI} | grep -c "/dev/${VDEV_FAULTY}"` -eq 0 ]; then
			continue
		fi

		dd if=/dev/${VDEV_FAULTY} of=/dev/null bs=8M count=1 &>/dev/null
		if [ $? -ne 0 ]; then
			continue
		fi

		RESCAN=0
	done

	# Bring the device back online.  We expect it to be automatically
	# resilvered without error and we should see minimally the zfs.io,
	# zfs.statechange (VDEV_STATE_HEALTHY (0x7)), and zfs.resilver.*
	# events posted.
	${ZPOOL} online ${POOL_NAME} ${VDEV_FAULTY} || fail 51
	sleep 3
	test `zpool_event "zfs.io" "zio_err"` = "0x5" || fail 52
	test `zpool_event "zfs.statechange" "vdev_state"` = "0x7" || fail 53
	test `zpool_event "zfs.resilver.start" "ena"` != "" || fail 54
	test `zpool_event "zfs.resilver.finish" "ena"` != "" || fail 55
	test `zpool_scan_errors ${POOL_NAME}` -eq 0 || fail 56

	local VDEV_STATUS=`vdev_status ${POOL_NAME} ${VDEV_FAULTY}`
	test "${VDEV_STATUS}" = "ONLINE" || fail 57

	# Verify the known pattern.
	pattern_verify ${TMP_PATTERN1} 1M 8 ${ZVOL_DEVICE}1 || fail 61
	pattern_verify ${TMP_PATTERN2} 1M 8 ${ZVOL_DEVICE}2 || fail 62
	pattern_verify ${TMP_PATTERN3} 1M 8 ${ZVOL_DEVICE}3 || fail 63
	pattern_remove ${TMP_PATTERN1} || fail 64
	pattern_remove ${TMP_PATTERN2} || fail 65
	pattern_remove ${TMP_PATTERN3} || fail 66

	test_cleanup ${POOL_NAME} ${POOL_CONFIG} ${ZVOL_NAME} ${TMP_CACHE}
	pass_nonewline
}

test_9() {
	skip_nonewline # Skip non-redundant config
	test_write_timeout_hard tank scsi_debug-raid10 1 -50
	test_write_timeout_hard tank scsi_debug-raidz  1 -50
	test_write_timeout_hard tank scsi_debug-raidz2 1 -50
	test_write_timeout_hard tank scsi_debug-raidz3 1 -50
	echo
}
run_test 9 "hard write timeout"

exit 0
