// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */


#ifndef _ZFS_IOCTL_IMPL_H_
#define	_ZFS_IOCTL_IMPL_H_

extern kmutex_t zfsdev_state_lock;
extern uint64_t zfs_max_nvlist_src_size;

typedef int zfs_ioc_legacy_func_t(zfs_cmd_t *);
typedef int zfs_ioc_func_t(const char *, nvlist_t *, nvlist_t *);
typedef int zfs_secpolicy_func_t(zfs_cmd_t *, nvlist_t *, cred_t *);

typedef enum {
	POOL_CHECK_NONE		= 1 << 0,
	POOL_CHECK_SUSPENDED	= 1 << 1,
	POOL_CHECK_READONLY	= 1 << 2,
} zfs_ioc_poolcheck_t;

typedef enum {
	NO_NAME,
	POOL_NAME,
	DATASET_NAME,
	ENTITY_NAME
} zfs_ioc_namecheck_t;

/*
 * IOC Keys are used to document and validate user->kernel interface inputs.
 * See zfs_keys_recv_new for an example declaration. Any key name that is not
 * listed will be rejected as input.
 *
 * The keyname 'optional' is always allowed, and must be an nvlist if present.
 * Arguments which older kernels can safely ignore can be placed under the
 * "optional" key.
 *
 * When adding new keys to an existing ioc for new functionality, consider:
 *	- adding an entry into zfs_sysfs.c zfs_features[] list
 *	- updating the libzfs_input_check.c test utility
 *
 * Note: in the ZK_WILDCARDLIST case, the name serves as documentation
 * for the expected name (bookmark, snapshot, property, etc) but there
 * is no validation in the preflight zfs_check_input_nvpairs() check.
 */
typedef enum {
	ZK_OPTIONAL = 1 << 0,		/* pair is optional */
	ZK_WILDCARDLIST = 1 << 1,	/* one or more unspecified key names */
} ioc_key_flag_t;

typedef struct zfs_ioc_key {
	const char	*zkey_name;
	data_type_t	zkey_type;
	ioc_key_flag_t	zkey_flags;
} zfs_ioc_key_t;

int zfs_secpolicy_config(zfs_cmd_t *, nvlist_t *, cred_t *);

void zfs_ioctl_register_dataset_nolog(zfs_ioc_t, zfs_ioc_legacy_func_t *,
    zfs_secpolicy_func_t *, zfs_ioc_poolcheck_t);

void zfs_ioctl_register(const char *, zfs_ioc_t, zfs_ioc_func_t *,
    zfs_secpolicy_func_t *, zfs_ioc_namecheck_t, zfs_ioc_poolcheck_t,
    boolean_t, boolean_t, const zfs_ioc_key_t *, size_t);

uint64_t zfs_max_nvlist_src_size_os(void);
void zfs_ioctl_update_mount_cache(const char *dsname);
void zfs_ioctl_init_os(void);

boolean_t zfs_vfs_held(zfsvfs_t *);
int zfs_vfs_ref(zfsvfs_t **);
void zfs_vfs_rele(zfsvfs_t *);

long zfsdev_ioctl_common(uint_t, zfs_cmd_t *, int);
int zfsdev_attach(void);
void zfsdev_detach(void);
void zfsdev_private_set_state(void *, zfsdev_state_t *);
zfsdev_state_t *zfsdev_private_get_state(void *);
int zfsdev_state_init(void *);
void zfsdev_state_destroy(void *);
int zfs_kmod_init(void);
void zfs_kmod_fini(void);

#endif
