#!/bin/bash

prog=zpios-jbod.sh
. ../.script-config

SPL_OPTIONS=$1
ZPOOL_OPTIONS=$2
KPIOS_OPTIONS=$3
PROFILE_KPIOS_LOGS=$4
KPIOS_PRE=$5
KPIOS_POST=$6

PROFILE_KPIOS_PRE=/home/behlendo/src/zfs/scripts/profile-kpios-pre.sh
PROFILE_KPIOS_POST=/home/behlendo/src/zfs/scripts/profile-kpios-post.sh

echo ------------------------- ZFS TEST LOG ---------------------------------
echo -n "Date = "; date
echo -n "Kernel = "; uname -r
echo ------------------------------------------------------------------------

echo
./load-zfs.sh "${SPL_OPTIONS}" "${ZPOOL_OPTIONS}"

sysctl -w kernel.spl.debug.mask=0
sysctl -w kernel.spl.debug.subsystem=0

echo ---------------------- SPL Sysctl Tunings ------------------------------
sysctl -A | grep spl
echo

echo ------------------- SPL/ZPOOL Module Tunings ---------------------------
grep [0-9] /sys/module/spl/parameters/*
grep [0-9] /sys/module/zpool/parameters/*
echo

DEVICES="/dev/sdn  /dev/sdo  /dev/sdp \
         /dev/sdq  /dev/sdr  /dev/sds \
         /dev/sdt  /dev/sdu  /dev/sdv \
         /dev/sdw  /dev/sdx  /dev/sdy"

${CMDDIR}/zpool/zpool create -F lustre ${DEVICES}
${CMDDIR}/zpool/zpool status lustre

if [ -n "${KPIOS_PRE}" ]; then
	${KPIOS_PRE}
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
#	--threadcount=256,256,256,256,256                        \

CMD="${CMDDIR}/zpios/zpios                                       \
	--load=dmuio                                             \
	--path=lustre                                            \
	--chunksize=1M                                           \
	--regionsize=4M                                          \
	--regioncount=16384                                      \
	--threadcount=256                        \
	--offset=4M                                              \
        --cleanup                                                \
	--verbose                                                \
	--human-readable                                         \
	${KPIOS_OPTIONS}                                         \
        --prerun=${PROFILE_KPIOS_PRE}                            \
        --postrun=${PROFILE_KPIOS_POST}                          \
        --log=${PROFILE_KPIOS_LOGS}" 
echo
date
echo ${CMD}
$CMD
date

if [ -n "${KPIOS_POST}" ]; then
	${KPIOS_POST}
fi 

${CMDDIR}/zpool/zpool destroy lustre
./unload-zfs.sh
