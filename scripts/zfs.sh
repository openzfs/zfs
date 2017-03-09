#!/bin/bash
#
# A simple script to simply the loading/unloading the ZFS module stack.

basedir=$(dirname "$0")

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

# shellcheck disable=SC2034
PROG=zfs.sh
UNLOAD=

usage() {
cat << EOF
USAGE:
$0 [hvud] [module-options]

DESCRIPTION:
	Load/unload the ZFS module stack.

OPTIONS:
	-h      Show this message
	-v      Verbose
	-u      Unload modules

MODULE-OPTIONS:
	Must be of the from module="options", for example:

$0 zfs="zfs_prefetch_disable=1"
$0 zfs="zfs_prefetch_disable=1 zfs_mdcomp_disable=1"

EOF
}

while getopts 'hvu' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		# shellcheck disable=SC2034
		VERBOSE=1
		;;
	u)
		UNLOAD=1
		;;
	?)
		usage
		exit
		;;
	esac
done

if [ "$(id -u)" != 0 ]; then
	die "Must run as root"
fi

if [ ${UNLOAD} ]; then
	kill_zed
	umount -t zfs -a
	stack_check
	unload_modules
else
	stack_clear
	check_modules || die "${ERROR}"
	load_modules "$@" || die "Failed to load modules"
	wait_udev /dev/zfs 30 || die "'/dev/zfs' was not created"
fi

exit 0
