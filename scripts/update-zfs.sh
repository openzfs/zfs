#!/bin/bash
#
# WARNING: This script removes the entire zfs subtree and will
# repopulate it using the requested OpenSolaris source release.
# This script should only be used when rebasing the TopGit tree
# against the latest release.  
#
trap die_int INT

RELEASE=$1
PROG=update-zfs.sh
REMOTE_SRC=http://dlc.sun.com/osol/on/downloads/${RELEASE}/on-src.tar.bz2

die() {
	rm -Rf ${SRC}
	echo "${PROG}: $1" >&2
	exit 1
}

die_int() {
	die "Ctrl-C abort"
}

DST=`pwd`
if [ `basename $DST` != "scripts" ]; then
	die "Must be run from scripts directory"
fi

SRC=`mktemp -d /tmp/os-${RELEASE}.XXXXXXXXXX`
DST=`dirname $DST`

echo "----------------------------------------------------------------------"
echo "Remote Source: ${REMOTE_SRC}"
echo "Local Source:  ${SRC}"
echo "Local Dest:    ${DST}"
echo
echo "------------- Fetching OpenSolaris ${RELEASE} archive ----------------"
wget ${REMOTE_SRC} -P ${SRC} ||
	die "Error 'wget ${REMOTE_SRC}'"

echo "------------- Unpacking OpenSolaris ${RELEASE} archive ---------------"
tar -xjf ${SRC}/on-src.tar.bz2 -C ${SRC} ||
	die "Error 'tar -xjf ${SRC}/on-src.tar.bz2 -C ${SRC}'"

SRC_LIB=${SRC}/usr/src/lib
SRC_CMD=${SRC}/usr/src/cmd
SRC_CM=${SRC}/usr/src/common
SRC_UTS=${SRC}/usr/src/uts
SRC_UCM=${SRC}/usr/src/uts/common
SRC_ZLIB=${SRC}/usr/src/uts/common/fs/zfs

DST_MOD=${DST}/module
DST_LIB=${DST}/lib
DST_CMD=${DST}/cmd

rm -Rf ${DST}/zfs

echo
echo "------------- Updating ZFS from OpenSolaris ${RELEASE} ---------------"
echo "* module/avl"
mkdir -p ${DST_MOD}/avl/include/sys/
cp ${SRC_CM}/avl/avl.c				${DST_MOD}/avl/
cp ${SRC_UCM}/sys/avl.h				${DST_MOD}/avl/include/sys/
cp ${SRC_UCM}/sys/avl_impl.h			${DST_MOD}/avl/include/sys/

echo "* module/nvpair"
mkdir -p ${DST_MOD}/nvpair/include/sys/
cp ${SRC_CM}/nvpair/nvpair.c			${DST_MOD}/nvpair/
cp ${SRC_UCM}/sys/nvpair.h			${DST_MOD}/nvpair/include/sys/
cp ${SRC_UCM}/sys/nvpair_impl.h			${DST_MOD}/nvpair/include/sys/

echo "* module/unicode"
mkdir -p ${DST_MOD}/unicode/include/sys/
cp ${SRC_CM}/unicode/*.c			${DST_MOD}/unicode/
cp ${SRC_UCM}/sys/u8_textprep.h			${DST_MOD}/unicode/include/sys/
cp ${SRC_UCM}/sys/u8_textprep_data.h		${DST_MOD}/unicode/include/sys/

echo "* module/zcommon"
mkdir -p ${DST_MOD}/zcommon/include/sys/fs/
mkdir -p ${DST_MOD}/zcommon/include/sys/fm/fs/
cp ${SRC_CM}/zfs/*.c				${DST_MOD}/zcommon/
cp ${SRC_CM}/zfs/*.h				${DST_MOD}/zcommon/include/
cp ${SRC_UCM}/sys/fs/zfs.h			${DST_MOD}/zcommon/include/sys/fs/
cp ${SRC_UCM}/sys/fm/fs/zfs.h			${DST_MOD}/zcommon/include/sys/fm/fs/

echo "* module/zfs"
mkdir -p ${DST_MOD}/zpool/include/sys/
cp ${SRC_UTS}/intel/zfs/spa_boot.c		${DST_MOD}/zfs/
cp ${SRC_ZLIB}/*.c				${DST_MOD}/zfs/
cp ${SRC_ZLIB}/sys/*.h				${DST_MOD}/zfs/include/sys/
rm ${DST_MOD}/zfs/vdev_disk.c
rm ${DST_MOD}/zfs/include/sys/vdev_disk.h

echo "* lib/libavl"
# Full source available in 'module/avl'

echo "* lib/libnvpair"
mkdir -p ${DST_LIB}/libnvpair/include/
cp ${SRC_UCM}/os/nvpair_alloc_system.c		${DST_LIB}/libnvpair/
cp ${SRC_LIB}/libnvpair/libnvpair.c		${DST_LIB}/libnvpair/
cp ${SRC_LIB}/libnvpair/libnvpair.h		${DST_LIB}/libnvpair/include/

echo "* lib/libunicode"
# Full source available in 'module/unicode'

echo "* lib/libuutil"
mkdir -p ${DST_LIB}/libuutil/include/
cp ${SRC_LIB}/libuutil/common/*.c		${DST_LIB}/libuutil/
cp ${SRC_LIB}/libuutil/common/*.h		${DST_LIB}/libuutil/include/

echo "* lib/libzpool"
mkdir -p ${DST_LIB}/libzpool/include/sys/
cp ${SRC_LIB}/libzpool/common/kernel.c		${DST_LIB}/libzpool/
cp ${SRC_LIB}/libzpool/common/taskq.c		${DST_LIB}/libzpool/
cp ${SRC_LIB}/libzpool/common/util.c		${DST_LIB}/libzpool/
cp ${SRC_LIB}/libzpool/common/sys/zfs_context.h	${DST_LIB}/libzpool/include/sys/

echo "* lib/libzfs"
mkdir -p ${DST_LIB}/libzfs/include/
cp ${SRC_LIB}/libzfs/common/*.c			${DST_LIB}/libzfs/
cp ${SRC_LIB}/libzfs/common/*.h			${DST_LIB}/libzfs/include/

echo "* cmd/zpool"
mkdir -p ${DST_CMD}/zpool
cp ${SRC_CMD}/zpool/*.c				${DST_CMD}/zpool/
cp ${SRC_CMD}/zpool/*.h				${DST_CMD}/zpool/

echo "* cmd/zfs"
mkdir -p ${DST_CMD}/zfs
cp ${SRC_CMD}/zfs/*.c				${DST_CMD}/zfs/
cp ${SRC_CMD}/zfs/*.h				${DST_CMD}/zfs/

echo "* cmd/zdb"
mkdir -p ${DST_CMD}/zdb/
cp ${SRC_CMD}/zdb/*.c				${DST_CMD}/zdb/

echo "* cmd/zinject"
mkdir -p ${DST_CMD}/zinject
cp ${SRC_CMD}/zinject/*.c			${DST_CMD}/zinject/
cp ${SRC_CMD}/zinject/*.h			${DST_CMD}/zinject/

echo "* cmd/ztest"
mkdir -p ${DST_CMD}/ztest
cp ${SRC_CMD}/ztest/*.c				${DST_CMD}/ztest/

echo "${REMOTE_SRC}" >${DST}/ZFS.RELEASE

rm -Rf ${SRC}
