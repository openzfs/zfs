#!/bin/bash

prog=zpios.sh
. ../.script-config

SPL_OPTIONS="spl_debug_mask=0 spl_debug_subsys=0 ${1}"
ZPOOL_OPTIONS=$2
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
./load-zfs.sh "${SPL_OPTIONS}" "${ZPOOL_OPTIONS}"

echo ---------------------- SPL Sysctl Tunings ------------------------------
sysctl -A | grep spl
echo

echo ------------------- SPL/ZPOOL Module Tunings ---------------------------
if [ -d /sys/module/spl/parameters ]; then
	grep [0-9] /sys/module/spl/parameters/*
	grep [0-9] /sys/module/zpool/parameters/*
else
	grep [0-9] /sys/module/spl/*
	grep [0-9] /sys/module/zpool/*
fi
echo

echo "${CMDDIR}/zpool/zpool create -f lustre ${DEVICES}"
${CMDDIR}/zpool/zpool create -f lustre ${DEVICES}

echo "${CMDDIR}/zpool/zpool status lustre"
${CMDDIR}/zpool/zpool status lustre

echo "Waiting for /dev/zpios to come up..."
while [ ! -c /dev/zpios ]; do
	sleep 1
done

if [ -n "${ZPIOS_PRE}" ]; then
	${ZPIOS_PRE}
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
	--regioncount=64                                         \
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
$CMD
date

if [ -n "${ZPIOS_POST}" ]; then
	${ZPIOS_POST}
fi 

${CMDDIR}/zpool/zpool destroy lustre

echo ---------------------- SPL Sysctl Tunings ------------------------------
sysctl -A | grep spl
echo

echo ------------------------ KSTAT Statistics ------------------------------
echo ARCSTATS
cat /proc/spl/kstat/zfs/arcstats
echo
echo VDEV_CACHE_STATS
cat /proc/spl/kstat/zfs/vdev_cache_stats
echo
echo SLAB
cat /proc/spl/kmem/slab
echo

./unload-zfs.sh
