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
  at the OpenZFS issue tracker at https://github.com/openzfs/zfs/issues.

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
  at the beginning of the "zfs-functions" file which sets up the
  logging output.

INSTALLING INIT SCRIPT LINKS
  To setup the init script links in /etc/rc?.d manually on a Debian GNU/Linux
  (or derived) system, run the following commands (the order is important!):

    update-rc.d zfs-import start 07 S .       stop 07 0 1 6 .
    update-rc.d zfs-mount  start 02 2 3 4 5 . stop 06 0 1 6 .
    update-rc.d zfs-zed    start 07 2 3 4 5 . stop 08 0 1 6 .
    update-rc.d zfs-share  start 27 2 3 4 5 . stop 05 0 1 6 .

  To do the same on RedHat, Fedora and/or CentOS:

    chkconfig zfs-import
    chkconfig zfs-mount
    chkconfig zfs-zed
    chkconfig zfs-share

  On Gentoo:

    rc-update add zfs-import boot
    rc-update add zfs-mount boot
    rc-update add zfs-zed default
    rc-update add zfs-share default

  The idea here is to make sure all of the ZFS filesystems, including possibly
  separate datasets like /var, are mounted before anything else is started.

  Then, ZED, which depends on /var, can be started.  It will consume and act
  on events that occurred before it started.  ZED may also play a role in
  sharing filesystems in the future, so it is important to start before the
  'share' service.

  Finally, we share filesystems configured with the share\* property.
