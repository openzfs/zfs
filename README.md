![img](https://openzfs.github.io/openzfs-docs/_static/img/logo/480px-Open-ZFS-Secondary-Logo-Colour-halfsize.png)

OpenZFS is an advanced file system and volume manager which was originally
developed for Solaris and is now maintained by the OpenZFS community.
This repository contains the code for running OpenZFS on Linux and FreeBSD.

[![codecov](https://codecov.io/gh/openzfs/zfs/branch/master/graph/badge.svg)](https://codecov.io/gh/openzfs/zfs)
[![coverity](https://scan.coverity.com/projects/1973/badge.svg)](https://scan.coverity.com/projects/openzfs-zfs)

# Official Resources

  * [Documentation](https://openzfs.github.io/openzfs-docs/) - for using and developing this repo
  * [ZoL site](https://zfsonlinux.org) - Linux release info & links
  * [Mailing lists](https://openzfs.github.io/openzfs-docs/Project%20and%20Community/Mailing%20Lists.html)
  * [OpenZFS site](https://openzfs.org/) - for conference videos and info on other platforms (illumos, OSX, Windows, etc)

# Installation

Full documentation for installing OpenZFS on your favorite operating system can
be found at the [Getting Started Page](https://openzfs.github.io/openzfs-docs/Getting%20Started/index.html).

# Contribute & Develop

We have a separate document with [contribution guidelines](./.github/CONTRIBUTING.md).

We have a [Code of Conduct](./CODE_OF_CONDUCT.md).

# Release

OpenZFS is released under a CDDL license.
For more details see the NOTICE, LICENSE and COPYRIGHT files; `UCRL-CODE-235197`

# Supported Kernels and Distributions

## Linux

Given the wide variety of Linux environments, we prioritize development and testing on stable, supported kernels and distributions.

### Kernel ([kernel.org](https://kernel.org))

All **longterm** kernels from [kernel.org](https://kernel.org) are supported. **stable** kernels are usually supported in the next OpenZFS release.

**Supported longterm kernels**: **6.18**, **6.12**, **6.6**, **6.1**, **5.15**, **5.10**.

### Red Hat Enterprise Linux (RHEL)

All RHEL (and compatible systems: AlmaLinux OS, Rocky Linux, etc) on the **full** or **maintenance** support tracks are supported.

**Supported RHEL releases**: **8.10**, **9.7**, **10.1**.

### Ubuntu

All Ubuntu **LTS** releases are supported.

**Supported Ubuntu releases**: **24.04 “Noble”**, **22.04 “Jammy”**.

### Debian

All Debian **stable** and **LTS** releases are supported.

**Supported Debian releases**: **13 “Trixie”**, **12 “Bookworm”**, **11 “Bullseye”**.

### Other Distributions

Generally, if a distribution is following an LTS kernel, it should work well with OpenZFS.

## FreeBSD

All FreeBSD releases receiving [security support](https://www.freebsd.org/security/#sup) are supported by OpenZFS.

**Supported FreeBSD releases**: **15.0**, **14.4**, **13.5**.
