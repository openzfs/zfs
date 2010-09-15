The Solaris Porting Layer (SPL) is a Linux kernel module which provides
many of the Solaris kernel APIs.  This shim layer makes it possible to
run Solaris kernel code in the Linux kernel with relatively minimal
modification.  This can be particularly useful when you want to track
upstream Solaris development closely and don’t want the overhead of
maintaining a large patch which converts Solaris primitives to Linux
primitives.

To build packages for your distribution:

    $ ./configure
    $ make pkg

Full documentation for building, configuring, and using the SPL can
be found at: <http://zfsonlinux.org>
