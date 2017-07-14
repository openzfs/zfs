#!/bin/bash
#
# Wrapper script for easily running a survey of zpios based tests
#

basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zpios-survey.sh

usage() {
cat << EOF
USAGE:
$0 [hvp] <-c config> <-t test>

DESCRIPTION:
        Helper script for easy zpios survey benchmarking.

OPTIONS:
        -h      Show this message
        -v      Verbose
        -p      Enable profiling
        -c      Zpool configuration
        -t      Zpios test
        -l      Zpios survey log

EOF
}

print_header() {
tee -a ${ZPIOS_SURVEY_LOG} << EOF

================================================================
Test: $1
EOF
}

# Baseline performance for an out of the box config with no manual tuning.
# Ideally, we want everything to be automatically tuned for your system and
# for this to perform reasonably well.
zpios_survey_base() {
	TEST_NAME="${ZPOOL_CONFIG}+${ZPIOS_TEST}+baseline"
	print_header ${TEST_NAME}

	${ZFS_SH} ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZPIOS_SH} ${VERBOSE_FLAG} -c ${ZPOOL_CONFIG} -t ${ZPIOS_TEST} | \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZFS_SH} -u ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
}

# Disable ZFS's prefetching.  For some reason still not clear to me
# current prefetching policy is quite bad for a random workload.
# Allowing the algorithm to detect a random workload and not do 
# anything may be the way to address this issue.
zpios_survey_prefetch() {
	TEST_NAME="${ZPOOL_CONFIG}+${ZPIOS_TEST}+prefetch"
	print_header ${TEST_NAME}

	${ZFS_SH} ${VERBOSE_FLAG}               \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZPIOS_SH} ${VERBOSE_FLAG} -c ${ZPOOL_CONFIG} -t ${ZPIOS_TEST} \
		-o "--noprefetch" |                                    \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZFS_SH} -u ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
}

# Simulating a zerocopy IO path should improve performance by freeing up
# lots of CPU which is wasted move data between buffers.
zpios_survey_zerocopy() {
	TEST_NAME="${ZPOOL_CONFIG}+${ZPIOS_TEST}+zerocopy"
	print_header ${TEST_NAME}

	${ZFS_SH} ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZPIOS_SH} ${VERBOSE_FLAG} -c ${ZPOOL_CONFIG} -t ${ZPIOS_TEST} \
		-o "--zerocopy" |                                      \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZFS_SH} -u ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
}

# Disabling checksumming should show some (if small) improvement
# simply due to freeing up a modest amount of CPU.
zpios_survey_checksum() {
	TEST_NAME="${ZPOOL_CONFIG}+${ZPIOS_TEST}+checksum"
	print_header ${TEST_NAME}

	${ZFS_SH} ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZPIOS_SH} ${VERBOSE_FLAG} -c ${ZPOOL_CONFIG} -t ${ZPIOS_TEST} \
		-s "set checksum=off" |                                \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZFS_SH} -u ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
}

# Increasing the pending IO depth also seems to improve things likely
# at the expense of latency.  This should be explored more because I'm
# seeing a much bigger impact there that I would have expected.  There
# may be some low hanging fruit to be found here.
zpios_survey_pending() {
	TEST_NAME="${ZPOOL_CONFIG}+${ZPIOS_TEST}+pending"
	print_header ${TEST_NAME}

	${ZFS_SH} ${VERBOSE_FLAG}                  \
		zfs="zfs_vdev_max_pending=1024" | \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZPIOS_SH} ${VERBOSE_FLAG} -c ${ZPOOL_CONFIG} -t ${ZPIOS_TEST} | \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZFS_SH} -u ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
}

# Apply all possible turning concurrently to get a best case number
zpios_survey_all() {
	TEST_NAME="${ZPOOL_CONFIG}+${ZPIOS_TEST}+all"
	print_header ${TEST_NAME}

	${ZFS_SH} ${VERBOSE_FLAG}                \  
		zfs="zfs_vdev_max_pending=1024" | \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZPIOS_SH} ${VERBOSE_FLAG} -c ${ZPOOL_CONFIG} -t ${ZPIOS_TEST} \
		-o "--noprefetch --zerocopy"                           \
		-s "set checksum=off" |                                \
		tee -a ${ZPIOS_SURVEY_LOG}
	${ZFS_SH} -u ${VERBOSE_FLAG} | \
		tee -a ${ZPIOS_SURVEY_LOG}
}


PROFILE=
ZPOOL_NAME=zpios-survey
ZPOOL_CONFIG=zpool-config.sh
ZPIOS_TEST=zpios-test.sh
ZPIOS_SURVEY_LOG=/dev/null

while getopts 'hvpc:t:l:' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		VERBOSE_FLAG="-v"
		;;
	p)
		PROFILE=1
		PROFILE_FLAG="-p"
		;;
	c)
		ZPOOL_CONFIG=${OPTARG}
		;;
	t)
		ZPIOS_TEST=${OPTARG}
		;;
	l)
		ZPIOS_SURVEY_LOG=${OPTARG}
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

zpios_survey_base
zpios_survey_prefetch
zpios_survey_zerocopy
zpios_survey_checksum
zpios_survey_pending
zpios_survey_all

exit 0
