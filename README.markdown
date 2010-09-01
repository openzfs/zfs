Native ZFS for Linux! ZFS is an advanced file system and volume manager
which was originally developed for Solaris. It has been successfully 
ported to FreeBSD and now there is a functional Linux ZFS kernel port
too. The port currently includes a fully functional and stable SPA, DMU,
and ZVOL with a ZFS Posix Layer (ZPL) on the way!

To build packages for your distribution, first build and install the
SPL packages <http://wiki.github.com/behlendorf/spl/> then:

$ ./configure
$ make pkg

Full documentation for building, configuring, and using ZFS can be
found at: <http://wiki.github.com/behlendorf/zfs/>
