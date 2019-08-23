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

BASE_DIR=$(dirname "$0")
SCRIPT_COMMON=common.sh
if [ -f "${BASE_DIR}/${SCRIPT_COMMON}" ]; then
. "${BASE_DIR}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zfs-tests.sh
VERBOSE="no"
QUIET=""
CLEANUP="yes"
CLEANUPALL="no"
LOOPBACK="yes"
STACK_TRACER="no"
FILESIZE="4G"
RUNFILE=${RUNFILE:-"linux.run"}
FILEDIR=${FILEDIR:-/var/tmp}
DISKS=${DISKS:-""}
SINGLETEST=()
SINGLETESTUSER="root"
TAGS=""
ITERATIONS=1
ZFS_DBGMSG="$STF_SUITE/callbacks/zfs_dbgmsg.ksh"
ZFS_DMESG="$STF_SUITE/callbacks/zfs_dmesg.ksh"
ZFS_MMP="$STF_SUITE/callbacks/zfs_mmp.ksh"
TESTFAIL_CALLBACKS=${TESTFAIL_CALLBACKS:-"$ZFS_DBGMSG:$ZFS_DMESG:$ZFS_MMP"}
LOSETUP=${LOSETUP:-/sbin/losetup}
DMSETUP=${DMSETUP:-/sbin/dmsetup}

#
# Log an informational message when additional verbosity is enabled.
#
msg() {
	if [ "$VERBOSE" = "yes" ]; then
		echo "$@"
	fi
}

#
# Log a failure message, cleanup, and return an error.
#
fail() {
        echo -e "$PROG: $1" >&2
	cleanup
	exit 1
}

#
# Attempt to remove loopback devices and files which where created earlier
# by this script to run the test framework.  The '-k' option may be passed
# to the script to suppress cleanup for debugging purposes.
#
cleanup() {
	if [ "$CLEANUP" = "no" ]; then
		return 0
	fi

	if [ "$LOOPBACK" = "yes" ]; then
		for TEST_LOOPBACK in ${LOOPBACKS}; do
			LOOP_DEV=$(basename "$TEST_LOOPBACK")
			DM_DEV=$(sudo "${DMSETUP}" ls 2>/dev/null | \
			    grep "${LOOP_DEV}" | cut -f1)

			if [ -n "$DM_DEV" ]; then
				sudo "${DMSETUP}" remove "${DM_DEV}" ||
				    echo "Failed to remove: ${DM_DEV}"
			fi

			if [ -n "${TEST_LOOPBACK}" ]; then
				sudo "${LOSETUP}" -d "${TEST_LOOPBACK}" ||
				    echo "Failed to remove: ${TEST_LOOPBACK}"
			fi
		done
	fi

	for TEST_FILE in ${FILES}; do
		rm -f "${TEST_FILE}" &>/dev/null
	done

	if [ "$STF_PATH_REMOVE" = "yes" ] && [ -d "$STF_PATH" ]; then
		rm -Rf "$STF_PATH"
	fi
}
trap cleanup EXIT

#
# Attempt to remove all testpools (testpool.XXX), unopened dm devices,
# loopback devices, and files.  This is a useful way to cleanup a previous
# test run failure which has left the system in an unknown state.  This can
# be dangerous and should only be used in a dedicated test environment.
#
cleanup_all() {
	local TEST_POOLS
	TEST_POOLS=$(sudo "$ZPOOL" list -H -o name | grep testpool)
	local TEST_LOOPBACKS
	TEST_LOOPBACKS=$(sudo "${LOSETUP}" -a|grep file-vdev|cut -f1 -d:)
	local TEST_FILES
	TEST_FILES=$(ls /var/tmp/file-vdev* 2>/dev/null)

	msg
	msg "--- Cleanup ---"
	msg "Removing pool(s):     $(echo "${TEST_POOLS}" | tr '\n' ' ')"
	for TEST_POOL in $TEST_POOLS; do
		sudo "$ZPOOL" destroy "${TEST_POOL}"
	done

	msg "Removing dm(s):       $(sudo "${DMSETUP}" ls |
	    grep loop | tr '\n' ' ')"
	sudo "${DMSETUP}" remove_all

	msg "Removing loopback(s): $(echo "${TEST_LOOPBACKS}" | tr '\n' ' ')"
	for TEST_LOOPBACK in $TEST_LOOPBACKS; do
		sudo "${LOSETUP}" -d "${TEST_LOOPBACK}"
	done

	msg "Removing files(s):    $(echo "${TEST_FILES}" | tr '\n' ' ')"
	for TEST_FILE in $TEST_FILES; do
		sudo rm -f "${TEST_FILE}"
	done
}

#
# Takes a name as the only arguments and looks for the following variations
# on that name.  If one is found it is returned.
#
# $RUNFILE_DIR/<name>
# $RUNFILE_DIR/<name>.run
# <name>
# <name>.run
#
find_runfile() {
	local NAME=$1
	local RESULT=""

	if [ -f "$RUNFILE_DIR/$NAME" ]; then
		RESULT="$RUNFILE_DIR/$NAME"
	elif [ -f "$RUNFILE_DIR/$NAME.run" ]; then
		RESULT="$RUNFILE_DIR/$NAME.run"
	elif [ -f "$NAME" ]; then
		RESULT="$NAME"
	elif [ -f "$NAME.run" ]; then
		RESULT="$NAME.run"
	fi

	echo "$RESULT"
}

#
# Symlink file if it appears under any of the given paths.
#
create_links() {
	local dir_list="$1"
	local file_list="$2"

	[ -n "$STF_PATH" ] || fail "STF_PATH wasn't correctly set"

	for i in $file_list; do
		for j in $dir_list; do
			[ ! -e "$STF_PATH/$i" ] || continue

			if [ ! -d "$j/$i" ] && [ -e "$j/$i" ]; then
				ln -s "$j/$i" "$STF_PATH/$i" || \
				    fail "Couldn't link $i"
				break
			fi
		done

		[ ! -e "$STF_PATH/$i" ] && STF_MISSING_BIN="$STF_MISSING_BIN$i "
	done
}

#
# Constrain the path to limit the available binaries to a known set.
# When running in-tree a top level ./bin/ directory is created for
# convenience, otherwise a temporary directory is used.
#
constrain_path() {
	. "$STF_SUITE/include/commands.cfg"

	if [ "$INTREE" = "yes" ]; then
		# Constrained path set to ./zfs/bin/
		STF_PATH="$BIN_DIR"
		STF_PATH_REMOVE="no"
		STF_MISSING_BIN=""
		if [ ! -d "$STF_PATH" ]; then
			mkdir "$STF_PATH"
			chmod 755 "$STF_PATH" || fail "Couldn't chmod $STF_PATH"
		fi

		# Special case links for standard zfs utilities
		DIRS="$(find "$CMD_DIR" -type d \( ! -name .deps -a \
		    ! -name .libs \) -print | tr '\n' ' ')"
		create_links "$DIRS" "$ZFS_FILES"

		# Special case links for zfs test suite utilities
		DIRS="$(find "$STF_SUITE" -type d \( ! -name .deps -a \
		    ! -name .libs \) -print | tr '\n' ' ')"
		create_links "$DIRS" "$ZFSTEST_FILES"
	else
		# Constrained path set to /var/tmp/constrained_path.*
		SYSTEMDIR=${SYSTEMDIR:-/var/tmp/constrained_path.XXXX}
		STF_PATH=$(/bin/mktemp -d "$SYSTEMDIR")
		STF_PATH_REMOVE="yes"
		STF_MISSING_BIN=""

		chmod 755 "$STF_PATH" || fail "Couldn't chmod $STF_PATH"

		# Special case links for standard zfs utilities
		create_links "/bin /usr/bin /sbin /usr/sbin" "$ZFS_FILES"

		# Special case links for zfs test suite utilities
		create_links "$STF_SUITE/bin" "$ZFSTEST_FILES"
	fi

	# Standard system utilities
	create_links "/bin /usr/bin /sbin /usr/sbin" "$SYSTEM_FILES"

	# Exceptions
	ln -fs "$STF_PATH/awk" "$STF_PATH/nawk"
	ln -fs /sbin/fsck.ext4 "$STF_PATH/fsck"
	ln -fs /sbin/mkfs.ext4 "$STF_PATH/newfs"
	ln -fs "$STF_PATH/gzip" "$STF_PATH/compress"
	ln -fs "$STF_PATH/gunzip" "$STF_PATH/uncompress"
	ln -fs "$STF_PATH/exportfs" "$STF_PATH/share"
	ln -fs "$STF_PATH/exportfs" "$STF_PATH/unshare"

	if [ -L "$STF_PATH/arc_summary3" ]; then
		ln -fs "$STF_PATH/arc_summary3" "$STF_PATH/arc_summary"
	fi
}

#
# Output a useful usage message.
#
usage() {
cat << EOF
USAGE:
$0 [hvqxkfS] [-s SIZE] [-r RUNFILE] [-t PATH] [-u USER]

DESCRIPTION:
	ZFS Test Suite launch script

OPTIONS:
	-h          Show this message
	-v          Verbose zfs-tests.sh output
	-q          Quiet test-runner output
	-x          Remove all testpools, dm, lo, and files (unsafe)
	-k          Disable cleanup after test failure
	-f          Use files only, disables block device tests
	-S          Enable stack tracer (negative performance impact)
	-c          Only create and populate constrained path
	-n NFSFILE  Use the nfsfile to determine the NFS configuration
	-I NUM      Number of iterations
	-d DIR      Use DIR for files and loopback devices
	-s SIZE     Use vdevs of SIZE (default: 4G)
	-r RUNFILE  Run tests in RUNFILE (default: linux.run)
	-t PATH     Run single test at PATH relative to test suite
	-T TAGS     Comma separated list of tags (default: 'functional')
	-u USER     Run single test as USER (default: root)

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

while getopts 'hvqxkfScn:d:s:r:?t:T:u:I:' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		# shellcheck disable=SC2034
		VERBOSE="yes"
		;;
	q)
		QUIET="yes"
		;;
	x)
		CLEANUPALL="yes"
		;;
	k)
		CLEANUP="no"
		;;
	f)
		LOOPBACK="no"
		;;
	S)
		STACK_TRACER="yes"
		;;
	c)
		constrain_path
		exit
		;;
	n)
		nfsfile=$OPTARG
		[[ -f $nfsfile ]] || fail "Cannot read file: $nfsfile"
		export NFS=1
		. "$nfsfile"
		;;
	d)
		FILEDIR="$OPTARG"
		;;
	I)
		ITERATIONS="$OPTARG"
		if [ "$ITERATIONS" -le 0 ]; then
			fail "Iterations must be greater than 0."
		fi
		;;
	s)
		FILESIZE="$OPTARG"
		;;
	r)
		RUNFILE="$OPTARG"
		;;
	t)
		if [ ${#SINGLETEST[@]} -ne 0 ]; then
			fail "-t can only be provided once."
		fi
		SINGLETEST+=("$OPTARG")
		;;
	T)
		TAGS="$OPTARG"
		;;
	u)
		SINGLETESTUSER="$OPTARG"
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

if [ ${#SINGLETEST[@]} -ne 0 ]; then
	if [ -n "$TAGS" ]; then
		fail "-t and -T are mutually exclusive."
	fi
	RUNFILE_DIR="/var/tmp"
	RUNFILE="zfs-tests.$$.run"
	SINGLEQUIET="False"

	if [ -n "$QUIET" ]; then
		SINGLEQUIET="True"
	fi

	cat >$RUNFILE_DIR/$RUNFILE << EOF
[DEFAULT]
pre =
quiet = $SINGLEQUIET
pre_user = root
user = $SINGLETESTUSER
timeout = 600
post_user = root
post =
outputdir = /var/tmp/test_results
EOF
	for t in "${SINGLETEST[@]}"
	do
		SINGLETESTDIR=$(dirname "$t")
		SINGLETESTFILE=$(basename "$t")
		SETUPSCRIPT=
		CLEANUPSCRIPT=

		if [ -f "$STF_SUITE/$SINGLETESTDIR/setup.ksh" ]; then
			SETUPSCRIPT="setup"
		fi

		if [ -f "$STF_SUITE/$SINGLETESTDIR/cleanup.ksh" ]; then
			CLEANUPSCRIPT="cleanup"
		fi

		cat >>$RUNFILE_DIR/$RUNFILE << EOF

[$SINGLETESTDIR]
tests = ['$SINGLETESTFILE']
pre = $SETUPSCRIPT
post = $CLEANUPSCRIPT
tags = ['functional']
EOF
	done
fi

#
# Use default tag if none was specified
#
TAGS=${TAGS:='functional'}

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
if [ "$(id -u)" = "0" ]; then
	fail "This script must not be run as root."
fi

if [ "$(sudo whoami)" != "root" ]; then
	fail "Passwordless sudo access required."
fi

#
# Constrain the available binaries to a known set.
#
constrain_path

#
# Check if ksh exists
#
[ -e "$STF_PATH/ksh" ] || fail "This test suite requires ksh."
[ -e "$STF_SUITE/include/default.cfg" ] || fail \
    "Missing $STF_SUITE/include/default.cfg file."

#
# Verify the ZFS module stack is loaded.
#
if [ "$STACK_TRACER" = "yes" ]; then
	sudo "${ZFS_SH}" -S &>/dev/null
else
	sudo "${ZFS_SH}" &>/dev/null
fi

#
# Attempt to cleanup all previous state for a new test run.
#
if [ "$CLEANUPALL" = "yes" ]; then
	cleanup_all
fi

#
# By default preserve any existing pools
# NOTE: Since 'zpool list' outputs a newline-delimited list convert $KEEP from
# space-delimited to newline-delimited.
#
if [ -z "${KEEP}" ]; then
	KEEP="$(sudo "$ZPOOL" list -H -o name)"
	if [ -z "${KEEP}" ]; then
		KEEP="rpool"
	fi
else
	KEEP="$(echo -e "${KEEP//[[:blank:]]/\n}")"
fi

#
# NOTE: The following environment variables are undocumented
# and should be used for testing purposes only:
#
# __ZFS_POOL_EXCLUDE - don't iterate over the pools it lists
# __ZFS_POOL_RESTRICT - iterate only over the pools it lists
#
# See libzfs/libzfs_config.c for more information.
#
__ZFS_POOL_EXCLUDE="$(echo "$KEEP" | sed ':a;N;s/\n/ /g;ba')"

. "$STF_SUITE/include/default.cfg"

msg
msg "--- Configuration ---"
msg "Runfile:         $RUNFILE"
msg "STF_TOOLS:       $STF_TOOLS"
msg "STF_SUITE:       $STF_SUITE"
msg "STF_PATH:        $STF_PATH"

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
		truncate -s "${FILESIZE}" "${TEST_FILE}" ||
		    fail "Failed creating: ${TEST_FILE} ($?)"
		if [[ "$DISKS" ]]; then
			DISKS="$DISKS $TEST_FILE"
		else
			DISKS="$TEST_FILE"
		fi
	done

	#
	# If requested setup loopback devices backed by the sparse files.
	#
	if [ "$LOOPBACK" = "yes" ]; then
		DISKS=""

		test -x "$LOSETUP" || fail "$LOSETUP utility must be installed"

		for TEST_FILE in ${FILES}; do
			TEST_LOOPBACK=$(sudo "${LOSETUP}" -f)
			sudo "${LOSETUP}" "${TEST_LOOPBACK}" "${TEST_FILE}" ||
			    fail "Failed: ${TEST_FILE} -> ${TEST_LOOPBACK}"
			LOOPBACKS="${LOOPBACKS}${TEST_LOOPBACK} "
			BASELOOPBACKS=$(basename "$TEST_LOOPBACK")
			if [[ "$DISKS" ]]; then
				DISKS="$DISKS $BASELOOPBACKS"
			else
				DISKS="$BASELOOPBACKS"
			fi
		done
	fi
fi

NUM_DISKS=$(echo "${DISKS}" | awk '{print NF}')
[ "$NUM_DISKS" -lt 3 ] && fail "Not enough disks ($NUM_DISKS/3 minimum)"

#
# Disable SELinux until the ZFS Test Suite has been updated accordingly.
#
if [ -x "$STF_PATH/setenforce" ]; then
	sudo setenforce permissive &>/dev/null
fi

#
# Enable internal ZFS debug log and clear it.
#
if [ -e /sys/module/zfs/parameters/zfs_dbgmsg_enable ]; then
	sudo /bin/sh -c "echo 1 >/sys/module/zfs/parameters/zfs_dbgmsg_enable"
	sudo /bin/sh -c "echo 0 >/proc/spl/kstat/zfs/dbgmsg"
fi

msg "FILEDIR:         $FILEDIR"
msg "FILES:           $FILES"
msg "LOOPBACKS:       $LOOPBACKS"
msg "DISKS:           $DISKS"
msg "NUM_DISKS:       $NUM_DISKS"
msg "FILESIZE:        $FILESIZE"
msg "ITERATIONS:      $ITERATIONS"
msg "TAGS:            $TAGS"
msg "STACK_TRACER:    $STACK_TRACER"
msg "Keep pool(s):    $KEEP"
msg "Missing util(s): $STF_MISSING_BIN"
msg ""

export STF_TOOLS
export STF_SUITE
export STF_PATH
export DISKS
export FILEDIR
export KEEP
export __ZFS_POOL_EXCLUDE
export TESTFAIL_CALLBACKS
export PATH=$STF_PATH

RESULTS_FILE=$(mktemp -u -t zts-results.XXXX -p "$FILEDIR")
REPORT_FILE=$(mktemp -u -t zts-report.XXXX -p "$FILEDIR")

#
# Run all the tests as specified.
#
msg "${TEST_RUNNER} ${QUIET:+-q}" \
    "-c \"${RUNFILE}\"" \
    "-T \"${TAGS}\"" \
    "-i \"${STF_SUITE}\"" \
    "-I \"${ITERATIONS}\""
${TEST_RUNNER} ${QUIET:+-q} \
    -c "${RUNFILE}" \
    -T "${TAGS}" \
    -i "${STF_SUITE}" \
    -I "${ITERATIONS}" \
    2>&1 | tee "$RESULTS_FILE"

#
# Analyze the results.
#
set -o pipefail
${ZTS_REPORT} "$RESULTS_FILE" | tee "$REPORT_FILE"
RESULT=$?
set +o pipefail

RESULTS_DIR=$(awk '/^Log directory/ { print $3 }' "$RESULTS_FILE")
if [ -d "$RESULTS_DIR" ]; then
	cat "$RESULTS_FILE" "$REPORT_FILE" >"$RESULTS_DIR/results"
fi

rm -f "$RESULTS_FILE" "$REPORT_FILE"

if [ ${#SINGLETEST[@]} -ne 0 ]; then
	rm -f "$RUNFILE" &>/dev/null
fi

exit ${RESULT}
