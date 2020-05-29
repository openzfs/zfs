How to setup a zfs root filesystem using dracut
-----------------------------------------------

1) Install the zfs-dracut package.  This package adds a zfs dracut module
to the /usr/share/dracut/modules.d/ directory which allows dracut to
create an initramfs which is zfs aware.

2) Set the bootfs property for the bootable dataset in the pool.  Then set
the dataset mountpoint property to '/'.

    $ zpool set bootfs=pool/dataset pool
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

Kernel Command Line
-------------------

The initramfs' behavior is influenced by the following kernel command line
parameters passed in from the boot loader:

* `root=...`: If not set, importable pools are searched for a bootfs
attribute.  If an explicitly set root is desired, you may use
`root=ZFS:pool/dataset`

* `zfs_force=0`: If set to 1, the initramfs will run `zpool import -f` when
attempting to import pools if the required pool isn't automatically imported
by the zfs module.  This can save you a trip to a bootcd if hostid has
changed, but is dangerous and can lead to zpool corruption, particularly in
cases where storage is on a shared fabric such as iSCSI where multiple hosts
can access storage devices concurrently.  _Please understand the implications
of force-importing a pool before enabling this option!_

* `spl_hostid`: By default, the hostid used by the SPL module is read from
/etc/hostid inside the initramfs.  This file is placed there from the host
system when the initramfs is built which effectively ties the ramdisk to the
host which builds it.  If a different hostid is desired, one may be set in
this attribute and will override any file present in the ramdisk.  The
format should be hex exactly as found in the `/etc/hostid` file, IE
`spl_hostid=0x00bab10c`.

Note that changing the hostid between boots will most likely lead to an
un-importable pool since the last importing hostid won't match.  In order
to recover from this, you may use the `zfs_force` option or boot from a
different filesystem and `zpool import -f` then `zpool export` the pool
before rebooting with the new hostid.

* `bootfs.snapshot`: If listed, enables the zfs-snapshot-bootfs service on a Dracut system. The zfs-snapshot-bootfs service simply runs `zfs snapshot $BOOTFS@%v` after the pool has been imported but before the bootfs is mounted. `$BOOTFS` is substituted with the value of the bootfs setting on the pool. `%v` is substituted with the version string of the kernel currently being booted (e.g. 5.6.6-200.fc31.x86\_64). Failure to create the snapshot (e.g. because one with the same name already exists) will be logged, but will not otherwise interrupt the boot process.

    It is safe to leave the bootfs.snapshot flag set persistently on your kernel command line so that a new snapshot of your bootfs will be created on every kernel update. If you leave bootfs.snapshot set persistently on your kernel command line, you may find the below script helpful for automatically removing old snapshots of the bootfs along with their associated kernel.

        #!/usr/bin/sh

        if [[ "$1" == "remove" ]] && grep -q "\bbootfs.snapshot\b" /proc/cmdline; then
           zfs destroy $(findmnt -n -o source /)@$2 &> /dev/null
        fi

        exit 0

    To use the above script place it in a plain text file named /etc/kernel/install.d/99-zfs-cleanup.install and mark it executable with the following command:

        $ chmod +x /etc/kernel/install.d/99-zfs-cleanup.install

    On Red Hat based systems, you can change the value of `installonly_limit` in /etc/dnf/dnf.conf to adjust the number of kernels and their associated snapshots that are kept.

* `bootfs.snapshot=<snapname>`: Is identical to the bootfs.snapshot parameter explained above except that the value substituted for \<snapname\> will be used when creating the snapshot instead of the version string of the kernel currently being booted. 

* `bootfs.rollback`: If listed, enables the zfs-rollback-bootfs service on a Dracut system. The zfs-rollback-bootfs service simply runs `zfs rollback -Rf $BOOTFS@%v` after the pool has been imported but before the bootfs is mounted. If the rollback operation fails, the boot process will be interrupted with a Dracut rescue shell. __Use this parameter with caution. Intermediate snapshots of the bootfs will be destroyed!__ TIP: Keep your user data (e.g. /home) on separate file systems (it can be in the same pool though).

* `bootfs.rollback=<snapname>`: Is identical to the bootfs.rollback parameter explained above except that the value substituted for \<snapname\> will be used when rolling back the bootfs instead of the version string of the kernel currently being booted. If you use this form, choose a snapshot that is new enough to contain the needed kernel modules under /lib/modules or use a kernel that has all the needed modules built-in.

How it Works
============

The Dracut module consists of the following files (less Makefile's):

* `module-setup.sh`: Script run by the initramfs builder to create the
ramdisk.  Contains instructions on which files are required by the modules
and z* programs.  Also triggers inclusion of `/etc/hostid` and the zpool
cache.  This file is not included in the initramfs.

* `90-zfs.rules`: udev rules which trigger loading of the ZFS modules at boot.

* `zfs-lib.sh`: Utility functions used by the other files.

* `parse-zfs.sh`: Run early in the initramfs boot process to parse kernel
command line and determine if ZFS is the active root filesystem.

* `mount-zfs.sh`: Run later in initramfs boot process after udev has settled
to mount the root dataset.

* `export-zfs.sh`: Run on shutdown after dracut has restored the initramfs
and pivoted to it, allowing for a clean unmount and export of the ZFS root.

`zfs-lib.sh`
------------

This file provides a few handy functions for working with ZFS. Those
functions are used by the `mount-zfs.sh` and `export-zfs.sh` files.
However, they could be used by any other file as well, as long as the file
sources `/lib/dracut-zfs-lib.sh`.

`module-setup.sh`
-----------------

This file is run by the Dracut script within the live system, not at boot
time.  It's not included in the final initramfs.  Functions in this script
describe which files are needed by ZFS at boot time.

Currently all the various z* and spl modules are included, a dependency is
asserted on udev-rules, and the various zfs, zpool, etc. helpers are included.
Dracut provides library functions which automatically gather the shared libs
necessary to run each of these binaries, so statically built binaries are
not required.

The zpool and zvol udev rules files are copied from where they are
installed by the ZFS build.  __PACKAGERS TAKE NOTE__: If you move
`/etc/udev/rules/60-z*.rules`, you'll need to update this file to match.

Currently this file also includes `/etc/hostid` and `/etc/zfs/zpool.cache`
which means the generated ramdisk is specific to the host system which built
it.  If a generic initramfs is required, it may be preferable to omit these
files and specify the `spl_hostid` from the boot loader instead.

`parse-zfs.sh`
--------------

Run during the cmdline phase of the initramfs boot process, this script
performs some basic sanity checks on kernel command line parameters to
determine if booting from ZFS is likely to be what is desired.  Dracut
requires this script to adjust the `root` variable if required and to set
`rootok=1` if a mountable root filesystem is available.  Unfortunately this
script must run before udev is settled and kernel modules are known to be
loaded, so accessing the zpool and zfs commands is unsafe.

If the root=ZFS... parameter is set on the command line, then it's at least
certain that ZFS is what is desired, though this script is unable to
determine if ZFS is in fact available.  This script will alter the `root`
parameter to replace several historical forms of specifying the pool and
dataset name with the canonical form of `zfs:pool/dataset`.

If no root= parameter is set, the best this script can do is guess that
ZFS is desired.  At present, no other known filesystems will work with no
root= parameter, though this might possibly interfere with using the
compiled-in default root in the kernel image.  It's considered unlikely
that would ever be the case when an initramfs is in use, so this script
sets `root=zfs:AUTO` and hopes for the best.

Once the root=... (or lack thereof) parameter is parsed, a dummy symlink
is created from `/dev/root` -> `/dev/null` to satisfy parts of the Dracut
process which check for presence of a single root device node.

Finally, an initqueue/finished hook is registered which causes the initqueue
phase of Dracut to wait for `/dev/zfs` to become available before attempting
to mount anything.

`mount-zfs.sh`
--------------

This script is run after udev has settled and all tasks in the initqueue
have succeeded.  This ensures that `/dev/zfs` is available and that the
various ZFS modules are successfully loaded.  As it is now safe to call
zpool and friends, we can proceed to find the bootfs attribute if necessary.

If the root parameter was explicitly set on the command line, no parsing is
necessary.  The list of imported pools is checked to see if the desired pool
is already imported.  If it's not, and attempt is made to import the pool
explicitly, though no force is attempted.  Finally the specified dataset
is mounted on `$NEWROOT`, first using the `-o zfsutil` option to handle
non-legacy mounts, then if that fails, without zfsutil to handle legacy
mount points.

If no root parameter was specified, this script attempts to find a pool with
its bootfs attribute set.  First, already-imported pools are scanned and if
an appropriate pool is found, no additional pools are imported.  If no pool
with bootfs is found, any additional pools in the system are imported with
`zpool import -N -a`, and the scan for bootfs is tried again.  If no bootfs
is found with all pools imported, all pools are re-exported, and boot fails.
Assuming a bootfs is found, an attempt is made to mount it to `$NEWROOT`,
first with, then without the zfsutil option as above.

Ordinarily pools are imported _without_ the force option which may cause
boot to fail if the hostid has changed or a pool has been physically moved
between servers.  The `zfs_force` kernel parameter is provided which when
set to `1` causes `zpool import` to be run with the `-f` flag.  Forcing pool
import can lead to serious data corruption and loss of pools, so this option
should be used with extreme caution.  Note that even with this flag set, if
the required zpool was auto-imported by the kernel module, no additional
`zpool import` commands are run, so nothing is forced.

`export-zfs.sh`
---------------

Normally the zpool containing the root dataset cannot be exported on
shutdown as it is still in use by the init process. To work around this,
Dracut is able to restore the initramfs on shutdown and pivot to it.
All remaining process are then running from a ramdisk, allowing for a
clean unmount and export of the ZFS root. The theory of operation is
described in detail in the [Dracut manual](https://www.kernel.org/pub/linux/utils/boot/dracut/dracut.html#_dracut_on_shutdown).

This script will try to export all remaining zpools after Dracut has
pivoted to the initramfs. If an initial regular export is not successful,
Dracut will call this script once more with the `final` option,
in which case a forceful export is attempted.

Other Dracut modules include similar shutdown scripts and Dracut
invokes these scripts round-robin until they succeed. In particular,
the `90dm` module installs a script which tries to close and remove
all device mapper targets. Thus, if there are ZVOLs containing
dm-crypt volumes or if the zpool itself is backed by a dm-crypt
volume, the shutdown scripts will try to untangle this.
