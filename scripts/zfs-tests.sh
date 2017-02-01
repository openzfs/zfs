#!/bin/bash
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

. $STF_SUITE/include/default.cfg

PROG=zfs-tests.sh
SUDO=/usr/bin/sudo
SETENFORCE=/usr/sbin/setenforce
VERBOSE=
QUIET=
CLEANUP=1
CLEANUPALL=0
LOOPBACK=1
FILESIZE="2G"
RUNFILE=${RUNFILE:-"linux.run"}
FILEDIR=${FILEDIR:-/var/tmp}
DISKS=${DISKS:-""}

#
# Attempt to remove loopback devices and files which where created earlier
# by this script to run the test framework.  The '-k' option may be passed
# to the script to suppress cleanup for debugging purposes.
#
cleanup() {
	if [ $CLEANUP -eq 0 ]; then
		return 0
	fi

	if [ $LOOPBACK -eq 1 ]; then
		for TEST_LOOPBACK in ${LOOPBACKS}; do
			LOOP_DEV=$(basename $TEST_LOOPBACK)
			DM_DEV=$(${SUDO} ${DMSETUP} ls 2>/dev/null | \
			    grep ${LOOP_DEV} | cut -f1)

			if [ -n "$DM_DEV" ]; then
				${SUDO} ${DMSETUP} remove ${DM_DEV} ||
				    echo "Failed to remove: ${DM_DEV}"
			fi

			if [ -n "${TEST_LOOPBACK}" ]; then
				${SUDO} ${LOSETUP} -d ${TEST_LOOPBACK} ||
				    echo "Failed to remove: ${TEST_LOOPBACK}"
			fi
		done
	fi

	for TEST_FILE in ${FILES}; do
		rm -f ${TEST_FILE} &>/dev/null
	done
}
trap cleanup EXIT

#
# Attempt to remove all testpools (testpool.XXX), unopened dm devices,
# loopback devices, and files.  This is a useful way to cleanup a previous
# test run failure which has left the system in an unknown state.  This can
# be dangerous and should only be used in a dedicated test environment.
#
cleanup_all() {
	local TEST_POOLS=$(${SUDO} ${ZPOOL} list -H -o name | grep testpool)
	local TEST_LOOPBACKS=$(${SUDO} ${LOSETUP} -a|grep file-vdev|cut -f1 -d:)
	local TEST_FILES=$(ls /var/tmp/file-vdev* 2>/dev/null)

	msg
	msg "--- Cleanup ---"
	msg "Removing pool(s):     $(echo ${TEST_POOLS} | tr '\n' ' ')"
	for TEST_POOL in $TEST_POOLS; do
		${SUDO} ${ZPOOL} destroy ${TEST_POOL}
	done

	msg "Removing dm(s):       $(${SUDO} ${DMSETUP} ls |
	    grep loop | tr '\n' ' ')"
	${SUDO} ${DMSETUP} remove_all

	msg "Removing loopback(s): $(echo ${TEST_LOOPBACKS} | tr '\n' ' ')"
	for TEST_LOOPBACK in $TEST_LOOPBACKS; do
		${SUDO} ${LOSETUP} -d ${TEST_LOOPBACK}
	done

	msg "Removing files(s):    $(echo ${TEST_FILES} | tr '\n' ' ')"
	for TEST_FILE in $TEST_FILES; do
		${SUDO} rm -f ${TEST_FILE}
	done
}

#
# Log a failure message, cleanup, and return an error.
#
fail() {
        echo -e "${PROG}: $1" >&2
	cleanup
	exit 1
}

#
# Takes a name as the only arguments and looks for the following variations
# on that name.  If one is found it is returned.
#
# $RUNFILEDIR/<name>
# $RUNFILEDIR/<name>.run
# <name>
# <name>.run
#
find_runfile() {
	local NAME=$1
	local RESULT=""

	if [ -f "$RUNFILEDIR/$NAME" ]; then
		RESULT="$RUNFILEDIR/$NAME"
	elif [ -f "$RUNFILEDIR/$NAME.run" ]; then
		RESULT="$RUNFILEDIR/$NAME.run"
	elif [ -f "$NAME" ]; then
		RESULT="$NAME"
	elif [ -f "$NAME.run" ]; then
		RESULT="$NAME.run"
	fi

	echo "$RESULT"
}

#
# Output a useful usage message.
#
usage() {
cat << EOF
USAGE:
$0 [hvqxkf] [-s SIZE] [-r RUNFILE]

DESCRIPTION:
	ZFS Test Suite launch script

OPTIONS:
	-h          Show this message
	-v          Verbose zfs-tests.sh output
	-q          Quiet test-runner output
	-x          Remove all testpools, dm, lo, and files (unsafe)
	-k          Disable cleanup after test failure
	-f          Use files only, disables block device tests
	-d DIR      Use DIR for files and loopback devices
	-s SIZE     Use vdevs of SIZE (default: 2G)
	-r RUNFILE  Run tests in RUNFILE (default: linux.run)

EXAMPLES:
# Run the default (linux) suite of tests and output the configuration used.
$0 -v

# Run a smaller suite of tests designed to run more quickly.
$0 -r linux-fast

# Cleanup a previous run of the test suite prior to testing, run the
# default (linux) suite of tests and perform no cleanup on exit.
$0 -x

EOF
}

while getopts 'hvqxkfd:s:r:?' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	q)
		QUIET="-q"
		;;
	x)
		CLEANUPALL=1
		;;
	k)
		CLEANUP=0
		;;
	f)
		LOOPBACK=0
		;;
	d)
		FILEDIR="$OPTARG"
		;;
	s)
		FILESIZE="$OPTARG"
		;;
	r)
		RUNFILE="$OPTARG"
		;;
	?)
		usage
		exit
		;;
	esac
done

shift $((OPTIND-1))

FILES=${FILES:-"$FILEDIR/file-vdev0 $FILEDIR/file-vdev1 $FILEDIR/file-vdev2"}
LOOPBACKS=${LOOPBACKS:-""}

#
# Attempt to locate the runfile describing the test workload.
#
if [ -n "$RUNFILE" ]; then
	SAVED_RUNFILE="$RUNFILE"
	RUNFILE=$(find_runfile "$RUNFILE")
	[ -z "$RUNFILE" ] && fail "Cannot find runfile: $SAVED_RUNFILE"
fi

if [ ! -r "$RUNFILE" ]; then
	fail "Cannot read runfile: $RUNFILE"
fi

#
# This script should not be run as root.  Instead the test user, which may
# be a normal user account, needs to be configured such that it can
# run commands via sudo passwordlessly.
#
if [ $(id -u) = "0" ]; then
	fail "This script must not be run as root."
fi

if [ $(sudo whoami) != "root" ]; then
	fail "Passwordless sudo access required."
fi

#
# Check if ksh exists
#
if [ -z "$(which ksh 2>/dev/null)" ]; then
	fail "This test suite requires ksh."
fi

#
# Verify the ZFS module stack if loaded.
#
${SUDO} ${ZFS_SH} &>/dev/null

#
# Attempt to cleanup all previous state for a new test run.
#
if [ $CLEANUPALL -ne 0 ]; then
	cleanup_all
fi

#
# By default preserve any existing pools
#
if [ -z "${KEEP}" ]; then
	KEEP=$(${SUDO} ${ZPOOL} list -H -o name)
	if [ -z "${KEEP}" ]; then
		KEEP="rpool"
	fi
fi

msg
msg "--- Configuration ---"
msg "Runfile:         $RUNFILE"
msg "STF_TOOLS:       $STF_TOOLS"
msg "STF_SUITE:       $STF_SUITE"

#
# No DISKS have been provided so a basic file or loopback based devices
# must be created for the test suite to use.
#
if [ -z "${DISKS}" ]; then
	#
	# Create sparse files for the test suite.  These may be used
	# directory or have loopback devices layered on them.
	#
	for TEST_FILE in ${FILES}; do
		[ -f "$TEST_FILE" ] && fail "Failed file exists: ${TEST_FILE}"
		truncate -s ${FILESIZE} ${TEST_FILE} ||
		    fail "Failed creating: ${TEST_FILE} ($?)"
		DISKS="$DISKS$TEST_FILE "
	done

	#
	# If requested setup loopback devices backed by the sparse files.
	#
	if [ $LOOPBACK -eq 1 ]; then
		DISKS=""
		check_loop_utils

		for TEST_FILE in ${FILES}; do
			TEST_LOOPBACK=$(${SUDO} ${LOSETUP} -f)
			${SUDO} ${LOSETUP} ${TEST_LOOPBACK} ${TEST_FILE} ||
			    fail "Failed: ${TEST_FILE} -> ${TEST_LOOPBACK}"
			LOOPBACKS="${LOOPBACKS}${TEST_LOOPBACK} "
			DISKS="$DISKS$(basename $TEST_LOOPBACK) "
		done
	fi
fi

NUM_DISKS=$(echo ${DISKS} | $AWK '{print NF}')
[ $NUM_DISKS -lt 3 ] && fail "Not enough disks ($NUM_DISKS/3 minimum)"

#
# Disable SELinux until the ZFS Test Suite has been updated accordingly.
#
if [ -x ${SETENFORCE} ]; then
	${SUDO} ${SETENFORCE} permissive &>/dev/null
fi

msg "FILEDIR:         $FILEDIR"
msg "FILES:           $FILES"
msg "LOOPBACKS:       $LOOPBACKS"
msg "DISKS:           $DISKS"
msg "NUM_DISKS:       $NUM_DISKS"
msg "FILESIZE:        $FILESIZE"
msg "Keep pool(s):    $KEEP"
msg ""

export STF_TOOLS
export STF_SUITE
export DISKS
export KEEP

msg "${TEST_RUNNER} ${QUIET} -c ${RUNFILE} -i ${STF_SUITE}"
${TEST_RUNNER} ${QUIET} -c ${RUNFILE} -i ${STF_SUITE}
RESULT=$?
echo

exit ${RESULT}
