#!/bin/bash

PROG=update-zfs.sh
ZFS_SRC=http://dlc.sun.com/osol/on/downloads/b89/on-src.tar.bz2

die() {
	rm -Rf $SRC
	echo "${PROG}: $1" >&2
	exit 1
}

DEST=`pwd`
if [ `basename $DEST` != "scripts" ]; then
	die "Must be run from scripts directory"
fi

SRC=`mktemp -d /tmp/zfs.XXXXXXXXXX`
DEST=`dirname $DEST`
DATE=`date +%Y%m%d%H%M%S`

wget $ZFS_SRC

echo "--- Updating ZFS source ---"
echo
echo "ZFS_REPO       = $ZFS_REPO"
echo "ZFS_PATCH_REPO = $ZFS_PATCH_REPO"
echo "SRC            = $SRC"
echo "DEST           = $DEST"

echo
echo "--- Cloning $ZFS_REPO ---"
cd $SRC || die "Failed to 'cd $SRC'"
hg clone $ZFS_REPO || die "Failed to clone $ZFS_REPO"

echo
echo "--- Cloning $ZFS_PATCH_REPO ---"
hg clone $ZFS_PATCH_REPO patches || die "Failed to clone $ZFS_PATCH_REPO"

echo
echo "--- Backing up existing files ---"
echo "$DEST/zfs -> $DEST/zfs.$DATE"
cp -Rf $DEST/zfs $DEST/zfs.$DATE || die "Failed to backup"
echo "$DEST/zfs_patches -> $DEST/zfs_patches.$DATE"
cp -Rf $DEST/zfs_patches $DEST/zfs_patches.$DATE || die "Failed to backup"

echo
echo "--- Overwriting $DEST/zfs and $DEST/zfs_patches ---"
find $SRC/trunk/src/ -name SConstruct -type f -print | xargs /bin/rm -f
find $SRC/trunk/src/ -name SConscript -type f -print | xargs /bin/rm -f
find $SRC/trunk/src/ -name *.orig -type f -print | xargs /bin/rm -f
rm -f $SRC/trunk/src/myconfig.py
cp -Rf $SRC/trunk/src/* $DEST/zfs || die "Failed to overwrite"
cp -Rf $SRC/patches/*.patch $DEST/zfs_patches/patches/ || die "Failed to overwrite"
cp -f $SRC/patches/series $DEST/zfs_patches/series/zfs-lustre

echo
echo "--- Removing $SRC ---"
rm -Rf $SRC

