wdk_add_library(zfskern_os
  KMDF 1.9
abd_os.c
arc_os.c
qat.c
qat_compress.c
qat_crypt.c
spa_misc_os.c
vdev_disk.c
vdev_file.c
zfs_acl.c
zfs_ctldir.c
zfs_debug.c
zfs_dir.c
zfs_file_os.c
zfs_fuid_os.c
zfs_ioctl_os.c
zfs_kstat_windows.c
zfs_vfsops.c
zfs_vnops.c
zfs_vnops_windows.c
zfs_vnops_windows_lib.c
zfs_vnops_windows_mount.c
zfs_windows_zvol.c
zfs_windows_zvol_scsi.c
zfs_windows_zvol_wmi.c
zfs_znode.c
zio_crypt.c
zvol_os.c
)


target_link_libraries(zfskern_os PRIVATE splkern icpkern)
