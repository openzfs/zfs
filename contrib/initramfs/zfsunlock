#!/bin/sh

set -eu
if [ ! -e /run/zfs_fs_name ]; then
	echo "Wait for the root pool to be imported or press Ctrl-C to exit."
fi
while [ ! -e /run/zfs_fs_name ]; do
	if [ -e /run/zfs_unlock_complete ]; then
		exit 0
	fi
	sleep 1
done
echo
echo "Unlocking encrypted ZFS filesystems..."
echo "Enter the password or press Ctrl-C to exit."
echo
zfs_fs_name=""
if [ ! -e /run/zfs_unlock_complete_notify ]; then
	mkfifo /run/zfs_unlock_complete_notify
fi
while [ ! -e /run/zfs_unlock_complete ]; do
	zfs_fs_name=$(cat /run/zfs_fs_name)
	zfs_console_askpwd_cmd=$(cat /run/zfs_console_askpwd_cmd)
	systemd-ask-password "Encrypted ZFS password for ${zfs_fs_name}:" | \
		/sbin/zfs load-key "$zfs_fs_name" || true
	if [ "$(/sbin/zfs get -H -ovalue keystatus "$zfs_fs_name" 2> /dev/null)" = "available" ]; then
		echo "Password for $zfs_fs_name accepted."
		zfs_console_askpwd_pid=$(ps | awk '!'"/awk/ && /$zfs_console_askpwd_cmd/ { print \$1; exit }")
		if [ -n "$zfs_console_askpwd_pid" ]; then
			kill "$zfs_console_askpwd_pid"
		fi
		# Wait for another filesystem to unlock.
		while [ "$(cat /run/zfs_fs_name)" = "$zfs_fs_name" ] && [ ! -e /run/zfs_unlock_complete ]; do
			sleep 1
		done
	else
		echo "Wrong password.  Try again."
	fi
done
echo "Unlocking complete.  Resuming boot sequence..."
echo "Please reconnect in a while."
echo "ok" > /run/zfs_unlock_complete_notify
