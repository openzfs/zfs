#!/bin/sh
set -e

# Run depmod first
depmod -a _KVERS_

#DEBHELPER#


case $1 in
	(configure)
		if [ -x /usr/share/update-notifier/notify-reboot-required ]; then
			/usr/share/update-notifier/notify-reboot-required
		fi
		;;
esac
