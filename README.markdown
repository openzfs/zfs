Native ZFS for Linux! ZFS is an advanced file system and volume manager
which was originally developed for Solaris. It has been successfully 
ported to FreeBSD and now there is a functional Linux ZFS kernel port
too. The port currently includes a fully functional and stable SPA, DMU,
and ZVOL with a ZFS Posix Layer (ZPL) on the way!

    $ ./configure
    $ make pkg

To copy the kernel code inside your kernel source tree for builtin
compilation:

    $ ./configure --enable-linux-builtin --with-linux=/usr/src/linux-...
    $ ./copy-builtin /usr/src/linux-...

Full documentation for building, configuring, and using ZFS can be
found at: <http://zfsonlinux.org>
