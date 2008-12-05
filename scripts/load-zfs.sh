#!/bin/bash

prog=load-zfs.sh
. ../.script-config

spl_options=$1
zpool_options=$2

spl_module=${SPLBUILD}/modules/spl/spl.ko
zlib_module=/lib/modules/${KERNELSRCVER}/kernel/lib/zlib_deflate/zlib_deflate.ko
zavl_module=${ZFSBUILD}/lib/libavl/zavl.ko
znvpair_module=${ZFSBUILD}/lib/libnvpair/znvpair.ko
zcommon_module=${ZFSBUILD}/lib/libzcommon/zcommon.ko
zpool_module=${ZFSBUILD}/lib/libzpool/zpool.ko
zctl_module=${ZFSBUILD}/lib/libdmu-ctl/zctl.ko
zpios_module=${ZFSBUILD}/lib/libzpios/zpios.ko

die() {
	echo "${prog}: $1" >&2
	exit 1
}

load_module() {
	echo "Loading $1"
	/sbin/insmod $* || die "Failed to load $1"
}

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

if /sbin/lsmod | egrep -q "^spl|^zavl|^znvpair|^zcommon|^zlib_deflate|^zpool"; then
	die "Must start with modules unloaded"
fi

if [ ! -f ${zavl_module} ] ||
   [ ! -f ${znvpair_module} ] ||
   [ ! -f ${zcommon_module} ] ||
   [ ! -f ${zpool_module} ]; then
	die "Source tree must be built, run 'make'"
fi

load_module ${spl_module} ${spl_options}
load_module ${zlib_module}
load_module ${zavl_module}
load_module ${znvpair_module}
load_module ${zcommon_module}
load_module ${zpool_module} ${zpool_options}
load_module ${zctl_module}
load_module ${zpios_module}

sleep 1
echo "Successfully loaded ZFS module stack"

exit 0
