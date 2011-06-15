Native ZFS for Linux! ZFS is an advanced file system and volume manager
which was originally developed for Solaris. It has been successfully 
ported to FreeBSD and now there is a functional Linux ZFS kernel port
too. The port currently includes a fully functional and stable SPA, DMU,
and ZVOL with a ZFS Posix Layer (ZPL) on the way!

    $ ./configure
    $ make pkg

Full documentation for building, configuring, and using ZFS can be
found at: <http://zfsonlinux.org>

WTF: Why The Fork?
==================

This fork exists for testing changes to improve integration of the ZFSonLinux port with Gentoo.  The intent is for these changes to be merged back to origin once the changes are tested & stable enough to work on Gentoo without negative effect on other distros.

Current efforts on this fork include:

 * Make build FHS compliant (move zfs, zpool et al. to /s?bin instead of /usr/s?bin).
 * Integrate ZFS support into genkernel created initramfs.
 * Support booting from ZFS root w/ /usr on separate ZFS from ROOT.

Longer range goals include:

 * Get /boot on ZFS, IE Grub boot from ZFS without need for /boot on MD-based RAID-1.


Root on ZFS
-----------

My rpool layout looks like this:

	rpool              5.64G  57.9G    21K  /
	rpool/ROOT          136M  57.9G   136M  legacy
	rpool/home           30K  57.9G    30K  /home
	rpool/usr          5.36G  57.9G  1.19G  /usr
	rpool/usr/local    70.5M  57.9G  70.5M  /usr/local
	rpool/usr/portage  1.27G  57.9G   316M  /usr/portage
	rpool/usr/src      2.83G  57.9G  2.82G  /usr/src
	rpool/var          89.2M  57.9G  84.7M  /var
	rpool/var/log      4.13M  57.9G  3.54M  /var/log

Key points are /bin, /sbin, /etc, /dev/, /sys, /lib*, /proc all on bootfs, all made available by mounting root in the initramf.  /usr (and others) are on separate zfs made available early in the boot process inside new_root.

Note that the root of rpool itself is set canmount=off, rpool/ROOT is the actual root filesystem (and is the bootfs rpool), and mountpoint on rpool is '/' so all sub-ZFS (except ROOT) inherit proper mountpoints.
