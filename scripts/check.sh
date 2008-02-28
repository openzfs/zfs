#!/bin/bash

prog=check.sh
spl_module=../modules/spl/spl.ko
splat_module=../modules/splat/splat.ko
splat_cmd=../cmd/splat

die() {
	echo "${prog}: $1" >&2
	exit 1
}

warn() {
	echo "${prog}: $1" >&2
}

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

if /sbin/lsmod | egrep -q "^spl|^splat"; then
	die "Must start with spl modules unloaded"
fi

if [ ! -f ${spl_module} ] || [ ! -f ${splat_module} ]; then
	die "Source tree must be built, run 'make'"
fi

echo "Loading ${spl_module}"
/sbin/insmod ${spl_module} || die "Failed to load ${spl_module}"

echo "Loading ${splat_module}"
/sbin/insmod ${splat_module} || die "Unable to load ${splat_module}"

sleep 5
$splat_cmd -a

echo "Unloading ${splat_module}"
/sbin/rmmod ${splat_module} || die "Failed to unload ${splat_module}"

echo "Unloading ${spl_module}"
/sbin/rmmod ${spl_module} || die "Unable to unload ${spl_module}"

exit 0
