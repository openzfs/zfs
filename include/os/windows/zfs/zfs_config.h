/* zfs_config.h.  static file on Windows.  */

/* Define to 1 to enabled dmu tx validation */
/* #undef DEBUG_DMU_TX */

#define SYSCONFDIR "/etc/zfs/scripts/"  // Windwosify me
#define	TEXT_DOMAIN "zfs-windows-user"

/* Path where the Filesystems bundle is installed. */
#define FILESYSTEMS_PREFIX "/Library/Filesystems"

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if you have libblkid */
/* #undef HAVE_LIBBLKID */

/* Define if you have libuuid */
#define HAVE_LIBUUID 1

#define HAVE_XDR_BYTESREC 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `mlockall' function. */
#define HAVE_MLOCKALL 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

#define	HAVE_SETMNTENT

/* Define if you have zlib */
#define HAVE_ZLIB 1

#define	HAVE_LOFF_T 1

#define	HAVE_USLEEP 1

/* Path where the kernel module is installed. */
#define KERNEL_MODPREFIX "/Library/Extensions"

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Define to a directory where mount(2) will look for mount_zfs. */
#define MOUNTEXECDIR "${exec_prefix}/sbin"

/* Define ZFS_BOOT to enable kext load at boot */
#define ZFS_BOOT 1

/* zfs debugging enabled */

/* Define the project author. */
#define ZFS_META_AUTHOR "OpenZFS on Windows"

/* Define the project release date. */
/* #undef ZFS_META_DATA */

/* Define the project license. */
#define ZFS_META_LICENSE "CDDL"

/* Define the libtool library 'age' version information. */
/* #undef ZFS_META_LT_AGE */

/* Define the libtool library 'current' version information. */
/* #undef ZFS_META_LT_CURRENT */

/* Define the libtool library 'revision' version information. */
/* #undef ZFS_META_LT_REVISION */

/* Define the project name. */
#define ZFS_META_NAME "zfs"

/* Define the project release. */
#define ZFS_META_RELEASE "2"

/* Define the project version. */
#define ZFS_META_VERSION "2.0.0"

/* Define the project alias string. */
#define ZFS_META_ALIAS "zfs-" ZFS_META_VERSION "-" ZFS_META_RELEASE
