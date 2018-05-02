#!/bin/sh
#
# Add udev rules for ZoL to the initrd.
#

PREREQ="udev"
PREREQ_UDEV_RULES="60-zvol.rules 69-vdev.rules"
COPY_EXEC_LIST="/lib/udev/zvol_id /lib/udev/vdev_id"

# Generic result code.
RC=0

case $1 in
prereqs)
	echo "$PREREQ"
	exit 0
	;;
esac

for ii in $COPY_EXEC_LIST
do
	if [ ! -x "$ii" ]
	then
		echo "Error: $ii is not executable."
		RC=2
	fi
done

if [ "$RC" -ne 0 ]
then
	exit "$RC"
fi

. /usr/share/initramfs-tools/hook-functions

mkdir -p "$DESTDIR/lib/udev/rules.d/"
for ii in $PREREQ_UDEV_RULES
do
	if [ -e "/etc/udev/rules.d/$ii" ]
	then
		cp -p "/etc/udev/rules.d/$ii" "$DESTDIR/lib/udev/rules.d/"
	elif [ -e "/lib/udev/rules.d/$ii" ]
	then
		cp -p "/lib/udev/rules.d/$ii" "$DESTDIR/lib/udev/rules.d/"
	else
		echo "Error: Missing udev rule: $ii"
		echo "       This file must be in the /etc/udev/rules.d or /lib/udev/rules.d directory."
		exit 1
	fi
done

for ii in $COPY_EXEC_LIST
do
	copy_exec "$ii"
done

if [ -f '/etc/default/zfs' -a -r '/etc/default/zfs' ]
then
	mkdir -p "$DESTDIR/etc/default"
	cp -a '/etc/default/zfs' "$DESTDIR/etc/default/"
fi

if [ -d '/etc/zfs' -a -r '/etc/zfs' ]
then
	mkdir -p "$DESTDIR/etc"
	cp -a '/etc/zfs' "$DESTDIR/etc/"
fi
