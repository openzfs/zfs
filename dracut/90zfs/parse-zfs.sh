#!/bin/sh

# This seems to not be ready in time for some reason...
ZFS_DEV=/dev/zfs
if [ ! -c ${ZFS_DEV} ]; then
  modprobe zfs
  
  COUNT=0
  DELAY=5
  while [ ! -e ${ZFS_DEV} ]; do
		if [ ${COUNT} -gt ${DELAY} ]; then
		  echo "ZFS: Module didn't create /dev/zfs.  This isn't likely to end well..."
			break
		fi

		let COUNT=${COUNT}+1
	  sleep 1
	done
fi

case "$root" in
  ZFS\=*|zfs:*|zfs:FILESYSTEM\=*|FILESYSTEM\=*)
    # root is explicit ZFS root. Try to import the pool.
    # We can handle a root=... param in any of the following formats:
    # root=ZFS=rpool/ROOT
    # root=zfs:rpool/ROOT
    # root=zfs:FILESYSTEM=rpool/ROOT
    # root=FILESYSTEM=rpool/ROOT
    
    # Strip down to just the pool/fs
    zfsbootfs="${root#zfs:}"
    zfsbootfs="${zfsbootfs#FILESYSTEM=}"
    zfsbootfs="${zfsbootfs#ZFS=}"
    
    pool="${zfsbootfs%%/*}"
    zpool list -H $pool > /dev/null
    if [ "$?" -eq "1" ] ; then
      # pool wasn't imported automatically by the kernel module, so try it manually.
      zpool import -N $pool || { echo "ZFS: Unable to import root pool '${pool}'." ; continue ; }
    fi
    rootfs="zfs"
    rootok=1
    root="${zfsbootfs}"
    echo "ZFS: Using ${zfsbootfs} as root."
    ;;
  "")
    # No root set, so try to find it from bootfs attribute.
    echo "ZFS: Attempting to detect root from imported ZFS pools."

    # Might be imported by the kernel module, so try searching before we import anything.
    zfsbootfs=`zpool list -H -o bootfs | sed 'q'`
    if [ "x$zfsbootfs" = 'x' ] ; then
      # Not there, so we need to import everything.
      echo "ZFS: Attempting to import additional pools."
      zpool import -N -a
      zfsbootfs=`zpool list -H -o bootfs | sed 'q'`
    fi
    if [ "x$zfsbootfs" = 'x' ] ; then
      pool=""
      echo "ZFS: No bootfs attribute found in importable pools."
    else
      rootfs="zfs"
      rootok=1
      root="$zfsbootfs"
      pool="${root%%/*}"
      echo "ZFS: Using ${zfsbootfs} as root."
    fi

    # Export everything but our root again.
    # FIXME: Ideally, we shouldn't export it unless we imported it -- IE anything brought
    # in by the kernel module should stay imported.
    zpool list -H | while read fs rest ; do
      if [ "$pool" != "$fs" ] ; then
        zpool export "$fs"
      fi
    done
    ;;
    # We leave the rpool imported since exporting it can cause things to mount
    # badly later on.
esac
