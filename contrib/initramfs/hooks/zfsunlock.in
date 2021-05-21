#!/bin/sh

if [ "$1" = "prereqs" ]; then
	echo "dropbear"
	exit
fi

. /usr/share/initramfs-tools/hook-functions

copy_exec /usr/share/initramfs-tools/zfsunlock /usr/bin/zfsunlock
