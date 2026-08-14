#!/usr/bin/env bash
# SPDX-License-Identifier: CDDL-1.0
# shellcheck disable=SC2154
# shellcheck disable=SC2292
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2020 OmniOS Community Edition (OmniOSce) Association.
#

SCRIPT_COMMON=${SCRIPT_COMMON:-${0%/*}/common.sh}
. "${SCRIPT_COMMON}" || exit

PROG=zfs-tests.sh
VERBOSE="no"
QUIET=""
DEBUG=""
CLEANUP="yes"
CLEANUPALL="no"
KMSG=""
TIMEOUT_DEBUG=""
LOOPBACK="yes"
STACK_TRACER="no"
FILESIZE="4G"
UNAME=$(uname)
if [ "$UNAME" = "NetBSD" ]; then
	# NetBSD runfile is self-contained; do not also load common.run.
	DEFAULT_RUNFILES="netbsd.run"
else
	DEFAULT_RUNFILES="common.run,$(echo "$UNAME" | tr '[:upper:]' '[:lower:]').run"
fi
RUNFILES=${RUNFILES:-$DEFAULT_RUNFILES}
FILEDIR=${FILEDIR:-/var/tmp}
DISKS=${DISKS:-""}
SINGLETEST=""
SINGLETESTUSER="root"
TAGS=""
ITERATIONS=1
ZFS_DBGMSG="$STF_SUITE/callbacks/zfs_dbgmsg.ksh"
ZFS_DMESG="$STF_SUITE/callbacks/zfs_dmesg.ksh"
RERUN=""
KMEMLEAK=""

# Override some defaults if on FreeBSD
if [ "$UNAME" = "FreeBSD" ] ; then
	TESTFAIL_CALLBACKS=${TESTFAIL_CALLBACKS:-"$ZFS_DMESG"}
	LOSETUP=/sbin/mdconfig
	DMSETUP=/sbin/gpart
elif [ "$UNAME" = "NetBSD" ] ; then
	# File vdevs; NetBSD has no losetup.  vnd(4) is optional later.
	TESTFAIL_CALLBACKS=${TESTFAIL_CALLBACKS:-"$ZFS_DMESG"}
	LOOPBACK="no"
	LOSETUP=/sbin/vndconfig
	DMSETUP=/usr/bin/true
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
		    awk -v l="${LOOP_DEV}" '$0 ~ l {print $1}')

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
		elif [ "$UNAME" = "NetBSD" ] ; then
			:
		else
			cleanup_linux_loopback
		fi
	fi

	# shellcheck disable=SC2086
	rm -f ${FILES} >/dev/null 2>&1

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
	TEST_POOLS=$(ASAN_OPTIONS=detect_leaks=false "$ZPOOL" list -Ho name | grep testpool)
	if [ "$UNAME" = "FreeBSD" ] ; then
		TEST_LOOPBACKS=$(sudo "${LOSETUP}" -l)
	elif [ "$UNAME" = "NetBSD" ] ; then
		TEST_LOOPBACKS=""
	else
		TEST_LOOPBACKS=$("${LOSETUP}" -a | awk -F: '/file-vdev/ {print $1}')
	fi
	TEST_FILES=$(ls "${FILEDIR}"/file-vdev* 2>/dev/null)

	msg
	msg "--- Cleanup ---"
	# shellcheck disable=2116,2086
	msg "Removing pool(s):     $(echo ${TEST_POOLS})"
	for TEST_POOL in $TEST_POOLS; do
		sudo env ASAN_OPTIONS=detect_leaks=false "$ZPOOL" destroy "${TEST_POOL}"
	done

	if [ "$UNAME" != "FreeBSD" ] && [ "$UNAME" != "NetBSD" ] ; then
		msg "Removing all dm(s):   $(sudo "${DMSETUP}" ls |
		    grep loop | tr '\n' ' ')"
		sudo "${DMSETUP}" remove_all
	fi

	# shellcheck disable=2116,2086
	msg "Removing loopback(s): $(echo ${TEST_LOOPBACKS})"
	for TEST_LOOPBACK in $TEST_LOOPBACKS; do
		if [ "$UNAME" = "FreeBSD" ] ; then
			sudo "${LOSETUP}" -d -u "${TEST_LOOPBACK}"
		elif [ "$UNAME" = "NetBSD" ] ; then
			:
		else
			sudo "${LOSETUP}" -d "${TEST_LOOPBACK}"
		fi
	done

	# shellcheck disable=2116,2086
	msg "Removing files(s):    $(echo ${TEST_FILES})"
	# shellcheck disable=2086
	sudo rm -f ${TEST_FILES}
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

	if [ -f "$RUNFILE_DIR/$NAME" ]; then
		echo "$RUNFILE_DIR/$NAME"
	elif [ -f "$RUNFILE_DIR/$NAME.run" ]; then
		echo "$RUNFILE_DIR/$NAME.run"
	elif [ -f "$NAME" ]; then
		echo "$NAME"
	elif [ -f "$NAME.run" ]; then
		echo "$NAME.run"
	else
		return 1
	fi
}

# Given a TAGS with a format like "1/3" or "2/3" then divide up the test list
# into portions and print that portion.  So "1/3" for "the first third of the
# test tags".
#
#
split_tags() {
	# Get numerator and denominator
	NUM=$(echo "$TAGS" | cut -d/ -f1)
	DEN=$(echo "$TAGS" | cut -d/ -f2)
	# At the point this is called, RUNFILES will contain a comma separated
	# list of full paths to the runfiles, like:
	#
	# "/home/hutter/qemu/tests/runfiles/common.run,/home/hutter/qemu/tests/runfiles/linux.run"
	#
	# So to get tags for our selected tests we do:
	#
	# 1. Remove unneeded chars: [],\
	# 2. Print out the last field of each tag line.  This will be the tag
	#    for the test (like 'zpool_add').
	# 3. Remove duplicates between the runfiles.  If the same tag is defined
	#    in multiple runfiles, then when you do '-T <tag>' ZTS is smart
	#    enough to know to run the tag in each runfile.  So '-T zpool_add'
	#    will run the zpool_add from common.run and linux.run.
	# 4. Ignore the 'functional' tag since we only want individual tests
	# 5. Print out the tests in our faction of all tests.  This uses modulus
	#    so "1/3" will run tests 1,3,6,9 etc.  That way the tests are
	#    interleaved so, say, "3/4" isn't running all the zpool_* tests that
	#    appear alphabetically at the end.
	# 6. Remove trailing comma from list
	#
	# TAGS will then look like:
	#
	# "append,atime,bootfs,cachefile,checksum,cp_files,deadman,dos_attributes, ..."

	# Change the comma to a space for easy processing
	_RUNFILES=${RUNFILES//","/" "}
	# shellcheck disable=SC2002,SC2086
	cat $_RUNFILES | tr -d "[],\'" | awk '/tags = /{print $NF}' | sort | \
		uniq | grep -v functional | \
		awk -v num="$NUM" -v den="$DEN" '{ if(NR % den == (num - 1)) {printf "%s,",$0}}' | \
		sed -E 's/,$//'
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

	SYSTEM_FILES="$SYSTEM_FILES_COMMON"
	ZFSTEST_FILES="$ZFSTEST_FILES_COMMON"
	if [ "$UNAME" = "FreeBSD" ] ; then
		SYSTEM_FILES="$SYSTEM_FILES $SYSTEM_FILES_FREEBSD"
		ZFSTEST_FILES="$ZFSTEST_FILES $ZFSTEST_FILES_FREEBSD"
	elif [ "$UNAME" = "NetBSD" ] ; then
		SYSTEM_FILES="$SYSTEM_FILES $SYSTEM_FILES_NETBSD"
		ZFSTEST_FILES="$ZFSTEST_FILES $ZFSTEST_FILES_NETBSD"
		# pkgsrc OpenZFS must beat stock /sbin/zfs
		SYSTEM_DIRS="/usr/pkg/bin /usr/pkg/sbin $SYSTEM_DIRS"
	else
		SYSTEM_FILES="$SYSTEM_FILES $SYSTEM_FILES_LINUX"
		ZFSTEST_FILES="$ZFSTEST_FILES $ZFSTEST_FILES_LINUX"
	fi

	if [ "$INTREE" = "yes" ]; then
		# Constrained path set to $(top_builddir)/tests/zfs-tests/bin
		STF_PATH="$BIN_DIR"
		STF_PATH_REMOVE="no"
		STF_MISSING_BIN=""
		if [ ! -d "$STF_PATH" ]; then
			mkdir "$STF_PATH"
			chmod 755 "$STF_PATH" || fail "Couldn't chmod $STF_PATH"
		fi

		# Special case links for standard zfs utilities
		create_links "$CMD_DIR" "$ZFS_FILES"

		# Special case links for zfs test suite utilities
		create_links "$CMD_DIR/tests/zfs-tests/cmd" "$ZFSTEST_FILES"
	else
		# Constrained path set to $FILEDIR/constrained_path.*
		SYSTEMDIR=${SYSTEMDIR:-$FILEDIR/constrained_path.XXXXXX}
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
	create_links "$SYSTEM_DIRS" "$SYSTEM_FILES"

	# Exceptions
	if [ "$UNAME" = "Linux" ] ; then
		ln -fs /sbin/fsck.ext4 "$STF_PATH/fsck"
		ln -fs /sbin/mkfs.ext4 "$STF_PATH/newfs"
		ln -fs "$STF_PATH/gzip" "$STF_PATH/compress"
		ln -fs "$STF_PATH/gunzip" "$STF_PATH/uncompress"
	elif [ "$UNAME" = "FreeBSD" ] ; then
		ln -fs /usr/local/bin/ksh93 "$STF_PATH/ksh"
	elif [ "$UNAME" = "NetBSD" ] ; then
		if [ -x /usr/pkg/bin/ksh93 ]; then
			ln -fs /usr/pkg/bin/ksh93 "$STF_PATH/ksh"
		elif [ -x /usr/local/bin/ksh93 ]; then
			ln -fs /usr/local/bin/ksh93 "$STF_PATH/ksh"
		else
			fail "ksh93 is required on NetBSD (pkgsrc shells/ksh93). /bin/ksh is pdksh and cannot run ZTS."
		fi
		# GNU timeout is pkgsrc gtimeout; ZTS scripts call timeout(1).
		if [ -x /usr/pkg/bin/gtimeout ]; then
			ln -fs /usr/pkg/bin/gtimeout "$STF_PATH/timeout"
		elif [ -x /usr/pkg/bin/timeout ]; then
			ln -fs /usr/pkg/bin/timeout "$STF_PATH/timeout"
		fi
		# Stock NetBSD /usr/sbin/{zfs,zpool,zdb} cannot spa_open
		# an OpenZFS-kmod pool.  Drop those links so a missing
		# port binary is an honest fail, not EINVAL from stock zdb.
		for n in zdb zfs zpool zhack ztest zstream zinject \
		    zfs_ids_to_path zstreamdump; do
			tgt=$(readlink "$STF_PATH/$n" 2>/dev/null) || continue
			case $tgt in
			/sbin/*|/usr/sbin/*)
				rm -f "$STF_PATH/$n"
				;;
			esac
		done
		# NetBSD dd(1) only accepts lowercase size suffixes
		# (1m/1k/1g).  Tests were converted; do not install a
		# wrapper over STF_PATH/dd -- cp(1) follows a symlink
		# to /bin/dd and replaces the system binary.
		# CURRENT dd (iflags=fullblock) is /usr/pkg/bin/dd.
		if [ -x /usr/pkg/bin/dd ]; then
			ln -fs /usr/pkg/bin/dd "$STF_PATH/dd"
		fi
		# mount(8) execs /sbin/mount_XXX, not PATH.
		# zgenhostid is built for NetBSD too; link it into
		# STF_PATH so create_links' missing-bin list is honest.
		for _n in mount_zfs zgenhostid; do
			for _p in /usr/pkg/sbin /usr/pkg/bin \
			    /usr/local/sbin /usr/local/bin; do
				if [ -x "$_p/$_n" ]; then
					[ "$_n" = mount_zfs ] && \
					    ln -fs "$_p/$_n" /sbin/mount_zfs
					ln -fs "$_p/$_n" "$STF_PATH/$_n"
					break
				fi
			done
		done
		# Illumos lockfs -f flushes a FS.  NetBSD has no
		# lockfs(8); mount -u syncs that mount, then sync(8).
		cat > "$STF_PATH/lockfs" <<'LOCKFS'
#!/bin/sh
mnt=
while [ $# -gt 0 ]; do
	case $1 in
	-f) shift ;;
	*) mnt=$1; shift ;;
	esac
done
if [ -n "$mnt" ] && [ -d "$mnt" ]; then
	mount -u "$mnt" >/dev/null 2>&1 || true
fi
sync
exit 0
LOCKFS
		chmod 755 "$STF_PATH/lockfs"
		# lofiadm(1M) -> vndconfig(8)
		cat > "$STF_PATH/lofiadm" <<'LOFI'
#!/bin/sh
set -e
op=$1
shift
case $op in
-a)
	file=$1
	u=0
	while [ $u -le 7 ]; do
		if vnconfig -l "vnd$u" 2>/dev/null | grep -q "not in use"; then
			vnconfig "vnd$u" "$file"
			if [ -c /dev/rvnd${u}d ]; then
				echo /dev/rvnd${u}d
			elif [ -c /dev/rvnd$u ]; then
				echo /dev/rvnd$u
			else
				echo /dev/vnd$u
			fi
			exit 0
		fi
		u=$((u + 1))
	done
	exit 1
	;;
-d)
	dev=${1##*/}
	dev=${dev#r}
	dev=${dev%%[a-p]}
	vnconfig -u "$dev"
	;;
*)
	exit 1
	;;
esac
LOFI
		chmod 755 "$STF_PATH/lofiadm"
		# Extra helpers above are linked after create_links
		# already recorded missing names.  Drop any that now
		# exist so "Missing util(s)" is honest.
		if [ -n "$STF_MISSING_BIN" ]; then
			_keep=
			for i in $STF_MISSING_BIN; do
				[ -e "$STF_PATH/$i" ] && continue
				_keep="$_keep $i"
			done
			STF_MISSING_BIN=${_keep# }
		fi
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
	-D          Debug; show all test output immediately (noisy)
	-x          Remove all testpools, dm, lo, and files (unsafe)
	-k          Disable cleanup after test failure
	-K          Log test names to /dev/kmsg
	-f          Use files only, disables block device tests
	-O          Dump debugging info to /dev/kmsg on test timeout
	-S          Enable stack tracer (negative performance impact)
	-c          Only create and populate constrained path
	-R          Automatically rerun failing tests
	-m          Enable kmemleak reporting (Linux only)
	-n NFSFILE  Use the nfsfile to determine the NFS configuration
	-I NUM      Number of iterations
	-d DIR      Use world-writable DIR for files and loopback devices
	-s SIZE     Use vdevs of SIZE (default: 4G)
	-r RUNFILES Run tests in RUNFILES (default: ${DEFAULT_RUNFILES})
	-t PATH|NAME  Run single test at PATH relative to test suite,
	                or search for test by NAME
	-T TAGS     Comma separated list of tags (default: 'functional')
	            Alternately, specify a fraction like "1/3" or "2/3" to
		     run the first third of tests or 2nd third of the tests.  This
		     is useful for splitting up the test amongst different
		     runners.
	-u USER     Run single test as USER (default: root)

EXAMPLES:
# Run the default ${DEFAULT_RUNFILES//\.run/} suite of tests and output the configuration used.
$0 -v

# Run a smaller suite of tests designed to run more quickly.
$0 -r linux-fast

# Run a single test
$0 -t tests/functional/cli_root/zfs_bookmark/zfs_bookmark_cliargs.ksh

# Run a single test by name
$0 -t zfs_bookmark_cliargs

# Cleanup a previous run of the test suite prior to testing, run the
# default ${DEFAULT_RUNFILES//\.run//} suite of tests and perform no cleanup on exit.
$0 -x

EOF
}

while getopts 'hvqxkKfScRmOn:d:Ds:r:?t:T:u:I:' OPTION; do
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
	K)
		KMSG="yes"
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
	m)
		KMEMLEAK="yes"
		;;
	n)
		nfsfile=$OPTARG
		[ -f "$nfsfile" ] || fail "Cannot read file: $nfsfile"
		export NFS=1
		. "$nfsfile"
		;;
	O)
		TIMEOUT_DEBUG="yes"
		;;
	d)
		FILEDIR="$OPTARG"
		;;
	D)
		DEBUG="yes"
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
	*)
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
	RUNFILE_DIR="$FILEDIR"
	RUNFILES="zfs-tests.$$.run"
	[ -n "$QUIET" ] && SINGLEQUIET="True" || SINGLEQUIET="False"

	cat >"${RUNFILE_DIR}/${RUNFILES}" << EOF
[DEFAULT]
pre =
quiet = $SINGLEQUIET
pre_user = root
user = $SINGLETESTUSER
timeout = 600
post_user = root
post =
EOF
	if [ "$SINGLETEST" = "${SINGLETEST%/*}" ] ; then
		if [ "$UNAME" = "NetBSD" ] ; then
			NEWSINGLETEST=$(find "$STF_SUITE" -name "$SINGLETEST*" -print | head -1)
		else
			NEWSINGLETEST=$(find "$STF_SUITE" -name "$SINGLETEST*" -print -quit)
		fi
		if [ -z "$NEWSINGLETEST" ] ; then
			fail "couldn't find test matching '$SINGLETEST'"
		fi
		SINGLETEST=$NEWSINGLETEST
	fi

	SINGLETESTDIR="${SINGLETEST%/*}"
	SETUPDIR="$SINGLETESTDIR"
	[ "${SETUPDIR#/}" = "$SETUPDIR" ] && SETUPDIR="$STF_SUITE/$SINGLETESTDIR"
	[ -x "$SETUPDIR/setup.ksh"   ] && SETUPSCRIPT="setup"     || SETUPSCRIPT=
	[ -x "$SETUPDIR/cleanup.ksh" ] && CLEANUPSCRIPT="cleanup" || CLEANUPSCRIPT=

	SINGLETESTFILE="${SINGLETEST##*/}"
	cat >>"${RUNFILE_DIR}/${RUNFILES}" << EOF

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
		RUNFILE=$(find_runfile "$RUNFILE") ||
			fail "Cannot find runfile: $SAVED_RUNFILE"
		R="$R,$RUNFILE"
	fi

	if [ ! -r "$RUNFILE" ]; then
		fail "Cannot read runfile: $RUNFILE"
	fi
done
unset IFS
RUNFILES=${R#,}

# The tag can be a fraction to indicate which portion of ZTS to run, Like
#
# 	"1/3": Run first one third of all tests in runfiles
#	"2/3": Run second one third of all test in runfiles
#	"6/10": Run 6th tenth of all tests in runfiles
#
# This is useful for splitting up the test across multiple runners.
#
# After this code block, TAGS will be transformed from something like
# "1/3" to a comma separate taglist, like:
#
# "append,atime,bootfs,cachefile,checksum,cp_files,deadman,dos_attributes, ..."
#
if echo "$TAGS" | grep -Eq '^[0-9]+/[0-9]+$' ; then
	TAGS=$(split_tags)
fi

#
# This script should not be run as root.  Instead the test user, which may
# be a normal user account, needs to be configured such that it can
# run commands via sudo passwordlessly.
#
if [ "$(id -u)" = "0" ]; then
	fail "This script must not be run as root."
fi

if [ "$(sudo id -un)" != "root" ]; then
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
elif [ "$UNAME" = "NetBSD" ]; then
	ksh93=
	for c in /usr/pkg/bin/ksh93 /usr/local/bin/ksh93; do
		if [ -x "$c" ]; then
			ksh93=$c
			break
		fi
	done
	if [ -z "$ksh93" ]; then
		fail "ksh93 is required on NetBSD; refusing to use pdksh /bin/ksh"
	fi
	# Confine the ksh93 link to STF_PATH.  Do not replace the
	# developer's /bin/ksh (pdksh).  QEMU images still get the
	# /bin/ksh link from qemu-3-deps-vm.sh.
	ln -sfn "$ksh93" "$STF_PATH/ksh"
	ln -sfn "$ksh93" "$STF_PATH/ksh93"
	export STF_KSH93="$ksh93"
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
#
if [ -z "${KEEP}" ]; then
	KEEP="$(ASAN_OPTIONS=detect_leaks=false "$ZPOOL" list -Ho name | tr -s '[:space:]' ' ')"
	if [ -z "${KEEP}" ]; then
		KEEP="rpool"
	fi
else
	KEEP="$(echo "$KEEP" | tr -s '[:space:]' ' ')"
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
__ZFS_POOL_EXCLUDE="$KEEP"

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
				TEST_LOOPBACK=$(sudo "${LOSETUP}" --show -f "${TEST_FILE}") ||
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
# NetBSD kernel labels store the character node (/dev/rldN).  Tests and
# DISKS still often say ldN.  Collapse to the raw leaf so zpool create,
# zpool status, check_state and vdevs_in_pool agree.  File vdevs stay
# as given.
#
if [ "$UNAME" = "NetBSD" ]; then
	_ndisks=""
	for _d in $DISKS; do
		_b=${_d##*/}
		if [ -f "$_d" ] && [ ! -b "$_d" ] && [ ! -c "$_d" ]; then
			_ndisks="$_ndisks $_d"
			continue
		fi
		# Strip a raw 'r' only on known disk classes (not raid0→aid0).
		case $_b in
		rld[0-9]*|rwd[0-9]*|rsd[0-9]*|rdk[0-9]*|rvnd[0-9]*|\
		rraid[0-9]*|rccd[0-9]*|rcgd[0-9]*|rxbd[0-9]*|rmd[0-9]*)
			_b=${_b#r}
			;;
		esac
		case $_b in
		ld[0-9]*|wd[0-9]*|sd[0-9]*|dk[0-9]*|vnd[0-9]*|ccd[0-9]*|\
		cgd[0-9]*|xbd[0-9]*|md[0-9]*|raid[0-9]*)
			if [ -c "/dev/r${_b}" ]; then
				_d="r${_b}"
			elif [ -b "/dev/${_b}" ]; then
				_d="$_b"
			fi
			;;
		esac
		_ndisks="$_ndisks $_d"
	done
	DISKS=${_ndisks# }
	unset _ndisks _d _b
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
if command -v setenforce >/dev/null; then
	sudo setenforce permissive >/dev/null 2>&1
fi

#
# Enable internal ZFS debug log and clear it.
#
if [ -e /sys/module/zfs/parameters/zfs_dbgmsg_enable ]; then
	sudo sh -c "echo 1 >/sys/module/zfs/parameters/zfs_dbgmsg_enable"
	sudo sh -c "echo 0 >/proc/spl/kstat/zfs/dbgmsg"
fi

#
# Set TMPDIR. Some tests run mktemp, and we want those files contained to
# the work dir the same as any other.
#
export TMPDIR="$FILEDIR"

msg
msg "--- Configuration ---"
msg "Runfiles:        $RUNFILES"
msg "STF_TOOLS:       $STF_TOOLS"
msg "STF_SUITE:       $STF_SUITE"
msg "STF_PATH:        $STF_PATH"
msg "FILEDIR:         $FILEDIR"
msg "TMPDIR:          $TMPDIR"
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

mktemp_file() {
	if [ "$UNAME" = "FreeBSD" ] || [ "$UNAME" = "NetBSD" ]; then
		mktemp -u "${FILEDIR}/$1.XXXXXX"
	else
		mktemp -ut "$1.XXXXXX" -p "$FILEDIR"
	fi
}
mkdir -p "$FILEDIR" || :

#
# NetBSD: ZFS mountpoints must not sit on the same FFS as file vdevs
# or mount(2) deadlocks.  default.cfg uses /mnt/zts.  A vnd-backed
# FFS image under FILEDIR keeps mountpoints off both the live-image
# root and the file-vdev filesystem.
#
netbsd_prepare_mntroot() {
	local root img need vnd sz u
	[ "$UNAME" = "NetBSD" ] || return 0
	root=/mnt/zts
	img="${FILEDIR:-/var/tmp}/zts-mntroot.img"
	need=$((8 * 1024 * 1024 * 1024))
	vnd= sz=0
	mkdir -p "$root" "$(dirname "$img")"
	if mount | grep -q " on ${root} "; then
		return 0
	fi
	for u in 3 2 1 0; do
		if /usr/sbin/vnconfig -l "vnd$u" 2>/dev/null | \
		    grep -q "not in use"; then
			vnd="vnd$u"
			break
		fi
	done
	[ -n "$vnd" ] || return 1
	[ -f "$img" ] && sz=$(stat -f %z "$img" 2>/dev/null || echo 0)
	if [ ! -f "$img" ] || [ "$sz" -lt "$need" ]; then
		dd if=/dev/zero of="$img" bs=1 count=1 seek=$((need - 1)) \
		    2>/dev/null || return 1
		/usr/sbin/vnconfig "$vnd" "$img" || return 1
		/sbin/newfs "/dev/r$vnd" >/dev/null 2>&1 || \
		    /sbin/newfs "/dev/r${vnd}c" >/dev/null 2>&1 || {
			/usr/sbin/vnconfig -u "$vnd" 2>/dev/null || true
			return 1
		}
	else
		/usr/sbin/vnconfig "$vnd" "$img" || return 1
	fi
	if /sbin/mount "/dev/$vnd" "$root" 2>/dev/null || \
	    /sbin/mount "/dev/${vnd}c" "$root" 2>/dev/null; then
		mkdir -p "$root/testdir" "$root/testdir0" \
		    "$root/testdir1" "$root/testdir2"
		return 0
	fi
	/sbin/newfs "/dev/r$vnd" >/dev/null 2>&1 || \
	    /sbin/newfs "/dev/r${vnd}c" >/dev/null 2>&1 || {
		/usr/sbin/vnconfig -u "$vnd" 2>/dev/null || true
		return 1
	}
	if /sbin/mount "/dev/$vnd" "$root" 2>/dev/null || \
	    /sbin/mount "/dev/${vnd}c" "$root" 2>/dev/null; then
		mkdir -p "$root/testdir" "$root/testdir0" \
		    "$root/testdir1" "$root/testdir2"
		return 0
	fi
	/usr/sbin/vnconfig -u "$vnd" 2>/dev/null || true
	return 1
}
if [ "$UNAME" = "NetBSD" ]; then
	netbsd_prepare_mntroot || \
	    echo "WARNING: /mnt/zts still on root; grow the live image" \
	    "or file-vdev tests may ENOSPC" >&2
	mkdir -p /mnt/zts/testdir /mnt/zts/testdir0 \
	    /mnt/zts/testdir1 /mnt/zts/testdir2
fi

RESULTS_FILE=$(mktemp_file zts-results)
REPORT_FILE=$(mktemp_file zts-report)

#
# Run all the tests as specified.
#
msg "${TEST_RUNNER}" \
    "${QUIET:+-q}" \
    "${DEBUG:+-D}" \
    "${KMEMLEAK:+-m}" \
    "${KMSG:+-K}" \
    "${TIMEOUT_DEBUG:+-O}" \
    "-c \"${RUNFILES}\"" \
    "-T \"${TAGS}\"" \
    "-i \"${STF_SUITE}\"" \
    "-I \"${ITERATIONS}\""
{ PATH=$STF_PATH \
    ${TEST_RUNNER} \
    ${QUIET:+-q} \
    ${DEBUG:+-D} \
    ${KMEMLEAK:+-m} \
    ${KMSG:+-K} \
    ${TIMEOUT_DEBUG:+-O} \
    -c "${RUNFILES}" \
    -T "${TAGS}" \
    -i "${STF_SUITE}" \
    -I "${ITERATIONS}" \
    2>&1; echo $? >"$REPORT_FILE"; } | tee "$RESULTS_FILE"
read -r RUNRESULT <"$REPORT_FILE"

if [[ "$RUNRESULT" -eq "255" ]] ; then
    fail "$TEST_RUNNER failed, test aborted."
fi 

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
	{ PATH=$STF_PATH \
	    ${TEST_RUNNER} \
	        ${QUIET:+-q} \
	        ${DEBUG:+-D} \
	        ${KMEMLEAK:+-m} \
	    -c "${RUNFILES}" \
	    -T "${TAGS}" \
	    -i "${STF_SUITE}" \
	    -I "${ITERATIONS}" \
	    -l "${TEST_LIST}" \
	    2>&1; echo $? >"$REPORT_FILE"; } | tee "$RESULTS_FILE"
	read -r RUNRESULT <"$REPORT_FILE"
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

rm -f "$RESULTS_FILE" "$REPORT_FILE" "$TEST_LIST" "$TEMP_RESULTS_FILE"

if [ -n "$SINGLETEST" ]; then
	rm -f "$RUNFILES" >/dev/null 2>&1
fi

[ "$RUNRESULT" -gt 3 ] && exit "$RUNRESULT" || exit "$RESULT"
