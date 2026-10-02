ZFSWrapper
==========

This is a C++ wrapper library around `libzfs`, `libzfs_core`, `libzpool` and `libnvpair`,
all part of the userland ZFS Libraries. It is primarily intended to be used by
[ZetaWatch](https://github.com/cbreak-black/ZetaWatch).



For Developers
==============

Dependencies
------------

On MacOS, OpenZFS 2.0.1 is tested. The following OpenZFS needs to be installed:
 - [OpenZFS 2.0.1](https://openzfsonosx.org/forum/viewtopic.php?f=20&t=3569&p=11206#p11206)

On Ubuntu 21.04, OpenZFS 2.0.2 is tested. The following apt packages need to be installed:
 - `libzfslinux-dev`
 - `libzfs4linux`


Building
--------

Building works as usual with CMake.
On MacOS, pass `-DZFSW_HAS_ZPOOL_STATUS_COMPATIBILITY_ERR` to CMake.

```
mkdir build
pushd build
cmake ..
make -j12
```
