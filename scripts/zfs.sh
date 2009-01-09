#!/bin/bash

prog=load-zfs.sh
. ../.script-config

KMOD=/lib/modules/${KERNELSRCVER}/kernel
kernel_modules=(				\
	$KMOD/lib/zlib_deflate/zlib_deflate.ko	\
)

spl_modules=(					\
	${SPLBUILD}/modules/spl/spl.ko		\
)

zfs_modules=(					\
	${MODDIR}/avl/zavl.ko			\
	${MODDIR}/nvpair/znvpair.ko		\
	${MODDIR}/unicode/zunicode.ko		\
	${MODDIR}/zcommon/zcommon.ko		\
	${MODDIR}/zfs/zpool.ko			\
)

test_modules=(					\
	${MODDIR}/zpios/zpios.ko		\
)

modules=(					\
	${kernel_modules[*]}			\
	${spl_modules[*]}			\
	${zfs_modules[*]}			\
	${test_modules[*]}			\
)

die() {
	echo -e "${prog}: $1" >&2
	exit 1
}

usage() {
cat << EOF
usage: $0 [hvu] [module-options]

OPTIONS:
   -h      Show this message
   -v      Verbose
   -u      Unload modules
   -d      Save debug log on unload

MODULE-OPTIONS: Must be of the frm module="options", for example:

$0 zpool="zfs_prefetch_disable=1"
$0 zpool="zfs_prefetch_disable=1 zfs_mdcomp_disable=1"
$0 spl="spl_debug_mask=0"

EOF
}

check_modules() {
	loaded_modules=()
	missing_modules=()

	for module in ${modules[*]}; do
		name=`basename $module .ko`

		if /sbin/lsmod | egrep -q "^${name}"; then
			loaded_modules=(${name} ${loaded_modules[*]})
		fi

		if [ ! -f $module ]; then
			missing_modules=("\t${module}\n" ${missing_modules[*]})
		fi
	done

	if [ ${#loaded_modules[*]} -gt 0 ]; then
		error="The following modules must be unloaded, manually run:\n"
		die "${error}'/sbin/rmmod ${loaded_modules[*]}'"
	fi

	if [ ${#missing_modules[*]} -gt 0 ]; then
		error="The following modules can not be found,"
		error="${error} ensure your source trees are built:\n"
		die "${error}${missing_modules[*]}"
	fi
}

spl_dump_log() {
        sysctl -w kernel.spl.debug.dump=1 &>/dev/null
	name=`dmesg | tail -n 1 | cut -f5 -d' '`
        ${SPLBUILD}/cmd/spl ${name} >${name}.log
	echo
        echo "Dumped debug log: ${name}.log"
        tail -n1 ${name}.log
	echo
}

load_module() {
	name=`basename $module .ko`

	if [ ${verbose} ]; then
		echo "Loading $name ($@)"
	fi

	/sbin/insmod $* || die "Failed to load $1"
}

load_modules() {

	for module in ${modules[*]}; do
		name=`basename $module .ko`
		value=

		for opt in "$@"; do
			opt_name=`echo $opt | cut -f1 -d'='`
			
			if [ ${name} = ${opt_name} ]; then
				value=`echo $opt | cut -f2- -d'='`
			fi
		done

		load_module ${module} ${value}
	done

	if [ ${verbose} ]; then
		echo "Successfully loaded ZFS module stack"
	fi
}

unload_module() {
	name=`basename $module .ko`

	if [ ${verbose} ]; then
		echo "Unloading $name ($@)"
	fi

	/sbin/rmmod $name || die "Failed to unload $name"
}

unload_modules() {
	modules_reverse=( $(echo ${modules[@]} |
		awk '{for (i=NF;i>=1;i--) printf $i" "} END{print ""}') )

	for module in ${modules_reverse[*]}; do
		name=`basename $module .ko`

		if /sbin/lsmod | egrep -q "^${name}"; then

			if [ "${dump_log}" -a ${name} = "spl" ]; then
				spl_dump_log
			fi

			unload_module ${module}
		fi
	done

	if [ ${verbose} ]; then
		echo "Successfully unloaded ZFS module stack"
	fi
}

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

verbose=
unload=
dump_log=

while getopts 'hvud' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		verbose=1
		;;
	u)
		unload=1
		;;
	d)
		dump_log=1
		;;
	?)
		usage
		exit
		;;
	esac
done

if [ ${unload} ]; then
	unload_modules
else
	check_modules
	load_modules "$@"
fi

exit 0
