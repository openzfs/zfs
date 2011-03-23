How to setup a zfs root filesystem using dracut
-----------------------------------------------

1) Install the zfs-dracut package.  This package adds a zfs dracut module
to the /usr/share/dracut/modules.d/ directory which allows dracut to
create an initramfs which is zfs aware.

2) Set the bootfs property for the bootable dataset in the pool.  Then set
the dataset mountpoint property to '/'.

    $ zpool set bootfs=pool/dataset
    $ zfs set mountpoint=/ pool/dataset

Alternately, legacy mountpoints can be used by setting the 'root=' option
on the kernel line of your grub.conf/menu.lst configuration file.  Then
set the dataset mountpoint property to 'legacy'.

    $ grub.conf/menu.lst: kernel ... root=ZFS=pool/dataset
    $ zfs set mountpoint=legacy pool/dataset

3) To set zfs module options put them in /etc/modprobe.d/zfs.conf file.
The complete list of zfs module options is available by running the
_modinfo zfs_ command.  Commonly set options include: zfs_arc_min,
zfs_arc_max, zfs_prefetch_disable, and zfs_vdev_max_pending.

4) Finally, create your new initramfs by running dracut.

    $ dracut --force /path/to/initramfs kernel_version
