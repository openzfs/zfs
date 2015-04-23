DESCRIPTION
  These script were written with the primary intention of being portable and
  usable on as many systems as possible.

  This is, in practice, usually not possible. But the intention is there.
  And it is a good one.

  They have been tested successfully on:

    * Debian GNU/Linux Wheezy
    * Debian GNU/Linux Jessie
    * Ubuntu Trusty
    * CentOS 6.0
    * CentOS 6.6
    * Gentoo

SUPPORT
  If you find that they don't work for your platform, please report this
  at the ZFS On Linux issue tracker at https://github.com/zfsonlinux/zfs/issues.

  Please include:

    * Distribution name
    * Distribution version
    * Where to find an install CD image
    * Architecture

  If you have code to share that fixes the problem, that is much better.
  But please remember to try your best keep portability in mind. If you
  suspect that what you're writing/modifying won't work on anything else
  than your distribution, please make sure to put that code in appropriate
  if/else/fi code.

  It currently MUST be bash (or fully compatible) for this to work.

  If you're making your own distribution and you want the scripts to
  work on that, the biggest problem you'll (probably) have is the part
  at the beginning of the "zfs-functions.in" file which sets up the
  logging output.

INSTALLING INIT SCRIPT LINKS
  To setup the init script links in /etc/rc?.d manually on a Debian GNU/Linux
  (or derived) system, run the following commands (the order is important!):

    update-rc.d zfs-zed    start 07 S .       stop 08 0 1 6 .
    update-rc.d zfs-import start 07 S .       stop 07 0 1 6 .
    update-rc.d zfs-mount  start 02 2 3 4 5 . stop 06 0 1 6 .
    update-rc.d zfs-share  start 27 2 3 4 5 . stop 05 0 1 6 .

  To do the same on RedHat, Fedora and/or CentOS:

    chkconfig zfs-zed
    chkconfig zfs-import
    chkconfig zfs-mount
    chkconfig zfs-share

  On Gentoo:

    rc-update add zfs-zed boot
    rc-update add zfs-import boot
    rc-update add zfs-mount boot
    rc-update add zfs-share default


  The idea here is to make sure ZED is started before the imports (so that
  we can start consuming pool events before pools are imported).

  Then import any/all pools (except the root pool which is mounted in the
  initrd before the system even boots - basically before the S (single-user)
  mode).

  Then we mount all filesystems before we start any network service (such as
  NFSd, AFSd, Samba, iSCSI targets and what not). Even if the share* in ZFS
  isn't used, the filesystem must be mounted for the service to start properly.

  Then, at almost the very end, we share filesystems configured with the
  share* property in ZFS.
