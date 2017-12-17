fstab generator
---------------

For compatibility with other posix utilities, it can be useful for
/etc/fstab to reflect the filesystems present on a system.

fstab-generator generates an fstab section, suitable for inlining into
/etc/fstab. For instance:

```fstab-generator tank ```

will place a section inside of /etc/fstab (with label "tank"). Each
filesystem under tank is enumerated, and appropriate mount options
and mountpoints are included. If systemd is being used,

```fstab-generator --options "x-systemd.requires=zfs-import.target" tank ```

will ensure that all pools are imported before systemd attempts to
mount that filesystem. These commands are idempotent (multiple calls
are the same as a single call). Moreover, subsequent calls will update
the section with any new filesystems or mount parameters.
