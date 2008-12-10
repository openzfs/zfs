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

echo "------------- Unpacking OperSolaris ${RELEASE} archive ---------------"
tar -xjf ${SRC}/on-src.tar.bz2 -C ${SRC} ||
	die "Error 'tar -xjf ${SRC}/on-src.tar.bz2 -C ${SRC}'"

SRC_LIB=${SRC}/usr/src/lib
SRC_CMD=${SRC}/usr/src/cmd
SRC_CM=${SRC}/usr/src/common
SRC_UTS=${SRC}/usr/src/uts
SRC_UCM=${SRC}/usr/src/uts/common
SRC_ZLIB=${SRC}/usr/src/uts/common/fs/zfs

DST_LIB=${DST}/zfs/lib
DST_CMD=${DST}/zfs/zcmd

rm -Rf ${DST}/zfs

echo
echo "------------- Updating ZFS from OpenSolaris ${RELEASE} ---------------"
echo "* zfs/lib/libavl"
mkdir -p ${DST_LIB}/libavl/include/sys/
cp ${SRC_CM}/avl/avl.c				${DST_LIB}/libavl/
cp ${SRC_UCM}/sys/avl.h				${DST_LIB}/libavl/include/sys/
cp ${SRC_UCM}/sys/avl_impl.h			${DST_LIB}/libavl/include/sys/

echo "* zfs/lib/libnvpair"
mkdir -p ${DST_LIB}/libnvpair/include/sys/
cp ${SRC_CM}/nvpair/nvpair.c			${DST_LIB}/libnvpair/
cp ${SRC_LIB}/libnvpair/libnvpair.c		${DST_LIB}/libnvpair/
cp ${SRC_UCM}/os/nvpair_alloc_system.c		${DST_LIB}/libnvpair/
cp ${SRC_CM}/nvpair/nvpair_alloc_fixed.c	${DST_LIB}/libnvpair/
cp ${SRC_LIB}/libnvpair/libnvpair.h		${DST_LIB}/libnvpair/include/
cp ${SRC_UCM}/sys/nvpair.h			${DST_LIB}/libnvpair/include/sys/
cp ${SRC_UCM}/sys/nvpair_impl.h			${DST_LIB}/libnvpair/include/sys/

echo "* zfs/lib/libuutil"
mkdir -p ${DST_LIB}/libuutil/include/
cp ${SRC_LIB}/libuutil/common/*.c		${DST_LIB}/libuutil/
cp ${SRC_LIB}/libuutil/common/*.h		${DST_LIB}/libuutil/include/

echo "* zfs/lib/libspl"
mkdir -p ${DST_LIB}/libspl/include/sys/
cp ${SRC_LIB}/libc/port/gen/strlcat.c		${DST_LIB}/libspl/
cp ${SRC_LIB}/libc/port/gen/strlcpy.c		${DST_LIB}/libspl/
cp ${SRC_LIB}/libc/port/gen/strnlen.c		${DST_LIB}/libspl/
cp ${SRC_LIB}/libgen/common/mkdirp.c		${DST_LIB}/libspl/
cp ${SRC_CM}/unicode/u8_textprep.c		${DST_LIB}/libspl/
cp ${SRC_UCM}/os/list.c				${DST_LIB}/libspl/
cp ${SRC_UCM}/sys/list.h			${DST_LIB}/libspl/include/sys/
cp ${SRC_UCM}/sys/list_impl.h			${DST_LIB}/libspl/include/sys/

echo "* zfs/lib/libzcommon"
mkdir -p ${DST_LIB}/libzcommon/include/sys/fs/
mkdir -p ${DST_LIB}/libzcommon/include/sys/fm/fs/
cp ${SRC_CM}/zfs/*.c				${DST_LIB}/libzcommon/
cp ${SRC_CM}/zfs/*.h				${DST_LIB}/libzcommon/include/
cp ${SRC_UCM}/sys/fs/zfs.h			${DST_LIB}/libzcommon/include/sys/fs/
cp ${SRC_UCM}/sys/fm/fs/zfs.h			${DST_LIB}/libzcommon/include/sys/fm/fs/

echo "* zfs/lib/libzpool"
mkdir -p ${DST_LIB}/libzpool/include/sys/
cp ${SRC_LIB}/libzpool/common/kernel.c		${DST_LIB}/libzpool/
cp ${SRC_LIB}/libzpool/common/taskq.c		${DST_LIB}/libzpool/
cp ${SRC_LIB}/libzpool/common/util.c		${DST_LIB}/libzpool/
#cp ${SRC_LIB}/libzpool/common/sys/zfs_context.h	${DST_LIB}/libzpool/include/sys/
cp ${SRC_ZLIB}/*.c				${DST_LIB}/libzpool/
cp ${SRC_UTS}/intel/zfs/spa_boot.c		${DST_LIB}/libzpool/
cp ${SRC_ZLIB}/sys/*.h				${DST_LIB}/libzpool/include/sys/
rm ${DST_LIB}/libzpool/vdev_disk.c
rm ${DST_LIB}/libzpool/include/sys/vdev_disk.h

echo "* zfs/lib/libzfs"
mkdir -p ${DST_LIB}/libzfs/include/
cp ${SRC_LIB}/libzfs/common/*.c			${DST_LIB}/libzfs/
cp ${SRC_LIB}/libzfs/common/*.h			${DST_LIB}/libzfs/include/

echo "* zfs/zcmd/zpool"
mkdir -p ${DST_CMD}/zpool
cp ${SRC_CMD}/zpool/*.c				${DST_CMD}/zpool/
cp ${SRC_CMD}/zpool/*.h				${DST_CMD}/zpool/

echo "* zfs/zcmd/zfs"
mkdir -p ${DST_CMD}/zfs
cp ${SRC_CMD}/zfs/*.c				${DST_CMD}/zfs/
cp ${SRC_CMD}/zfs/*.h				${DST_CMD}/zfs/

echo "* zfs/zcmd/zdb"
mkdir -p ${DST_CMD}/zdb/
cp ${SRC_CMD}/zdb/*.c				${DST_CMD}/zdb/

echo "* zfs/zcmd/zdump"
mkdir -p ${DST_CMD}/zdump
cp ${SRC_CMD}/zdump/*.c				${DST_CMD}/zdump/

echo "* zfs/zcmd/zinject"
mkdir -p ${DST_CMD}/zinject
cp ${SRC_CMD}/zinject/*.c			${DST_CMD}/zinject/
cp ${SRC_CMD}/zinject/*.h			${DST_CMD}/zinject/

echo "* zfs/zcmd/ztest"
mkdir -p ${DST_CMD}/ztest
cp ${SRC_CMD}/ztest/*.c				${DST_CMD}/ztest/

rm -Rf ${SRC}
