DESCRIPTION
  These scripts is intended to be used with initramfs-tools, which is a similar
  software product to "dracut" (which is more used in RedHat based distributions,
  and is mainly used by Debian GNU/Linux and derivates to create a initramfs so
  that the system can be booted of a ZFS filesystem. If you have no need or
  interest for this, then it can safely be ignored.

  These script were written with the primary intention of being portable and
  usable on as many systems as possible.

  This is, in practice, usually not possible. But the intention is there.
  And it is a good one.

  They have been tested successfully on:

    * Debian GNU/Linux Wheezy
    * Debian GNU/Linux Jessie

  It uses some functionality common with the SYSV init scripts, primarily
  the "/etc/zfs/zfs-functions" script.

FUNCTIONALITY
  * Supports booting of a ZFS snapshot.
    Do this by cloning the snapshot into a dataset. If this, the resulting
    dataset, already exists, destroy it. Then mount it as the root filesystem.
    * If snapshot does not exist, use base dataset (the part before '@')
      as boot filesystem instead.
    * Clone with 'mountpoint=none' and 'canmount=noauto' - we mount manually
      and explicitly.
    * Allow rollback of snapshots instead of clone it and boot from the clone.
    * If no snapshot is specified on the 'root=' kernel command line, but
      there is an '@', then get a list of snapshots below that filesystem
      and ask the user which to use.

  * Support all currently used kernel command line arguments
    * Core options:
      All the different distributions have their own standard on what to specify
      on the kernel command line to boot of a ZFS filesystem.

      Supports the following kernel command line argument combinations
      (in this order - first match win):
      * rpool=<pool>			(tries to finds bootfs automatically)
      * bootfs=<pool>/<dataset>		(uses this for rpool - first part)
      * rpool=<pool> bootfs=<pool>/<dataset>
      * -B zfs-bootfs=<pool>/<fs>	(uses this for rpool - first part)
      * rpool=rpool			(default if none of the above is used)
      * root=<pool>/<dataset>		(uses this for rpool - first part)
      * root=ZFS=<pool>/<dataset>	(uses this for rpool - first part, without 'ZFS=')
      * root=zfs:AUTO			(tries to detect both pool and rootfs
      * root=zfs:<pool>/<dataset>	(uses this for rpool - first part, without 'zfs:')

      Option <dataset> could also be <snapshot>
    * Extra (control) options:
      * zfsdebug=(on,yes,1)   Show extra debugging information
      * zfsforce=(on,yes,1)   Force import the pool
      * rollback=(on,yes,1)   Rollback (instead of clone) the snapshot

  * 'Smarter' way to import pools. Don't just try cache file or /dev.
    * Try to use /dev/disk/by-vdev (if /etc/zfs/vdev_id.conf exists),
    * Try /dev/mapper (to be able to use LUKS backed pools as well as
      multi-path devices).
    * /dev/disk/by-id and any other /dev/disk/by-* directory that may exist.
    * Use /dev as a last ditch attempt.
    * Fallback to using the cache file if that exist if nothing else worked.
    * Only try to import pool if it haven't already been imported
      * This will negate the need to force import a pool that have not been
        exported cleanly.
      * Support exclusion of pools to import by setting ZFS_POOL_EXCEPTIONS
         in /etc/default/zfs.

    Controlling in which order devices is searched for is controlled by
    ZPOOL_IMPORT_PATH variable set in /etc/defaults/zfs.

  * Support additional configuration variable ZFS_INITRD_ADDITIONAL_DATASETS
    to mount additional filesystems not located under your root dataset.

    For example, if the root fs is specified as 'rpool/ROOT/rootfs', it will
    automatically and without specific configuration mount any filesystems
    below this on the mount point specified in the 'mountpoint' property.
    Such as 'rpool/root/rootfs/var', 'rpool/root/rootfs/usr' etc)

    However, if one prefer to have separate filesystems, not located below
    the root fs (such as 'rpool/var', 'rpool/ROOT/opt' etc), special
    configuration needs to be done. This is what the variable, set in
    /etc/defaults/zfs file, needs to be configured. The 'mountpoint'
    property needs to be correct for this to work though.

  * Allows mounting a rootfs with mountpoint=legacy set.

  * Include /etc/modprobe.d/{zfs,spl}.conf in the initrd if it/they exist.

  * Include the udev rule to use by-vdev for pool imports.

  * Include the /etc/default/zfs file to the initrd.
