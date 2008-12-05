#!/bin/bash

prog=check.sh

die() {
	echo "${prog}: $1" >&2
	exit 1
}

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

./load-zfs.sh || die ""
./unload-zfs.sh || die ""

exit 0
