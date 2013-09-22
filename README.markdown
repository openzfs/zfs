The Solaris Porting Layer (SPL) is a Linux kernel module which provides
many of the Solaris kernel APIs.  This shim layer makes it possible to
run Solaris kernel code in the Linux kernel with relatively minimal
modification.  This can be particularly useful when you want to track
upstream Solaris development closely and do not want the overhead of
maintaining a large patch which converts Solaris primitives to Linux
primitives.

To build packages for your distribution:

    $ ./configure
    $ make pkg

If you are building directly from the git tree and not an officially
released tarball you will need to generate the configure script.
This can be done by executing the autogen.sh script after installing
the GNU autotools for your distribution.

To copy the kernel code inside your kernel source tree for builtin
compilation:

    $ ./configure --enable-linux-builtin --with-linux=/usr/src/linux-...
    $ ./copy-builtin /usr/src/linux-...

The SPL comes with an automated test suite called SPLAT.  The test suite
is implemented in two parts.  There is a kernel module which contains
the tests and a user space utility which controls which tests are run.
To run the full test suite:

    $ sudo insmod ./module/splat/splat.ko
    $ sudo ./cmd/splat --all

Full documentation for building, configuring, testing, and using the
SPL can be found at: <http://zfsonlinux.org>
