# dsl_root_reparent

Reparent the ZFS pool root dataset. Allows `zfs rename tank tank/old`
to move the pool root under a new root dataset, creating a new top-level
dataset in the pool.

## Status

In-memory reparent is fully functional. On-disk persistence is
unresolved — see commit message for details and hypothesis.

## Files

| File | Action |
|------|--------|
| `include/sys/dsl_root_reparent.h` | New — public API |
| `module/zfs/dsl_root_reparent.c` | New — core logic |
| `include/sys/fs/zfs.h` | Modify — add ZFS_IOC_ROOT_REPARENT |
| `module/zfs/zfs_ioctl.c` | Modify — ioctl handler |
| `lib/libzfs/libzfs_dataset.c` | Modify — detect in zfs_rename() |
