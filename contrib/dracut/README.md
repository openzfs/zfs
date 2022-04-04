## Basic setup
1. Install `zfs-dracut`
2. Set `mountpoint=/` for your root dataset (for compatibility, `legacy` also works, but is not recommended for new installations):
    ```sh
    zfs set mountpoint=/ pool/dataset
    ```
3. Either (a) set `bootfs=` on the pool to the dataset:
    ```sh
    zpool set bootfs=pool/dataset pool
    ```
4. Or (b) append `root=zfs:pool/dataset` to your kernel cmdline.
5. Re-generate your initrd and update it in your boot bundle

Encrypted datasets have keys loaded automatically or prompted for.

If the root dataset contains children with `mountpoint=`s of `/etc`, `/bin`, `/lib*`, or `/usr`, they're mounted too.

For complete documentation, see `dracut.zfs(7)`.

## cmdline
1. `root=`                    | Root dataset is…                                         |
   ---------------------------|----------------------------------------------------------|
   *(empty)*                  | the first `bootfs=` after `zpool import -aN`             |
   `zfs:AUTO`, `zfs:`, `zfs`  | *(as above, but overriding other autoselection methods)* |
   `ZFS=pool/dataset`         | `pool/dataset`                                           |
   `zfs:pool/dataset`         | *(as above)*                                             |

   All `+`es are replaced with spaces (i.e. to boot from `root pool/data set`, pass `root=zfs:root+pool/data+set`).

   The dataset can be at any depth, including being the pool's root dataset (i.e. `root=zfs:pool`).

   `rootfstype=zfs` is equivalent to `root=zfs:AUTO`, `rootfstype=zfs root=pool/dataset` is equivalent to `root=zfs:pool/dataset`.

2. `spl_hostid`: passed to `zgenhostid -f`, useful to override the `/etc/hostid` file baked into the initrd.

3. `bootfs.snapshot`, `bootfs.snapshot=snapshot-name`: enables `zfs-snapshot-bootfs.service`,
   which creates a snapshot `$root_dataset@$(uname -r)` (or, in the second form, `$root_dataset@snapshot-name`)
   after pool import but before the rootfs is mounted.
   Failure to create the snapshot is noted, but booting continues.

4. `bootfs.rollback`, `bootfs.rollback=snapshot-name`: enables `zfs-snapshot-bootfs.service`,
   which `-Rf` rolls back to `$root_dataset@$(uname -r)` (or, in the second form, `$root_dataset@snapshot-name`)
   after pool import but before the rootfs is mounted.
   Failure to roll back will fall down to the rescue shell.
   This has obvious potential for data loss: make sure your persistent data is not below the rootfs and you don't care about any intermediate snapshots.

5. If both `bootfs.snapshot` and `bootfs.rollback` are set, `bootfs.rollback` is ordered *after* `bootfs.snapshot`.

6. `zfs_force`, `zfs.force`, `zfsforce`: add `-f` to all `zpool import` invocations.
   May be useful. Use with caution.
