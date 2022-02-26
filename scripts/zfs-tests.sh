#!/bin/sh
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

#
# Copyright 2020 OmniOS Community Edition (OmniOSce) Association.
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
DEFAULT_RUNFILES="common.run,$(uname | tr '[:upper:]' '[:lower:]').run"
RUNFILES=${RUNFILES:-$DEFAULT_RUNFILES}
FILEDIR=${FILEDIR:-/var/tmp}
DISKS=${DISKS:-""}
SINGLETEST=""
SINGLETESTUSER="root"
TAGS=""
ITERATIONS=1
ZFS_DBGMSG="$STF_SUITE/callbacks/zfs_dbgmsg.ksh"
ZFS_DMESG="$STF_SUITE/callbacks/zfs_dmesg.ksh"
UNAME=$(uname -s)
RERUN=""

# Override some defaults if on FreeBSD
if [ "$UNAME" = "FreeBSD" ] ; then
	TESTFAIL_CALLBACKS=${TESTFAIL_CALLBACKS:-"$ZFS_DMESG"}
	LOSETUP=/sbin/mdconfig
	DMSETUP=/sbin/gpart
else
	ZFS_MMP="$STF_SUITE/callbacks/zfs_mmp.ksh"
	TESTFAIL_CALLBACKS=${TESTFAIL_CALLBACKS:-"$ZFS_DBGMSG:$ZFS_DMESG:$ZFS_MMP"}
	LOSETUP=${LOSETUP:-/sbin/losetup}
	DMSETUP=${DMSETUP:-/sbin/dmsetup}
fi

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
	echo "$PROG: $1" >&2
	cleanup
	exit 1
}

cleanup_freebsd_loopback() {
	for TEST_LOOPBACK in ${LOOPBACKS}; do
		if [ -c "/dev/${TEST_LOOPBACK}" ]; then
			sudo "${LOSETUP}" -d -u "${TEST_LOOPBACK}" ||
			    echo "Failed to destroy: ${TEST_LOOPBACK}"
		fi
	done
}

cleanup_linux_loopback() {
	for TEST_LOOPBACK in ${LOOPBACKS}; do
		LOOP_DEV="${TEST_LOOPBACK##*/}"
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
		if [ "$UNAME" = "FreeBSD" ] ; then
			cleanup_freebsd_loopback
		else
			cleanup_linux_loopback
		fi
	fi

	for TEST_FILE in ${FILES}; do
		rm -f "${TEST_FILE}" >/dev/null 2>&1
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
	TEST_POOLS=$(sudo "$ZPOOL" list -H -o name | grep testpool)
	if [ "$UNAME" = "FreeBSD" ] ; then
		TEST_LOOPBACKS=$(sudo "${LOSETUP}" -l)
	else
		TEST_LOOPBACKS=$(sudo "${LOSETUP}" -a|grep file-vdev|cut -f1 -d:)
	fi
	TEST_FILES=$(ls /var/tmp/file-vdev* 2>/dev/null)

	msg
	msg "--- Cleanup ---"
	msg "Removing pool(s):     $(echo "${TEST_POOLS}" | tr '\n' ' ')"
	for TEST_POOL in $TEST_POOLS; do
		sudo "$ZPOOL" destroy "${TEST_POOL}"
	done

	if [ "$UNAME" != "FreeBSD" ] ; then
		msg "Removing dm(s):       $(sudo "${DMSETUP}" ls |
		    grep loop | tr '\n' ' ')"
		sudo "${DMSETUP}" remove_all
	fi

	msg "Removing loopback(s): $(echo "${TEST_LOOPBACKS}" | tr '\n' ' ')"
	for TEST_LOOPBACK in $TEST_LOOPBACKS; do
		if [ "$UNAME" = "FreeBSD" ] ; then
			sudo "${LOSETUP}" -d -u "${TEST_LOOPBACK}"
		else
			sudo "${LOSETUP}" -d "${TEST_LOOPBACK}"
		fi
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
	NAME=$1
	RESULT=""

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
	dir_list="$1"
	file_list="$2"

	[ -n "$STF_PATH" ] || fail "STF_PATH wasn't correctly set"

	for i in $file_list; do
		for j in $dir_list; do
			[ ! -e "$STF_PATH/$i" ] || continue

			if [ ! -d "$j/$i" ] && [ -e "$j/$i" ]; then
				ln -sf "$j/$i" "$STF_PATH/$i" || \
				    fail "Couldn't link $i"
				break
			fi
		done

		[ ! -e "$STF_PATH/$i" ] && \
		    STF_MISSING_BIN="$STF_MISSING_BIN $i"
	done
	STF_MISSING_BIN=${STF_MISSING_BIN# }
}

#
# Constrain the path to limit the available binaries to a known set.
# When running in-tree a top level ./bin/ directory is created for
# convenience, otherwise a temporary directory is used.
#
constrain_path() {
	. "$STF_SUITE/include/commands.cfg"

	# On FreeBSD, base system zfs utils are in /sbin and OpenZFS utils
	# install to /usr/local/sbin. To avoid testing the wrong utils we
	# need /usr/local to come before / in the path search order.
	SYSTEM_DIRS="/usr/local/bin /usr/local/sbin"
	SYSTEM_DIRS="$SYSTEM_DIRS /usr/bin /usr/sbin /bin /sbin $LIBEXEC_DIR"

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
		SYSTEMDIR=${SYSTEMDIR:-/var/tmp/constrained_path.XXXXXX}
		STF_PATH=$(mktemp -d "$SYSTEMDIR")
		STF_PATH_REMOVE="yes"
		STF_MISSING_BIN=""

		chmod 755 "$STF_PATH" || fail "Couldn't chmod $STF_PATH"

		# Special case links for standard zfs utilities
		create_links "$SYSTEM_DIRS" "$ZFS_FILES"

		# Special case links for zfs test suite utilities
		create_links "$STF_SUITE/bin" "$ZFSTEST_FILES"
	fi

	# Standard system utilities
	SYSTEM_FILES="$SYSTEM_FILES_COMMON"
	if [ "$UNAME" = "FreeBSD" ] ; then
		SYSTEM_FILES="$SYSTEM_FILES $SYSTEM_FILES_FREEBSD"
	else
		SYSTEM_FILES="$SYSTEM_FILES $SYSTEM_FILES_LINUX"
	fi
	create_links "$SYSTEM_DIRS" "$SYSTEM_FILES"

	# Exceptions
	ln -fs "$STF_PATH/awk" "$STF_PATH/nawk"
	if [ "$UNAME" = "Linux" ] ; then
		ln -fs /sbin/fsck.ext4 "$STF_PATH/fsck"
		ln -fs /sbin/mkfs.ext4 "$STF_PATH/newfs"
		ln -fs "$STF_PATH/gzip" "$STF_PATH/compress"
		ln -fs "$STF_PATH/gunzip" "$STF_PATH/uncompress"
		ln -fs "$STF_PATH/exportfs" "$STF_PATH/share"
		ln -fs "$STF_PATH/exportfs" "$STF_PATH/unshare"
	elif [ "$UNAME" = "FreeBSD" ] ; then
		ln -fs /usr/local/bin/ksh93 "$STF_PATH/ksh"
	fi
}

#
# Output a useful usage message.
#
usage() {
cat << EOF
USAGE:
$0 [-hvqxkfS] [-s SIZE] [-r RUNFILES] [-t PATH] [-u USER]

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
	-R          Automatically rerun failing tests
	-n NFSFILE  Use the nfsfile to determine the NFS configuration
	-I NUM      Number of iterations
	-d DIR      Use DIR for files and loopback devices
	-s SIZE     Use vdevs of SIZE (default: 4G)
	-r RUNFILES Run tests in RUNFILES (default: ${DEFAULT_RUNFILES})
	-t PATH     Run single test at PATH relative to test suite
	-T TAGS     Comma separated list of tags (default: 'functional')
	-u USER     Run single test as USER (default: root)

EXAMPLES:
# Run the default (linux) suite of tests and output the configuration used.
$0 -v

# Run a smaller suite of tests designed to run more quickly.
$0 -r linux-fast

# Run a single test
$0 -t tests/functional/cli_root/zfs_bookmark/zfs_bookmark_cliargs.ksh

# Cleanup a previous run of the test suite prior to testing, run the
# default (linux) suite of tests and perform no cleanup on exit.
$0 -x

EOF
}

while getopts 'hvqxkfScRn:d:s:r:?t:T:u:I:' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
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
	R)
		RERUN="yes"
		;;
	n)
		nfsfile=$OPTARG
		[ -f "$nfsfile" ] || fail "Cannot read file: $nfsfile"
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
		RUNFILES="$OPTARG"
		;;
	t)
		if [ -n "$SINGLETEST" ]; then
			fail "-t can only be provided once."
		fi
		SINGLETEST="$OPTARG"
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

if [ -n "$SINGLETEST" ]; then
	if [ -n "$TAGS" ]; then
		fail "-t and -T are mutually exclusive."
	fi
	RUNFILE_DIR="/var/tmp"
	RUNFILES="zfs-tests.$$.run"
	SINGLEQUIET="False"

	if [ -n "$QUIET" ]; then
		SINGLEQUIET="True"
	fi

	cat >$RUNFILE_DIR/$RUNFILES << EOF
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
	SINGLETESTDIR=$(dirname "$SINGLETEST")
	SINGLETESTFILE=$(basename "$SINGLETEST")
	SETUPSCRIPT=
	CLEANUPSCRIPT=

	if [ -f "$STF_SUITE/$SINGLETESTDIR/setup.ksh" ]; then
		SETUPSCRIPT="setup"
	fi

	if [ -f "$STF_SUITE/$SINGLETESTDIR/cleanup.ksh" ]; then
		CLEANUPSCRIPT="cleanup"
	fi

	cat >>$RUNFILE_DIR/$RUNFILES << EOF

[$SINGLETESTDIR]
tests = ['$SINGLETESTFILE']
pre = $SETUPSCRIPT
post = $CLEANUPSCRIPT
tags = ['functional']
EOF
fi

#
# Use default tag if none was specified
#
TAGS=${TAGS:='functional'}

#
# Attempt to locate the runfiles describing the test workload.
#
R=""
IFS=,
for RUNFILE in $RUNFILES; do
	if [ -n "$RUNFILE" ]; then
		SAVED_RUNFILE="$RUNFILE"
		RUNFILE=$(find_runfile "$RUNFILE")
		[ -z "$RUNFILE" ] && fail "Cannot find runfile: $SAVED_RUNFILE"
		R="$R,$RUNFILE"
	fi

	if [ ! -r "$RUNFILE" ]; then
		fail "Cannot read runfile: $RUNFILE"
	fi
done
unset IFS
RUNFILES=${R#,}

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
if [ "$UNAME" = "FreeBSD" ]; then
	sudo ln -fs /usr/local/bin/ksh93 /bin/ksh
fi
[ -e "$STF_PATH/ksh" ] || fail "This test suite requires ksh."
[ -e "$STF_SUITE/include/default.cfg" ] || fail \
    "Missing $STF_SUITE/include/default.cfg file."

#
# Verify the ZFS module stack is loaded.
#
if [ "$STACK_TRACER" = "yes" ]; then
	sudo "${ZFS_SH}" -S >/dev/null 2>&1
else
	sudo "${ZFS_SH}" >/dev/null 2>&1
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
	KEEP="$(echo "$KEEP" | tr '[:blank:]' '\n')"
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
if [ "$UNAME" = "FreeBSD" ] ; then
	__ZFS_POOL_EXCLUDE="$(echo "$KEEP" | tr -s '\n' ' ')"
else
	__ZFS_POOL_EXCLUDE="$(echo "$KEEP" | sed ':a;N;s/\n/ /g;ba')"
fi

. "$STF_SUITE/include/default.cfg"

#
# No DISKS have been provided so a basic file or loopback based devices
# must be created for the test suite to use.
#
if [ -z "${DISKS}" ]; then
	#
	# If this is a performance run, prevent accidental use of
	# loopback devices.
	#
	[ "$TAGS" = "perf" ] && fail "Running perf tests without disks."

	#
	# Create sparse files for the test suite.  These may be used
	# directory or have loopback devices layered on them.
	#
	for TEST_FILE in ${FILES}; do
		[ -f "$TEST_FILE" ] && fail "Failed file exists: ${TEST_FILE}"
		truncate -s "${FILESIZE}" "${TEST_FILE}" ||
		    fail "Failed creating: ${TEST_FILE} ($?)"
	done

	#
	# If requested setup loopback devices backed by the sparse files.
	#
	if [ "$LOOPBACK" = "yes" ]; then
		test -x "$LOSETUP" || fail "$LOSETUP utility must be installed"

		for TEST_FILE in ${FILES}; do
			if [ "$UNAME" = "FreeBSD" ] ; then
				MDDEVICE=$(sudo "${LOSETUP}" -a -t vnode -f "${TEST_FILE}")
				if [ -z "$MDDEVICE" ] ; then
					fail "Failed: ${TEST_FILE} -> loopback"
				fi
				DISKS="$DISKS $MDDEVICE"
				LOOPBACKS="$LOOPBACKS $MDDEVICE"
			else
				TEST_LOOPBACK=$(sudo "${LOSETUP}" -f)
				sudo "${LOSETUP}" "${TEST_LOOPBACK}" "${TEST_FILE}" ||
				    fail "Failed: ${TEST_FILE} -> ${TEST_LOOPBACK}"
				BASELOOPBACK="${TEST_LOOPBACK##*/}"
				DISKS="$DISKS $BASELOOPBACK"
				LOOPBACKS="$LOOPBACKS $TEST_LOOPBACK"
			fi
		done
		DISKS=${DISKS# }
		LOOPBACKS=${LOOPBACKS# }
	else
		DISKS="$FILES"
	fi
fi

#
# It may be desirable to test with fewer disks than the default when running
# the performance tests, but the functional tests require at least three.
#
NUM_DISKS=$(echo "${DISKS}" | awk '{print NF}')
if [ "$TAGS" != "perf" ]; then
	[ "$NUM_DISKS" -lt 3 ] && fail "Not enough disks ($NUM_DISKS/3 minimum)"
fi

#
# Disable SELinux until the ZFS Test Suite has been updated accordingly.
#
if [ -x "$STF_PATH/setenforce" ]; then
	sudo setenforce permissive >/dev/null 2>&1
fi

#
# Enable internal ZFS debug log and clear it.
#
if [ -e /sys/module/zfs/parameters/zfs_dbgmsg_enable ]; then
	sudo /bin/sh -c "echo 1 >/sys/module/zfs/parameters/zfs_dbgmsg_enable"
	sudo /bin/sh -c "echo 0 >/proc/spl/kstat/zfs/dbgmsg"
fi

msg
msg "--- Configuration ---"
msg "Runfiles:        $RUNFILES"
msg "STF_TOOLS:       $STF_TOOLS"
msg "STF_SUITE:       $STF_SUITE"
msg "STF_PATH:        $STF_PATH"
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

mktemp_file() {
	if [ "$UNAME" = "FreeBSD" ]; then
		mktemp -u "${FILEDIR}/$1.XXXXXX"
	else
		mktemp -ut "$1.XXXXXX" -p "$FILEDIR"
	fi
}
mkdir -p "$FILEDIR" || :
RESULTS_FILE=$(mktemp_file zts-results)
REPORT_FILE=$(mktemp_file zts-report)

#
# Run all the tests as specified.
#
msg "${TEST_RUNNER} ${QUIET:+-q}" \
    "-c \"${RUNFILES}\"" \
    "-T \"${TAGS}\"" \
    "-i \"${STF_SUITE}\"" \
    "-I \"${ITERATIONS}\""
${TEST_RUNNER} ${QUIET:+-q} \
    -c "${RUNFILES}" \
    -T "${TAGS}" \
    -i "${STF_SUITE}" \
    -I "${ITERATIONS}" \
    2>&1 | tee "$RESULTS_FILE"
#
# Analyze the results.
#
${ZTS_REPORT} ${RERUN:+--no-maybes} "$RESULTS_FILE" >"$REPORT_FILE"
RESULT=$?

if [ "$RESULT" -eq "2" ] && [ -n "$RERUN" ]; then
	MAYBES="$($ZTS_REPORT --list-maybes)"
	TEMP_RESULTS_FILE=$(mktemp_file zts-results-tmp)
	TEST_LIST=$(mktemp_file test-list)
	grep "^Test:.*\[FAIL\]" "$RESULTS_FILE" >"$TEMP_RESULTS_FILE"
	for test_name in $MAYBES; do
		grep "$test_name " "$TEMP_RESULTS_FILE" >>"$TEST_LIST"
	done
	${TEST_RUNNER} ${QUIET:+-q} \
	    -c "${RUNFILES}" \
	    -T "${TAGS}" \
	    -i "${STF_SUITE}" \
	    -I "${ITERATIONS}" \
	    -l "${TEST_LIST}" \
	    2>&1 | tee "$RESULTS_FILE"
	#
	# Analyze the results.
	#
	${ZTS_REPORT} --no-maybes "$RESULTS_FILE" >"$REPORT_FILE"
	RESULT=$?
fi


cat "$REPORT_FILE"

RESULTS_DIR=$(awk '/^Log directory/ { print $3 }' "$RESULTS_FILE")
if [ -d "$RESULTS_DIR" ]; then
	cat "$RESULTS_FILE" "$REPORT_FILE" >"$RESULTS_DIR/results"
fi

rm -f "$RESULTS_FILE" "$REPORT_FILE"

if [ -n "$SINGLETEST" ]; then
	rm -f "$RUNFILES" >/dev/null 2>&1
fi

exit ${RESULT}
