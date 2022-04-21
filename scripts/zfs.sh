#!/bin/sh
#
# A simple script to load/unload the ZFS module stack.
#

BASE_DIR=${0%/*}
SCRIPT_COMMON=common.sh
if [ -f "${BASE_DIR}/${SCRIPT_COMMON}" ]; then
	. "${BASE_DIR}/${SCRIPT_COMMON}"
else
	echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

VERBOSE="no"
UNLOAD="no"
LOAD="yes"
STACK_TRACER="no"

ZED_PIDFILE=${ZED_PIDFILE:-/var/run/zed.pid}
LDMOD=${LDMOD:-/sbin/modprobe}
DELMOD=${DELMOD:-/sbin/rmmod}

KMOD_ZLIB_DEFLATE=${KMOD_ZLIB_DEFLATE:-zlib_deflate}
KMOD_ZLIB_INFLATE=${KMOD_ZLIB_INFLATE:-zlib_inflate}
KMOD_SPL=${KMOD_SPL:-spl}
KMOD_ZFS=${KMOD_ZFS:-zfs}
KMOD_FREEBSD=${KMOD_FREEBSD:-openzfs}


usage() {
	cat << EOF
USAGE:
$0 [hvudS]

DESCRIPTION:
	Load/unload the ZFS module stack.

OPTIONS:
	-h	Show this message
	-v	Verbose
	-r	Reload modules
	-u	Unload modules
	-S	Enable kernel stack tracer
EOF
	exit 1
}

while getopts 'hvruS' OPTION; do
	case $OPTION in
	v)
		VERBOSE="yes"
		;;
	r)
		UNLOAD="yes"
		LOAD="yes"
		;;
	u)
		UNLOAD="yes"
		LOAD="no"
		;;
	S)
		STACK_TRACER="yes"
		;;
	*)
		usage
		;;
	esac
done
shift $(( OPTIND - 1 ))
[ $# -eq 0 ] || usage

kill_zed() {
	if [ -f "$ZED_PIDFILE" ]; then
		read -r PID <"$ZED_PIDFILE"
		kill "$PID"
	fi
}

check_modules_linux() {
	LOADED_MODULES=""
	MISSING_MODULES=""

	for KMOD in $KMOD_SPL $KMOD_ZFS; do
		NAME="${KMOD##*/}"
		NAME="${NAME%.ko}"

		if lsmod | grep -E -q "^${NAME}"; then
			LOADED_MODULES="$LOADED_MODULES\t$NAME\n"
		fi

		if ! modinfo "$KMOD" >/dev/null 2>&1; then
			MISSING_MODULES="$MISSING_MODULES\t${KMOD}\n"
		fi
	done

	if [ -n "$LOADED_MODULES" ]; then
		printf "Unload the kernel modules by running '%s -u':\n" "$0"
		printf "%b" "$LOADED_MODULES"
		exit 1
	fi

	if [ -n "$MISSING_MODULES" ]; then
		printf "The following kernel modules can not be found:\n"
		printf "%b" "$MISSING_MODULES"
		exit 1
	fi

	return 0
}

load_module_linux() {
	KMOD=$1

	FILE=$(modinfo "$KMOD" 2>&1 | awk 'NR == 1 && /zlib/ && /not found/ {print "(builtin)"; exit}  /^filename:/ {print $2}')
	[ "$FILE" = "(builtin)" ] && return

	if [ "$VERBOSE" = "yes" ]; then
		VERSION=$(modinfo "$KMOD" | awk '/^version:/ {print $2}')
		echo "Loading: $FILE ($VERSION)"
	fi

	if ! $LDMOD "$KMOD" >/dev/null 2>&1; then
		echo "Failed to load $KMOD"
		return 1
	fi

	return 0
}

load_modules_freebsd() {
	kldload "$KMOD_FREEBSD" || return 1

	if [ "$VERBOSE" = "yes" ]; then
		echo "Successfully loaded ZFS module stack"
	fi

	return 0
}

load_modules_linux() {
	mkdir -p /etc/zfs

	for KMOD in "$KMOD_ZLIB_DEFLATE" "$KMOD_ZLIB_INFLATE" $KMOD_SPL $KMOD_ZFS; do
		load_module_linux "$KMOD" || return 1
	done

	if [ "$VERBOSE" = "yes" ]; then
		echo "Successfully loaded ZFS module stack"
	fi

	return 0
}

unload_modules_freebsd() {
	kldunload "$KMOD_FREEBSD" || echo "Failed to unload $KMOD_FREEBSD"

	if [ "$VERBOSE" = "yes" ]; then
		echo "Successfully unloaded ZFS module stack"
	fi

	return 0
}

unload_modules_linux() {
	legacy_kmods="icp zzstd zlua zcommon zunicode znvpair zavl"
	for KMOD in "$KMOD_ZFS" $legacy_kmods "$KMOD_SPL"; do
		NAME="${KMOD##*/}"
		NAME="${NAME%.ko}"
		! [ -d "/sys/module/$NAME" ] || $DELMOD "$NAME" || return
	done

	if [ "$VERBOSE" = "yes" ]; then
		echo "Successfully unloaded ZFS module stack"
	fi
}

stack_clear_linux() {
	STACK_MAX_SIZE=/sys/kernel/debug/tracing/stack_max_size
	STACK_TRACER_ENABLED=/proc/sys/kernel/stack_tracer_enabled

	if [ "$STACK_TRACER" = "yes" ] && [ -e "$STACK_MAX_SIZE" ]; then
		echo 1 >"$STACK_TRACER_ENABLED"
		echo 0 >"$STACK_MAX_SIZE"
	fi
}

stack_check_linux() {
	STACK_MAX_SIZE=/sys/kernel/debug/tracing/stack_max_size
	STACK_TRACE=/sys/kernel/debug/tracing/stack_trace
	STACK_LIMIT=15362

	if [ -e "$STACK_MAX_SIZE" ]; then
		read -r STACK_SIZE <"$STACK_MAX_SIZE"
		if [ "$STACK_SIZE" -ge "$STACK_LIMIT" ]; then
			echo
			echo "Warning: max stack size $STACK_SIZE bytes"
			cat "$STACK_TRACE"
		fi
	fi
}

if [ "$(id -u)" != 0 ]; then
	echo "Must run as root"
	exit 1
fi

UNAME=$(uname)

if [ "$UNLOAD" = "yes" ]; then
	kill_zed
	umount -t zfs -a
	case $UNAME in
		FreeBSD)
		   unload_modules_freebsd
		   ;;
		Linux)
		   stack_check_linux
		   unload_modules_linux
		   ;;
		*)
		   echo "unknown system: $UNAME" >&2
		   exit 1
		   ;;
	esac
fi
if [ "$LOAD" = "yes" ]; then
	case $UNAME in
		FreeBSD)
		   load_modules_freebsd
		   ;;
		Linux)
		   stack_clear_linux
		   check_modules_linux
		   load_modules_linux
		   udevadm trigger
		   udevadm settle
		   ;;
		*)
		   echo "unknown system: $UNAME" >&2
		   exit 1
		   ;;
	esac
fi

exit 0
