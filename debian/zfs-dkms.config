#!/bin/sh

set -e

# Source debconf library
. /usr/share/debconf/confmodule

db_input critical zfs-dkms/note-incompatible-licenses || true
db_go

kernelbits=unknown
if [ -r /proc/kallsyms ]; then
	addrlen=$(head -1 /proc/kallsyms|awk '{print $1}'|wc -c)
	if [ $addrlen = 17 ]; then
		kernelbits=64
	elif [ $addrlen = 9 ]; then
		kernelbits=32
	fi
fi

if [ $kernelbits != 64 ]; then
	if [ $kernelbits = 32 ]; then
		db_input critical zfs-dkms/stop-build-for-32bit-kernel || true
		db_go || true
	else
		db_input critical zfs-dkms/stop-build-for-unknown-kernel || true
		db_go || true
	fi
fi

#DEBHELPER#
