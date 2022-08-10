## Description

These scripts are intended to be used with `initramfs-tools`, which is a
similar software product to `dracut` (which is used in Red Hat based
distributions), and is mainly used by Debian GNU/Linux and derivatives.

These scripts share some common functionality with the SysV init scripts,
primarily the `/etc/zfs/zfs-functions` script.

## Configuration

### Root pool/filesystem

Different distributions have their own standard on what to specify on the
kernel command line to boot off a ZFS filesystem.

This script supports the following kernel command line argument combinations
(in this order - first match wins):

* `rpool=<pool>`
* `bootfs=<pool>/<dataset>`
* `rpool=<pool> bootfs=<pool>/<dataset>`
* `-B zfs-bootfs=<pool>/<fs>`
* `root=<pool>/<dataset>`
* `root=ZFS=<pool>/<dataset>`
* `root=zfs:AUTO`
* `root=zfs:<pool>/<dataset>`
* `rpool=rpool`

If a pool is specified, it will be used.  Otherwise, in `AUTO` mode, all pools
will be searched.  Pools may be excluded from the search by listing them in
`ZFS_POOL_EXCEPTIONS` in `/etc/default/zfs`.

Pools will be imported as follows:

* Try `/dev/disk/by-vdev` if it exists; see `/etc/zfs/vdev_id.conf`.
* Try `/dev/disk/by-id` and any other `/dev/disk/by-*` directories.
* Try `/dev`.
* Use the cache file if nothing else worked.

This order may be modified by setting `ZPOOL_IMPORT_PATH` in
`/etc/default/zfs`.

If a dataset is specified, it will be used as the root filesystem.  Otherwise,
this script will attempt to find a root filesystem automatically (in the
specified pool or all pools, as described above).

Filesystems below the root filesystem will be automatically mounted with no
additional configuration necessary.  For example, if the root filesystem is
`rpool/ROOT/rootfs`, `rpool/root/rootfs/var`, `rpool/root/rootfs/usr`, etc.
will be mounted (if they exist).

### Snapshots

The `<dataset>` can be a snapshot.  In this case, the snapshot will be cloned
and the clone used as the root filesystem.  Note:

* If the snapshot does not exist, the base dataset (the part before `@`) is
  used as the boot filesystem instead.
* If the resulting clone dataset already exists, it is destroyed.
* The clone is created with `mountpoint=none` and `canmount=noauto`.  The root
  filesystem is mounted manually by the initramfs script.
* If no snapshot is specified on the `root=` kernel command line, but
  there is an `@`, the user will be prompted to choose a snapshot to use.

### Extra options

The following kernel command line arguments are supported:

* `zfsdebug=(on,yes,1)`: Show extra debugging information
* `zfsforce=(on,yes,1)`: Force import the pool
* `rollback=(on,yes,1)`: Rollback to (instead of clone) the snapshot

### Unlocking a ZFS encrypted root over SSH

To use this feature:

1. Install the `dropbear-initramfs` package.  You may wish to uninstall the
   `cryptsetup-initramfs` package to avoid warnings.
2. Add your SSH key(s) to `/etc/dropbear-initramfs/authorized_keys`.  Note
   that Dropbear does not support ed25519 keys before version 2020.79; 
   in that case, use RSA (2048-bit or more) instead.
3. Rebuild the initramfs with your keys: `update-initramfs -u`
4. During the system boot, login via SSH and run: `zfsunlock`

### Supplying your own unlock mechanism

There is a way to unlock your zfs filesystem using a mechanism these scripts
don't have explicit support for, such as:

* get the key from DNS (presumably from a split-horizon domain that will only resolve when queried from the right client);
* get the key by using a special public key to ssh into a server, where sshd forces execution of a script that outputs the key;
* wait for some other program, such as an interactive dropbear session, to write the key into a file;
* derive the key from the TOC of the audio CD inserted into the optical drive, using some key derivation function ("my server boots to Vivaldi");
* get the key from a program that derives it from voice input to a microphone, or someone performing a specific jig on a dance game controller, or something equally esoteric.

(Note that we endorse neither of these mechanisms and don't mean to imply
that they're secure.)

To supply your own unlock mechanism:

1. Set the `org.openzfs:zfs-initramfs:user-decrypt-function` property to `true` on the fs you'd like your mechanism to unlock.
2. Create `/etc/zfs/zfs-user-functions`, and define the `decrypt_fs_user()` shell function in it. It will be called with `$1` set to the value of the `encryptionroot` attribute and `$2` set to the value of the `keylocation` attribute for the fs to be unlocked.
  * The function is expected to unlock the fs, i.e. call `zfs load-key` in an and supply the key somehow. If the fs is unlocked after the function returns, initramfs prompts for the key will be skipped.
  * If the `keystatus` attribute is still `unavailable` after your function returns, the normal logic takes over; if `keylocation` is `prompt`, a prompt will appear and so on.
  * The return value of your function is, for now, ignored.
3. Rebuild the initramfs.
