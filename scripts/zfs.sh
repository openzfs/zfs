#!/bin/bash
#
# A simple script to simply the loading/unloading the ZFS module
# stack.  It should probably be considered a first step towards
# a full ZFS init script when that is needed.
#

. ./common.sh
PROG=zfs.sh

KMOD=/lib/modules/${KERNELSRCVER}/kernel
KERNEL_MODULES=(				\
	$KMOD/lib/zlib_deflate/zlib_deflate.ko	\
)

SPL_MODULES=(					\
	${SPLBUILD}/module/spl/spl.ko		\
)

ZFS_MODULES=(					\
	${MODDIR}/avl/zavl.ko			\
	${MODDIR}/nvpair/znvpair.ko		\
	${MODDIR}/unicode/zunicode.ko		\
	${MODDIR}/zcommon/zcommon.ko		\
	${MODDIR}/zfs/zfs.ko			\
)

MODULES=(					\
	${KERNEL_MODULES[*]}			\
	${SPL_MODULES[*]}			\
	${ZFS_MODULES[*]}			\
)

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

UNLOAD=

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
