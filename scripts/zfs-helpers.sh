#!/bin/bash
#
# This script is designed to facilitate in-tree development and testing
# by installing symlinks on your system which refer to in-tree helper
# utilities.  These helper utilities must be installed to in order to
# exercise all ZFS functionality.  By using symbolic links and keeping
# the scripts in-tree during development they can be easily modified
# and those changes tracked.
#
# Use the following configuration option to override the installation
# paths for these scripts.  The correct path is automatically set for
# most distributions but you can optionally set it for your environment.
#
#   --with-mounthelperdir=DIR  install mount.zfs in dir [/sbin]
#   --with-udevdir=DIR         install udev helpers [default=check]
#   --with-udevruledir=DIR     install udev rules [default=UDEVDIR/rules.d]
#   --sysconfdir=DIR           install zfs configuration files [PREFIX/etc]
#

basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zfs-helpers.sh
DRYRUN=
INSTALL=
REMOVE=
VERBOSE=

usage() {
cat << EOF
USAGE:
$0 [dhirv]

DESCRIPTION:
	Install/remove the ZFS helper utilities.

OPTIONS:
	-d      Dry run
	-h      Show this message
	-i      Install the helper utilities
	-r      Remove the helper utilities
	-v      Verbose

$0 -iv
$0 -r

EOF
}

while getopts 'hdirv' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	d)
		DRYRUN=1
		;;
	i)
		INSTALL=1
		;;
	r)
		REMOVE=1
		;;
	v)
		VERBOSE=1
		;;
	?)
		usage
		exit
		;;
	esac
done

if [ "${INSTALL}" -a "${REMOVE}" ]; then
	usage
	die "Specify -i or -r but not both"
fi

if [ ! "${INSTALL}" -a ! "${REMOVE}" ]; then
	usage
	die "Either -i or -r must be specified"
fi

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

if [ "$VERBOSE" ]; then
	echo "--- Configuration ---"
	echo "udevdir:          $udevdir"
	echo "udevruledir:      $udevruledir"
	echo "mounthelperdir:   $mounthelperdir"
	echo "sysconfdir:	$sysconfdir"
	echo "DRYRUN:           $DRYRUN"
	echo
fi

install() {
	local src=$1
	local dst=$2

	if [ -h $dst ]; then
		echo "Symlink exists: $dst"
	elif [ -e $dst ]; then
		echo "File exists: $dst"
	elif [ ! -e $src ]; then
		echo "Source missing: $src"
	else
		msg "ln -s $src $dst"

		if [ ! "$DRYRUN" ]; then
			mkdir -p $(dirname $dst) &>/dev/null
			ln -s $src $dst
		fi
	fi
}

remove() {
	local dst=$1

	if [ -h $dst ]; then
		msg "rm $dst"
		rm $dst
		rmdir $(dirname $dst) &>/dev/null
	fi
}

if [ ${INSTALL} ]; then
	install $CMDDIR/mount_zfs/mount.zfs $mounthelperdir/mount.zfs
	install $CMDDIR/fsck_zfs/fsck.zfs $mounthelperdir/fsck.zfs
	install $CMDDIR/zvol_id/zvol_id $udevdir/zvol_id
	install $CMDDIR/vdev_id/vdev_id $udevdir/vdev_id
	install $UDEVRULEDIR/60-zvol.rules $udevruledir/60-zvol.rules
	install $UDEVRULEDIR/69-vdev.rules $udevruledir/69-vdev.rules
	install $UDEVRULEDIR/90-zfs.rules $udevruledir/90-zfs.rules
	install $CMDDIR/zpool/zpool.d $sysconfdir/zfs/zpool.d
else
	remove $mounthelperdir/mount.zfs
	remove $mounthelperdir/fsck.zfs
	remove $udevdir/zvol_id
	remove $udevdir/vdev_id
	remove $udevruledir/60-zvol.rules
	remove $udevruledir/69-vdev.rules
	remove $udevruledir/90-zfs.rules
	remove $sysconfdir/zfs/zpool.d
fi

exit 0
