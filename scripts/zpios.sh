#!/bin/bash

prog=zpios.sh
. ../.script-config

SPL_OPTIONS="spl=spl_debug_mask=0 spl_debug_subsys=0 spl_debug_mb=-1 ${1}"
ZFS_OPTIONS="zfs=${2}"
ZPIOS_OPTIONS=$3
PROFILE_ZPIOS_LOGS=$4
ZPIOS_PRE=$5
ZPIOS_POST=$6

PROFILE_ZPIOS_PRE=/home/behlendo/src/zfs/scripts/profile-zpios-pre.sh
PROFILE_ZPIOS_POST=/home/behlendo/src/zfs/scripts/profile-zpios-post.sh

DEVICES="/dev/hda"

echo ------------------------- ZFS TEST LOG ---------------------------------
echo -n "Date = "; date
echo -n "Kernel = "; uname -r
echo ------------------------------------------------------------------------
echo

echo "rm /etc/zfs/zpool.cache" || exit 1
rm -f /etc/zfs/zpool.cache

echo "./zfs.sh"
./zfs.sh "${SPL_OPTIONS}" "${ZPOOL_OPTIONS}" || exit 1
echo

echo ---------------------- SPL Sysctl Tunings ------------------------------
sysctl -A | grep spl
echo

echo ------------------- SPL Module Tunings ---------------------------
if [ -d /sys/module/spl/parameters ]; then
	grep [0-9] /sys/module/spl/parameters/*
else
	grep [0-9] /sys/module/spl/*
fi
echo

echo ------------------- ZFS Module Tunings ---------------------------
if [ -d /sys/module/zfs/parameters ]; then
	grep [0-9] /sys/module/zfs/parameters/*
else
	grep [0-9] /sys/module/zfs/*
fi
echo

echo "${CMDDIR}/zpool/zpool create -f lustre ${DEVICES}"
${CMDDIR}/zpool/zpool create -f lustre ${DEVICES} || exit 1

echo "${CMDDIR}/zpool/zpool status lustre"
${CMDDIR}/zpool/zpool status lustre || exit 1

echo "Waiting for /dev/zpios to come up..."
while [ ! -c /dev/zpios ]; do
	sleep 1
done

if [ -n "${ZPIOS_PRE}" ]; then
	${ZPIOS_PRE} || exit 1
fi 

# Usage: zpios
#         --chunksize         -c    =values
#         --chunksize_low     -a    =value
#         --chunksize_high    -b    =value
#         --chunksize_incr    -g    =value
#         --offset            -o    =values
#         --offset_low        -m    =value
#         --offset_high       -q    =value
#         --offset_incr       -r    =value
#         --regioncount       -n    =values
#         --regioncount_low   -i    =value
#         --regioncount_high  -j    =value
#         --regioncount_incr  -k    =value
#         --threadcount       -t    =values
#         --threadcount_low   -l    =value
#         --threadcount_high  -h    =value
#         --threadcount_incr  -e    =value
#         --regionsize        -s    =values
#         --regionsize_low    -A    =value
#         --regionsize_high   -B    =value
#         --regionsize_incr   -C    =value
#         --cleanup           -x
#         --verify            -V
#         --zerocopy          -z
#         --threaddelay       -T    =jiffies
#         --regionnoise       -I    =shift
#         --chunknoise        -N    =bytes
#         --prerun            -P    =pre-command
#         --postrun           -R    =post-command
#         --log               -G    =log directory
#         --pool | --path     -p    =pool name
#         --load              -L    =dmuio
#         --help              -?    =this help
#         --verbose           -v    =increase verbosity

#        --prerun=${PROFILE_ZPIOS_PRE}                            \
#        --postrun=${PROFILE_ZPIOS_POST}                          \

CMD="${CMDDIR}/zpios/zpios                                       \
	--load=dmuio                                             \
	--path=lustre                                            \
	--chunksize=1M                                           \
	--regionsize=4M                                          \
	--regioncount=256                                        \
	--threadcount=4                                          \
	--offset=4M                                              \
        --cleanup                                                \
	--verbose                                                \
	--human-readable                                         \
	${ZPIOS_OPTIONS}                                         \
        --log=${PROFILE_ZPIOS_LOGS}" 
echo
date
echo ${CMD}
$CMD || exit 1
date

if [ -n "${ZPIOS_POST}" ]; then
	${ZPIOS_POST} || exit 1
fi 

echo
echo "${CMDDIR}/zpool/zpool destroy lustre"
${CMDDIR}/zpool/zpool destroy lustre
echo

echo ---------------------- SPL Sysctl Tunings ------------------------------
sysctl -A | grep spl
echo

if [ -d /proc/spl/kstat/ ]; then
	if [ -f /proc/spl/kstat/zfs/arcstats ]; then
		echo "------------------ ARCSTATS --------------------------"
		cat /proc/spl/kstat/zfs/arcstats
		echo
	fi

	if [ -f /proc/spl/kstat/zfs/vdev_cache_stats ]; then
		echo "-------------- VDEV_CACHE_STATS ----------------------"
		cat /proc/spl/kstat/zfs/vdev_cache_stats
		echo
	fi
fi

if [ -f /proc/spl/kmem/slab ]; then
	echo "-------------------- SLAB ----------------------------"
	cat /proc/spl/kmem/slab
	echo
fi

echo "./zfs.sh -u"
./zfs.sh -u || exit 1

exit 0
