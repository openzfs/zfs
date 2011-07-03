#!/bin/sh

. /lib/dracut-lib.sh

case "$root" in
  zfs:*)
    info "ZFS: Detecting & importing pool for ${root}"

    # We have ZFS modules loaded, so we're able to import pools now.
    if [ "$root" = "zfs:AUTO" ] ; then
      # Need to parse bootfs attribute
      info "ZFS: Attempting to detect root from imported ZFS pools."

      # Might be imported by the kernel module, so try searching before we import anything.
      zfsbootfs=`zpool list -H -o bootfs | sed 'q'`
      if [ "$zfsbootfs" = "" ] ; then
        # Not there, so we need to import everything.
        info "ZFS: Attempting to import additional pools."
        zpool import -N -a
        zfsbootfs=`zpool list -H -o bootfs | sed 'q'`
      fi
      if [ "$zfsbootfs" = "" ] ; then
        rootok=0
        pool=""

        warn "ZFS: No bootfs attribute found in importable pools."

        # Re-export everything since we're not prepared to take responsibility for them
        zpool list -H | while read fs rest ; do
          zpool export "$fs"
        done

        return 1
      else
        root="zfs:$zfsbootfs"
        pool="${root%%/*}"

        info "ZFS: Using ${zfsbootfs} as root."

        # Export everything but our root again.
        # FIXME: Ideally, we shouldn't export it unless we imported it -- IE anything brought
        # in by the kernel module should stay imported.
        zpool list -H | while read fs rest ; do
          if [ "$pool" != "$fs" ] ; then
            zpool export "$fs"
          fi
        done
      fi
    else
      # Should have an explicit pool set, so just import it and we're done.
      zfsboot="${root#zfs:}"
      pool="${zfsboot%%/*}"
      if zpool list -H $pool > /dev/null ; then
        # pool wasn't imported automatically by the kernel module, so try it manually.
        info "ZFS: Importing pool ${pool}..."
        if zpool import -N $pool ; then 
          warn "ZFS: Unable to import pool ${pool}."
          rootok=0
        
          return 1
        fi
      else
        info "ZFS: Pool ${pool} imported by module."
      fi
    fi
  
    # Above should have left our rpool imported and rpool/fs in $root.
    zfsboot="${root#zfs:}"
    mount -o zfsutil -t zfs "$zfsboot" "$NEWROOT"
    if [ "$?" = "0" ] ; then
        ROOTFS_MOUNTED=yes
    else
        mount -t zfs "$zfsboot" "$NEWROOT" && ROOTFS_MOUNTED=yes
    fi
    ;;
esac
