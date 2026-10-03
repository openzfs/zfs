ZetaWatch
=========

![ZetaWatch displaying pool status and filesystems][ZFSImage]

ZetaWatch is a small OS X program that displays the zfs status in the menu bar, similar to
what iStat Menus does for other information. It is fairly well tested, but due to the
current state of libzfs and libzfs_core, changes will be required until the API
stabilizes. ZetaWatch is usually compiled for the latest available [ZFS release for Mac
OS](https://openzfsonosx.org/), and might not be compatible with other releases.

Currently supported features are:

 * Show pool, vdev, filesystem stats
 * Show pool / filesystem properties
 * Start, stop, pause scrubs, and monitor their progress.
 * Import and Export pools manually, or auto-import when they become available
 * Mount / unmount datasets manually or at pool import automatically
 * Load/Unload encryption keys for encrypted datasets manually or automatically
 * Optionally store pass phrases in the Mac OS X Keychain
 * Create, Display, Delete, Clone Snapshots or roll back to them
 * Report errors in notification center when they are discovered


Installation
------------

ZetaWatch Releases can be downloaded from the [GitHub Releases Page](https://github.com/cbreak-black/ZetaWatch/releases).
Please verify that the Version of ZetaWatch matches your ZFS Version. Usually,
only the newest official Release is supported, since code changes are sometimes
required to be fully compatible with the new library version.

ZetaWatch does not require manual installation. Simply copy it into /Application or where
ever else it fits. The bundled helper tool gets installed automatically the first time the
program is started. This requires user-authentication.

ZetaWatch supports auto updates, if enabled.


For Developers
==============

ZFS Interaction
---------------

ZetaWatch communicates with zfs using `libzfs.dylib`, `libzfs_core.dylib` ,
`libzpool.dylib` and `libnvpair.dylib`, just like the command line tools do. This gives
it all the flexibility of the command line tools, at the cost of having to reimplement
functionality that is found in the tools and not the library. And since the libraries are
explicitly not meant to provide a stable ABI, ZetaWatch is also closely coupled to the
ZFS version it is built and written for.

All the ZFS interaction is wrapped in the ZFSWrapper library. This C++ library isolates
the issues mentioned above and provides a more convenient and safe API than the original
C interface does. The library is used both by the helper tool and the frontend app. This
is the most reusable part of ZetaWatch, and might be split out as separate project later.

 * *ZFSUtils* contains most of the advanced functionality, such as C++ Wrappers around the
library, pool, vdev or file system handles. Those classes also have functionality to query
state and iterate over members.
 * *ZFSNVList* provides a wrapper around the `nvpair_t` / `nvlist_t` data structure that
is used in ZFS for a lot of userland / kernel communication. It manages resources in both
owning and non-owning fashion, and allows for easier iteration over sequences.
 * *ZFSStrings* translate ZFS status enums into the user facing emoji or string
description, optionally with localization. (Localization is not well tested or supported
at the moment.)


Helper Tool
-----------

The implementation of the helper tool follows apple's [EvenBetterAuthorizationSample].

The helper tool communicates with the user application via `AuthorizationService` and
`NSXPCConnection`. The application side of code for this is in `ZetaAuthorization.m`. The
RPC protocol can be found in `ZetaAuthorizationHelperProtocol.h`, and is implemented in
`ZetaAuthorizationHelper.mm`. The `CommonAuthorization.m` file contains the supported
commands and associated default permissions.

The helper tool can be uninstalled with the `uninstall-helper.sh` script. This is useful
for debugging the installation of the helper, or updating the helper without increasing
the bundle version.


Authorization
-------------

The ZetaWatch helper tool uses the Security framework to authorize users before performing
privileged operations. It currently supports the following permissions.

 * `net.the-color-black.ZetaWatch.import`, allowed by default, required for importing a pool.
 * `net.the-color-black.ZetaWatch.export`, allowed by default, required for exporting a pool.
 * `net.the-color-black.ZetaWatch.mount`, allowed by default, required for mounting a dataset.
 * `net.the-color-black.ZetaWatch.unmount`, allowed by default, required for unmounting a
 dataset.
 * `net.the-color-black.ZetaWatch.snapshot`, allowed by default, required for creating a
 snapshot.
 * `net.the-color-black.ZetaWatch.rollback`, requires admin authentication by default,
required for rolling back a filesystem.
 * `net.the-color-black.ZetaWatch.clone`, requires admin authentication by default,
required for cloning a filesystem.
 * `net.the-color-black.ZetaWatch.create`, requires admin authentication by default,
required for creating a new filesystem.
 * `net.the-color-black.ZetaWatch.destroy`, requires admin authentication by default,
required for destroying a filesystem or snapshot.
 * `net.the-color-black.ZetaWatch.key`, allowed by default, required for loading or
unloading a key for a dataset. This also includes the ability to auto mount / unmount them.
 * `net.the-color-black.ZetaWatch.scrub`, allowed by default, required for starting,
stopping or pausing scrubs.

These permissions can be manipulated via the `security` command line program. To inspect
the current dataset creation permissions, and switching it to allow this to all users:

```
security authorizationdb read net.the-color-black.ZetaWatch.create
security authorizationdb write net.the-color-black.ZetaWatch.create allow
```

Permissions include `allow`, `deny` or `authenticate-admin`.

More detailed information about this topic can be found in the article apples documentation
about [AuthorizationServices] and [Managing the Authorization Database in OS X Mavericks].


Security & Code Signing
-----------------------

Official release builds are signed and notarized, and should run without issues even on
newer Mac OS X. But there are still issues with authentication reported with the program
not being recognized as signed. To verify security manually, the following commands can
be used:

```bash
codesign -v -v -d ZetaWatch.app
xcrun stapler validate -v ZetaWatch.app
```

Building ZetaWatch requires an apple developer account with DeveloperID signing
capabilities, since it uses [`SMJobBless`] to run a helper service as root. This service
executes actions on behalf of the user, such as mounting, unmounting or loading a key.
[Notarization] is required to create binaries that can be run without without warning on
the newest Mac OS X.


Self Updating
-------------

The self-updating uses [SparkleFramework]. Since the newest released sparkle does not yet
support hardened runtime, it needs to be compiled manually. Building the "Distribution"
target in the Sparkle submodule in release mode is sufficient.
The sparkle submodule contains a version that is slimmed down by removing most languages,
which saves space. Since ZetaWatch is not localized, this is not a problem.
To create a working fork, adjust the public key and update url in the Info.plist file.


ZFS Binary Compatibility
------------------------

Since ZetaWatch directly links to the zfs libraries, it only works if those are
compatible. And while Sparkle has built-in support for OS compatibility checking, it
doesn't have the same for other dependencies. There is support for custom appcast
filtering, to select a suitable version, but since the ZFS version and the ZetaWatch
version are kind of orthogonal, this didn't seem fitting.

The chosen solution was to have a ZFS version specific appcast URL, and make ZetaWatch
query the appropriate appcast. This allows updating ZetaWatch when the used ZFS version
changes, but also have several supported parallel builds. Currently, the only supported
ZFS version is 1.9.


License
=======

This program is copyrighted by me, because I wrote it.
This program is licensed under the "3-clause BSD" License. See the LICENSE.md file for
details.

[EvenBetterAuthorizationSample]: https://developer.apple.com/library/content/samplecode/EvenBetterAuthorizationSample/Introduction/Intro.html
[`SMJobBless`]: https://developer.apple.com/documentation/servicemanagement/1431078-smjobbless?language=objc
[Notarization]: https://developer.apple.com/documentation/security/notarizing_your_app_before_distribution?language=objc
[ZFSImage]: https://raw.githubusercontent.com/cbreak-black/ZetaWatch/master/doc/ZetaWatch.jpg
[SparkleFramework]: https://sparkle-project.org/
[SparkleGithub]: https://github.com/sparkle-project/Sparkle
[AuthorizationServices]: https://developer.apple.com/documentation/security/authorization_services?language=objc
[Managing the Authorization Database in OS X Mavericks]: https://derflounder.wordpress.com/2014/02/16/managing-the-authorization-database-in-os-x-mavericks/
