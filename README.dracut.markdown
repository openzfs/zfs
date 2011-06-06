How to setup a zfs root filesystem using dracut
-----------------------------------------------

1) Install the zfs-dracut package.  This package adds a zfs dracut module
to the /usr/share/dracut/modules.d/ directory which allows dracut to
create an initramfs which is zfs aware.

2) Set the bootfs property for the bootable dataset in the pool.  Then set
the dataset mountpoint property to '/' or to 'legacy'.

    $ zpool set bootfs=pool/dataset
    $ zfs set mountpoint=/ pool/dataset

This ought to be enough to get your system to boot from dracut.  However,
this technique runs into the problem that the 'root=' option in the kernel
command line of your grub.conf/menu.lst will not match the stated mount
device after boot, so new kernel upgrades will not work correctly.

To prevent that from happening, change your kernel 'root=' option in your
grub.conf/menu.lst to point to your boot file system:

    $ grub.conf/menu.lst: kernel ... root=pool/dataset

Dracut will be smart enough to detect that you were referring not to a
block device but to a ZFS file system (as long as the containing pool
is present and importable during boot).  If you do this, setting the
bootfs property is optional (but will still be honored in case the 'root='
option in the kernel command line is not present or is invalid).

The syntaxes 'root=ZFS=pool/dataset', 'root=FILESYSTEM=pool/dataset' are
also supported, but will not work with kernel upgrades.  They are merely
a way to assert that the root parameter refers to, in fact and
exclusively, a ZFS file system.

3) To set zfs module options put them in /etc/modprobe.d/zfs.conf file.
The complete list of zfs module options is available by running the
_modinfo zfs_ command.  Commonly set options include: zfs_arc_min,
zfs_arc_max, zfs_prefetch_disable, and zfs_vdev_max_pending.

4) Finally, create your new initramfs by running dracut.

    $ dracut --force /path/to/initramfs kernel_version
