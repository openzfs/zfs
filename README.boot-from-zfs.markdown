#How to get your Fedora 16 system on a ZFS root pool, and to boot from it normally


##Requisites

- Internet access on the machine you will be doing this.
- A mechanism to boot your computer with Fedora 16 that does not reside on the drive you will be installing (live installer or rescue disk with support for yum).

##Procedure

###Preparation of the bootstrap system

- Connect the disk where you will be creating the ZFS pool and file systems.

- Boot your machine using the mechanism described above.

- On this booted system, generate a `hostid`:

        genhostid

- Install the requisite packages:

        yum install -y kernel-devel libuuid-devel zlib-devel

###Installation of ZFS on the bootstrap system    
    
- Download the sources of spl and zfs to the /root directory:

        cd /root
        git clone http://github.com/Rudd-O/spl
        git clone http://github.com/Rudd-O/zfs

- Configure and install spl and zfs:

        cd spl
        ./configure --sbindir=/sbin --bindir=/bin --mandir=/usr/share/man --sysconfdir=/etc --libdir=/lib64 --docdir=/usr/share/doc --sharedstatedir=/var --datadir=/usr/share --includedir=/usr/include
        make
        make install
        cd ..
        cd zfs
        ./configure --sbindir=/sbin --bindir=/bin --mandir=/usr/share/man --sysconfdir=/etc --libdir=/lib64 --docdir=/usr/share/doc --sharedstatedir=/var --datadir=/usr/share --includedir=/usr/include --with-udevdir=/lib/udev
        make
        make install
        cd ..

- Test for ZFS support:

        zpool

###Creation of file systems on the target device

- Find the block device where you will be installing your new Fedora system -- we will assume `/dev/sda`:

- Partition it according to the following list.  Make sure that the partitions are aligned to 1MB boundaries.  The first partition ought to start at sector 2048 as well.

1. 8 MB partition for `/boot/grub2` (type BF, bootable)
2. swap partition (type 82)
3. ZFS partition (type BE)

- Create non-ZFS file systems:

        mkswap /dev/sda2
        mke2fs /dev/sda1

- Create ZFS file systems and set the appropriate properties:

        zpool create syspool /dev/sda3 -o ashift=12
        zfs set canmount=off syspool
        zfs set mountpoint=none syspool
        
        zfs create syspool/RPOOL
        zfs set canmount=off syspool/RPOOL
        zfs set mountpoint=none syspool/RPOOL
        
        zfs create syspool/RPOOL/fedora-16
        zfs set canmount=noauto syspool/RPOOL/fedora-16
        zfs set mountpoint=/newroot syspool/RPOOL/fedora-16

- Set the `bootfs` property on your pool:

        zpool set bootfs=syspool/RPOOL/fedora-16

- Mount the root dataset to the `/newroot` filesystem:

        zfs mount syspool/RPOOL/fedora-16

- Create the `/boot` dataset:

        zfs create syspool/RPOOL/fedora-16/boot

- Create the directory `/boot/grub2`

        mkdir /newroot/boot/grub2

- Mount the ext2 filesystem there:

        mount /dev/sda1 /newroot/boot/grub2

- Create the ZFS file systems you see pertinent.  Example:

        zfs create syspool/RPOOL/fedora-16/usr
        zfs create syspool/sharedhome

###Installation of the new system

- Install your operating system to `/newroot`, using whatever facilities you desire -- for example, rsync from backups, use the Fedora installer, or use a yum bootstrapper.  I leave it unspecified because I restored my earlier system from another machine.

###Post-install preparation of the new system

- Get and write down the UUIDs of `/dev/sda1` and `/dev/sda2`:

        blkid /dev/sda1
        blkid /dev/sda2

- Make sure that your device files are available inside `/newroot/dev`:

        rsync -axv --delete /dev/ /newroot/dev/

- Make sure that your `hostid` is placed in `/newroot/etc`:

        cp /etc/hostid /newroot/etc/hostid

- Make sure that your `zpool.cache` is placed in `/newroot/etc/zfs`

        mkdir -p /newroot/etc/zfs
        cp /etc/zfs/zpool.cache /newroot/etc/zfs/zpool.cache

- Copy the zfs and spl sources into `/newroot/root`:

        rsync -av /root/zfs /root/spl /newroot/root/

- Chroot to `/newroot`:

        chroot /newroot

- Mount pseudo file systems:

        mount -t proc proc /proc
        mount -t sysfs sysfs /sys

###Installation of ZFS in the chroot

- Install the requisite packages from inside the chroot:

        yum install -y kernel-devel libuuid-devel zlib-devel

- Configure, make and install spl and zfs:

        cd /root
        cd spl
        ./configure --sbindir=/sbin --bindir=/bin --mandir=/usr/share/man --sysconfdir=/etc --libdir=/lib64 --docdir=/usr/share/doc --sharedstatedir=/var --datadir=/usr/share --includedir=/usr/include
        make
        make install
        cd ..
        cd zfs
        ./configure --sbindir=/sbin --bindir=/bin --mandir=/usr/share/man --sysconfdir=/etc --libdir=/lib64 --docdir=/usr/share/doc --sharedstatedir=/var --datadir=/usr/share --includedir=/usr/include --with-udevdir=/lib/udev
        make
        make install
        cd ..

- Patch files that GRUB2 installed in your system with the patches in the zfs source, directory  `grub2/`.

`GRUB_DEVICE` and `GRUB_DEVICE_BOOT` variables in `/sbin/grub2-mkconfig` reflect the devices backing / and /boot respectively, thus they should be adjusted as such.  For the purposes of this mini-HOWTO, we will set them to `zfs` since both the root and the boot file systems are ZFS datasets.

- Add support for systemd automounting of ZFS file systems on boot:

        gcc zfs/systemd-escape.c -o /lib/systemd/systemd-escaper
        cp zfs/systemd-zfs-generator /lib/systemd/system-generators

- Set the appropriate properties on your `fedora-16` filesystem now:

        zfs set mountpoint=/ syspool/RPOOL/fedora-16

- Regenerate the initramfs to include ZFS support:

        dracut -f /boot/initramfs-`uname -r`.img `uname -r`

- Verify that the initramfs includes ZFS support:

        lsinitrd /boot/initramfs-`uname -r` | grep -E '(zpool.cache|zfs|hostid)'

###ZFS-enabled boot setup

- Edit `/etc/fstab` to add the file systems:

        syspool/RPOOL/fedora-16  /  zfs defaults 0 0
        UUID=<blkid of sda1>  /boot/grub2   ext2 defaults 0 2
        UUID=<blkid of sda2>  swap    swap defaults  0 0

- Install GRUB2 to your MBR:

        grub2-install /dev/sda

- Generate a GRUB configuration file:

        /sbin/grub2-mkconfig > /boot/grub2/grub.cfg

- Check that the GRUB configuration file includes ZFS support (`zfs.mod` and `zfsinfo.mod`), and that the `root=zfs` parameter is present in the Linux kernel command lines.

###Final cleanup

- Exit the chroot jail, making sure no processes started from the chroot are still open:

        exit

- Clean up mounted file systems:

        cd /
        umount /newroot/boot/grub2
        umount /newroot/proc
        umount /newroot/sys
        zfs umount -a

###Reboot

- Sync:

        sync

- Power off the system abruptly.

- Power the system on again, having removed whatever media you used to boot the system before this installation.

Now your system should boot up, read the kernel and initramfs from ZFS, then proceed with dracut boot inside the initramfs.  Dracut boot should detect your pool, inspect its `bootfs` property, import your pool without mounting any file systems, then mount the `syspool/RPOOL/fedora-16` file system onto /sysroot, then pivot-root, then start systemd.  Systemd should detect all the other file systems in that pool, generate unit files for all the file systems and then mount them in the right locations and order.

Your Fedora 16 system is ready.  YOU ARE DONE.

##Note about yum and kernel upgrades

Watch out for kernel upgrades.  Every time there is a kernel upgrade, before you boot with the new kernel, you will need to reconfigure spl and zfs with the parameters `--with-linux=/usr/src/kernels/...` and `--with-linux-obj=/usr/src/kernels...` then install again, then regenerate the dracut initrd, every time you upgrade the kernel, 