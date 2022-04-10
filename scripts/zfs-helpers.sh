#!/bin/sh
# shellcheck disable=SC2154
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

BASE_DIR=${0%/*}
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
$0 [-dhirv]

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
	*)
		;;
	esac
done

if [ "$INSTALL" = "yes" ] && [ "$REMOVE" = "yes" ]; then
	fail "Specify -i or -r but not both"
fi

if [ "$INSTALL" = "no" ] && [ "$REMOVE" = "no" ]; then
	fail "Either -i or -r must be specified"
fi

if [ "$(id -u)" != "0" ] && [ "$DRYRUN" = "no" ]; then
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
	elif ! [ -e "$src" ]; then
		echo "Source missing: $src"
	else
		msg "ln -s $src $dst"

		if [ "$DRYRUN" = "no" ]; then
			DIR=${dst%/*}
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
		DIR=${dst%/*}
		rmdir "$DIR" >/dev/null 2>&1
	elif [ -e "$dst" ]; then
		echo "Expected symlink: $dst"
	fi
}

if [ "${INSTALL}" = "yes" ]; then
	for cmd in "mount.zfs" "fsck.zfs"; do
		install "$CMD_DIR/$cmd" "$INSTALL_MOUNT_HELPER_DIR/$cmd"
	done
	for udev in "$UDEV_CMD_DIR/zvol_id" "$UDEV_SCRIPT_DIR/vdev_id"; do
		install "$udev" "$INSTALL_UDEV_DIR/${udev##*/}"
	done
	for rule in "60-zvol.rules" "69-vdev.rules" "90-zfs.rules"; do
		install "$UDEV_RULE_DIR/$rule" "$INSTALL_UDEV_RULE_DIR/$rule"
	done
	install "$ZPOOL_SCRIPT_DIR"              "$INSTALL_SYSCONF_DIR/zfs/zpool.d"
	install "$CONTRIB_DIR/pyzfs/libzfs_core" "$INSTALL_PYTHON_DIR/libzfs_core"
	# Ideally we would install these in the configured ${libdir}, which is
	# by default "/usr/local/lib and unfortunately not included in the
	# dynamic linker search path.
	install "$LIB_DIR"/libzfs_core.so.?.?.? "/lib/libzfs_core.so"
	install "$LIB_DIR"/libnvpair.so.?.?.?   "/lib/libnvpair.so"
	[ "$DRYRUN" = "no" ] && ldconfig
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
