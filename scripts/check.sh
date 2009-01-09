#!/bin/bash

prog=check.sh

die() {
	echo "${prog}: $1" >&2
	exit 1
}

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

./zfs.sh -v  || die ""
./zfs.sh -vu || die ""

exit 0
