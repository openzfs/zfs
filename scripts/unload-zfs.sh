#!/bin/bash

prog=unload-zfs.sh
. ../.script-config

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

unload_module() {
	echo "Unloading $1"
	/sbin/rmmod $1 || die "Failed to unload $1"
}

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

unload_module ${zpios_module}
unload_module ${zctl_module}
unload_module ${zpool_module}
unload_module ${zcommon_module}
unload_module ${znvpair_module}
unload_module ${zavl_module}
unload_module ${zlib_module}

# Set DUMP=1 to generate debug logs on unload
if [ -n "${DUMP}" ]; then
	sysctl -w kernel.spl.debug.dump=1
	# This is racy, I don't like it, but for a helper script it will do.
	SPL_LOG=`dmesg | tail -n 1 | cut -f5 -d' '`
	${SPLBUILD}/cmd/spl ${SPL_LOG} >${SPL_LOG}.log
	echo
	echo "Dumped debug log: ${SPL_LOG}.log"
	tail -n1 ${SPL_LOG}.log
	echo
fi

unload_module ${spl_module}

echo "Successfully unloaded ZFS module stack"

exit 0
