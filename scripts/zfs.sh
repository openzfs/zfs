#!/bin/bash
#
# A simple script to simply the loading/unloading the ZFS module stack.

SCRIPT_COMMON=common.sh
if [ -f ./${SCRIPT_COMMON} ]; then
. ./${SCRIPT_COMMON}
elif [ -f /usr/libexec/zfs/${SCRIPT_COMMON} ]; then
. /usr/libexec/zfs/${SCRIPT_COMMON}
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

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
	-d      Save debug log on unload

MODULE-OPTIONS:
	Must be of the from module="options", for example:

$0 zfs="zfs_prefetch_disable=1"
$0 zfs="zfs_prefetch_disable=1 zfs_mdcomp_disable=1"
$0 spl="spl_debug_mask=0"

EOF
}

while getopts 'hvud' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	u)
		UNLOAD=1
		;;
	d)
		DUMP_LOG=1
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

if [ ${UNLOAD} ]; then
	unload_modules
else
	check_modules || die "${ERROR}"
	load_modules "$@"
fi

exit 0
