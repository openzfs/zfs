#!/bin/sh
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

BASE_DIR=$(dirname "$0")
SCRIPT_COMMON=common.sh
if [ -f "${BASE_DIR}/${SCRIPT_COMMON}" ]; then
	. "${BASE_DIR}/${SCRIPT_COMMON}"
else
	echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zfs-helpers.sh
DRYRUN="no"
INSTALL="no"
REMOVE="no"
VERBOSE="no"

fail() {
	echo "${PROG}: $1" >&2
	exit 1
}

msg() {
	if [ "$VERBOSE" = "yes" ]; then
		echo "$@"
	fi
}

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
		DRYRUN="yes"
		;;
	i)
		INSTALL="yes"
		;;
	r)
		REMOVE="yes"
		;;
	v)
		VERBOSE="yes"
		;;
	?)
		usage
		exit
		;;
	esac
done

if [ "$INSTALL" = "yes" ] && [ "$REMOVE" = "yes" ]; then
	fail "Specify -i or -r but not both"
fi

if [ "$INSTALL" = "no" ] && [ "$REMOVE" = "no" ]; then
	fail "Either -i or -r must be specified"
fi

if [ "$(id -u)" != "0" ]; then
	fail "Must run as root"
fi

if [ "$INTREE" != "yes" ]; then
	fail "Must be run in-tree"
fi

if [ "$VERBOSE" = "yes" ]; then
	echo "--- Configuration ---"
	echo "udevdir:          $INSTALL_UDEV_DIR"
	echo "udevruledir:      $INSTALL_UDEV_RULE_DIR"
	echo "mounthelperdir:   $INSTALL_MOUNT_HELPER_DIR"
	echo "sysconfdir:       $INSTALL_SYSCONF_DIR"
	echo "pythonsitedir:    $INSTALL_PYTHON_DIR"
	echo "dryrun:           $DRYRUN"
	echo
fi

install() {
	src=$1
	dst=$2

	if [ -h "$dst" ]; then
		echo "Symlink exists: $dst"
	elif [ -e "$dst" ]; then
		echo "File exists: $dst"
	elif [ ! -e "$src" ]; then
		echo "Source missing: $src"
	else
		msg "ln -s $src $dst"

		if [ "$DRYRUN" = "no" ]; then
			DIR=$(dirname "$dst")
			mkdir -p "$DIR" >/dev/null 2>&1
			ln -s "$src" "$dst"
		fi
	fi
}

remove() {
	dst=$1

	if [ -h "$dst" ]; then
		msg "rm $dst"
		rm "$dst"
		DIR=$(dirname "$dst")
		rmdir "$DIR" >/dev/null 2>&1
	elif [ -e "$dst" ]; then
		echo "Expected symlink: $dst"
	fi
}

if [ "${INSTALL}" = "yes" ]; then
	install "$CMD_DIR/mount_zfs/mount.zfs" \
	    "$INSTALL_MOUNT_HELPER_DIR/mount.zfs"
	install "$CMD_DIR/fsck_zfs/fsck.zfs" \
	    "$INSTALL_MOUNT_HELPER_DIR/fsck.zfs"
	install "$CMD_DIR/zvol_id/zvol_id" \
	    "$INSTALL_UDEV_DIR/zvol_id"
	install "$CMD_DIR/vdev_id/vdev_id" \
	    "$INSTALL_UDEV_DIR/vdev_id"
	install "$UDEV_RULE_DIR/60-zvol.rules" \
	    "$INSTALL_UDEV_RULE_DIR/60-zvol.rules"
	install "$UDEV_RULE_DIR/69-vdev.rules" \
	    "$INSTALL_UDEV_RULE_DIR/69-vdev.rules"
	install "$UDEV_RULE_DIR/90-zfs.rules" \
	    "$INSTALL_UDEV_RULE_DIR/90-zfs.rules"
	install "$CMD_DIR/zpool/zpool.d" \
	    "$INSTALL_SYSCONF_DIR/zfs/zpool.d"
	install "$CONTRIB_DIR/pyzfs/libzfs_core" \
	    "$INSTALL_PYTHON_DIR/libzfs_core"
	# Ideally we would install these in the configured ${libdir}, which is
	# by default "/usr/local/lib and unfortunately not included in the
	# dynamic linker search path.
	install "$(find "$LIB_DIR/libzfs_core" -type f -name 'libzfs_core.so*')" \
	    "/lib/libzfs_core.so"
	install "$(find "$LIB_DIR/libnvpair" -type f -name 'libnvpair.so*')" \
	    "/lib/libnvpair.so"
	ldconfig
else
	remove "$INSTALL_MOUNT_HELPER_DIR/mount.zfs"
	remove "$INSTALL_MOUNT_HELPER_DIR/fsck.zfs"
	remove "$INSTALL_UDEV_DIR/zvol_id"
	remove "$INSTALL_UDEV_DIR/vdev_id"
	remove "$INSTALL_UDEV_RULE_DIR/60-zvol.rules"
	remove "$INSTALL_UDEV_RULE_DIR/69-vdev.rules"
	remove "$INSTALL_UDEV_RULE_DIR/90-zfs.rules"
	remove "$INSTALL_SYSCONF_DIR/zfs/zpool.d"
	remove "$INSTALL_PYTHON_DIR/libzfs_core"
	remove "/lib/libzfs_core.so"
	remove "/lib/libnvpair.so"
	ldconfig
fi

exit 0
