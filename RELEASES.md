OpenZFS uses the MAJOR.MINOR.PATCH versioning scheme described here:

  * MAJOR - Incremented at the discretion of the OpenZFS developers to indicate
    a particularly noteworthy feature or change. An increase in MAJOR number
    does not indicate any incompatible on-disk format change. The ability
    to import a ZFS pool is controlled by the feature flags enabled on the
    pool and the feature flags supported by the installed OpenZFS version.
    Increasing the MAJOR version is expected to be an infrequent occurrence.

  * MINOR - Incremented to indicate new functionality such as a new feature
    flag, pool/dataset property, zfs/zpool sub-command, new user/kernel
    interface, etc. MINOR releases may introduce incompatible changes to the
    user space library APIs (libzfs.so). Existing user/kernel interfaces are
    considered to be stable to maximize compatibility between OpenZFS releases.
    Additions to the user/kernel interface are backwards compatible.

  * PATCH - Incremented when applying documentation updates, important bug
    fixes, minor performance improvements, and kernel compatibility patches.
    The user space library APIs and user/kernel interface are considered to
    be stable. PATCH releases for a MAJOR.MINOR are published as needed.

Two release branches are maintained for OpenZFS, they are:

  * OpenZFS LTS - A designated MAJOR.MINOR release with periodic PATCH
    releases that incorporate important changes backported from newer OpenZFS
    releases. This branch is intended for use in environments using an
    LTS, enterprise, or similarly managed kernel (RHEL, Ubuntu LTS, Debian).
    Minor changes to support these distribution kernels will be applied as
    needed. New kernel versions released after the OpenZFS LTS release are
    not supported. LTS releases will receive patches for at least 2 years.
    The current LTS release is OpenZFS 2.1.

  * OpenZFS current - Tracks the newest MAJOR.MINOR release. This branch
    includes support for the latest OpenZFS features and recently releases
    kernels.  When a new MINOR release is tagged the previous MINOR release
    will no longer be maintained (unless it is an LTS release). New MINOR
    releases are planned to occur roughly annually.
