#!/bin/sh

. /lib/dracut-lib.sh

# Rather than wait for settled, let's just keep checking for /dev/zfs before we start.
if [ ! -c /dev/zfs ] ; then
  info "ZFS: Waiting for /dev/zfs..."
  return 1
fi

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
    rootfs=""
    rootok=0
    pool=""
    
    warn "ZFS: No bootfs attribute found in importable pools."
    
    # Re-export everything since we're not prepared to take responsibility for them
    zpool list -H | while read fs rest ; do
      zpool export "$fs"
    done
    
    return 1
  else
    rootfs="zfs"
    rootok=1
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
    
    return 0
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
      
      rootfs=""
      rootok=0
      
      return 1
    fi
    
    # We're good!
    return 0
  else
    info "ZFS: Pool ${pool} imported by module."
    
    # We're good!
    return 0
  fi
fi
