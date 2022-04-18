#!/bin/sh
set -e

# Source debconf library (see dh_installdebconf(1) and #106070 #626312)
. /usr/share/debconf/confmodule

kernelbits=unknown
if [ -r /proc/kallsyms ]; then
	addrlen=$(head -1 /proc/kallsyms| grep -o '^ *[^ ]*' |wc -c)
	if [ $addrlen = 17 ]; then
		kernelbits=64
	elif [ $addrlen = 9 ]; then
		kernelbits=32
	fi
fi

if [ $kernelbits != 64 ]; then
	if [ $kernelbits = 32 ]; then
		db_get zfs-dkms/stop-build-for-32bit-kernel
		if [ "$RET" = "true" ]; then
			echo "Ok, aborting, since ZFS is not designed for 32-bit kernels." 1>&2
			# Exit 0: Tell dpkg that we finished OK but stop here.
			# (don't build the module)
			exit 0
		else
			echo "WARNING: Building ZFS module on a 32-bit kernel." 1>&2
		fi
	else
		db_get zfs-dkms/stop-build-for-unknown-kernel
		if [ "$RET" = "true" ]; then
			echo "Ok, aborting, since ZFS is not designed for 32-bit kernels." 1>&2
			# Exit 0: (same that above)
			exit 0
		else
			echo "WARNING: Building ZFS module on an unknown kernel." 1>&2
		fi
	fi
fi

# Here the module gets built (automatically handled by dh_dkms)

#DEBHELPER#


case $1 in
	(configure)
		if [ -x /usr/share/update-notifier/notify-reboot-required ]; then
			/usr/share/update-notifier/notify-reboot-required
		fi
		;;
esac
