// SPDX-License-Identifier: BSD-3-Clause
/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (c) 2020, Felix Dörre
 * All rights reserved.
 */

#include <sys/dsl_crypt.h>
#include <sys/byteorder.h>
#include <libzfs.h>

#include <syslog.h>

#include <sys/zio_crypt.h>
#include <openssl/evp.h>

#define	PAM_SM_AUTH
#define	PAM_SM_PASSWORD
#define	PAM_SM_SESSION
#include <security/pam_modules.h>

#if	defined(__linux__)
#include <security/pam_ext.h>
#define	MAP_FLAGS MAP_PRIVATE | MAP_ANONYMOUS
#elif	defined(__FreeBSD__)
#include <security/pam_appl.h>
static void
pam_syslog(pam_handle_t *pamh, int loglevel, const char *fmt, ...)
{
	(void) pamh;
	va_list args;
	va_start(args, fmt);
	vsyslog(loglevel, fmt, args);
	va_end(args);
}
#define	MAP_FLAGS MAP_PRIVATE | MAP_ANON | MAP_NOCORE
#endif

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <pwd.h>
#include <lib/libzfs/libzfs_impl.h>

#include <sys/mman.h>

static const char PASSWORD_VAR_NAME[] = "pam_zfs_key_authtok";
static const char OLD_PASSWORD_VAR_NAME[] = "pam_zfs_key_oldauthtok";

static libzfs_handle_t *g_zfs;

static void destroy_pw(pam_handle_t *pamh, void *data, int errcode);

typedef int (*mlock_func_t) (const void *, size_t);

typedef struct {
	size_t len;
	char *value;
} pw_password_t;

/*
 * Try to mlock(2) or munlock(2) addr while handling EAGAIN by retrying ten
 * times and sleeping 10 milliseconds in between for a total of 0.1
 * seconds. lock_func must point to either mlock(2) or munlock(2).
 */
static int
try_lock(mlock_func_t lock_func, const void *addr, size_t len)
{
	int err;
	int retries = 10;
	useconds_t sleep_dur = 10 * 1000;

	if ((err = (*lock_func)(addr, len)) != EAGAIN) {
		return (err);
	}
	for (int i = retries; i > 0; --i) {
		(void) usleep(sleep_dur);
		if ((err = (*lock_func)(addr, len)) != EAGAIN) {
			break;
		}
	}
	return (err);
}


static pw_password_t *
alloc_pw_size(size_t len)
{
	pw_password_t *pw = malloc(sizeof (pw_password_t));
	if (!pw) {
		return (NULL);
	}
	pw->len = len;
	/*
	 * We use mmap(2) rather than malloc(3) since later on we mlock(2) the
	 * memory region. Since mlock(2) and munlock(2) operate on whole memory
	 * pages we should allocate a whole page here as mmap(2) does. Further
	 * this ensures that the addresses passed to mlock(2) an munlock(2) are
	 * on a page boundary as suggested by FreeBSD and required by some
	 * other implementations. Finally we avoid inadvertently munlocking
	 * memory mlocked by an concurrently running instance of us.
	 */
	pw->value = mmap(NULL, pw->len, PROT_READ | PROT_WRITE, MAP_FLAGS,
	    -1, 0);

	if (pw->value == MAP_FAILED) {
		free(pw);
		return (NULL);
	}
	if (try_lock(mlock, pw->value, pw->len) != 0) {
		(void) munmap(pw->value, pw->len);
		free(pw);
		return (NULL);
	}
	return (pw);
}

static pw_password_t *
alloc_pw_string(const char *source)
{
	size_t len = strlen(source) + 1;
	pw_password_t *pw = alloc_pw_size(len);

	if (!pw) {
		return (NULL);
	}
	memcpy(pw->value, source, pw->len);
	return (pw);
}

static void
pw_free(pw_password_t *pw)
{
	memset(pw->value, 0, pw->len);
	if (try_lock(munlock, pw->value, pw->len) == 0) {
		(void) munmap(pw->value, pw->len);
	}
	free(pw);
}

static pw_password_t *
pw_fetch(pam_handle_t *pamh, int tok)
{
	const char *token;
	if (pam_get_authtok(pamh, tok, &token, NULL) != PAM_SUCCESS) {
		pam_syslog(pamh, LOG_ERR,
		    "couldn't get password from PAM stack");
		return (NULL);
	}
	if (!token) {
		pam_syslog(pamh, LOG_ERR,
		    "token from PAM stack is null");
		return (NULL);
	}
	return (alloc_pw_string(token));
}

static const pw_password_t *
pw_fetch_lazy(pam_handle_t *pamh, int tok, const char *var_name)
{
	pw_password_t *pw = pw_fetch(pamh, tok);
	if (pw == NULL) {
		return (NULL);
	}
	int ret = pam_set_data(pamh, var_name, pw, destroy_pw);
	if (ret != PAM_SUCCESS) {
		pw_free(pw);
		pam_syslog(pamh, LOG_ERR, "pam_set_data failed");
		return (NULL);
	}
	return (pw);
}

static const pw_password_t *
pw_get(pam_handle_t *pamh, int tok, const char *var_name)
{
	const pw_password_t *authtok = NULL;
	int ret = pam_get_data(pamh, var_name,
	    (const void**)(&authtok));
	if (ret == PAM_SUCCESS)
		return (authtok);
	if (ret == PAM_NO_MODULE_DATA)
		return (pw_fetch_lazy(pamh, tok, var_name));
	pam_syslog(pamh, LOG_ERR, "password not available");
	return (NULL);
}

static int
pw_clear(pam_handle_t *pamh, const char *var_name)
{
	int ret = pam_set_data(pamh, var_name, NULL, NULL);
	if (ret != PAM_SUCCESS) {
		pam_syslog(pamh, LOG_ERR, "clearing password failed");
		return (-1);
	}
	return (0);
}

static void
destroy_pw(pam_handle_t *pamh, void *data, int errcode)
{
	(void) pamh, (void) errcode;

	if (data != NULL) {
		pw_free((pw_password_t *)data);
	}
}

static int
pam_zfs_init(pam_handle_t *pamh)
{
	int error = 0;
	if ((g_zfs = libzfs_init()) == NULL) {
		error = errno;
		pam_syslog(pamh, LOG_ERR, "Zfs initialization error: %s",
		    libzfs_error_init(error));
	}
	return (error);
}

static void
pam_zfs_free(void)
{
	libzfs_fini(g_zfs);
}

static pw_password_t *
prepare_passphrase(pam_handle_t *pamh, zfs_handle_t *ds,
    const char *passphrase, nvlist_t *nvlist)
{
	pw_password_t *key = alloc_pw_size(WRAPPING_KEY_LEN);
	if (!key) {
		return (NULL);
	}
	uint64_t salt;
	uint64_t iters;
	if (nvlist != NULL) {
		int fd = open("/dev/urandom", O_RDONLY);
		if (fd < 0) {
			pw_free(key);
			return (NULL);
		}
		int bytes_read = 0;
		char *buf = (char *)&salt;
		size_t bytes = sizeof (uint64_t);
		while (bytes_read < bytes) {
			ssize_t len = read(fd, buf + bytes_read, bytes
			    - bytes_read);
			if (len < 0) {
				close(fd);
				pw_free(key);
				return (NULL);
			}
			bytes_read += len;
		}
		close(fd);

		if (nvlist_add_uint64(nvlist,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), salt)) {
			pam_syslog(pamh, LOG_ERR,
			    "failed to add salt to nvlist");
			pw_free(key);
			return (NULL);
		}
		iters = DEFAULT_PBKDF2_ITERATIONS;
		if (nvlist_add_uint64(nvlist, zfs_prop_to_name(
		    ZFS_PROP_PBKDF2_ITERS), iters)) {
			pam_syslog(pamh, LOG_ERR,
			    "failed to add iters to nvlist");
			pw_free(key);
			return (NULL);
		}
	} else {
		salt = zfs_prop_get_int(ds, ZFS_PROP_PBKDF2_SALT);
		iters = zfs_prop_get_int(ds, ZFS_PROP_PBKDF2_ITERS);
	}

	salt = LE_64(salt);
	if (!PKCS5_PBKDF2_HMAC_SHA1((char *)passphrase,
	    strlen(passphrase), (uint8_t *)&salt,
	    sizeof (uint64_t), iters, WRAPPING_KEY_LEN,
	    (uint8_t *)key->value)) {
		pam_syslog(pamh, LOG_ERR, "pbkdf failed");
		pw_free(key);
		return (NULL);
	}
	return (key);
}

static int
is_key_loaded(pam_handle_t *pamh, const char *ds_name)
{
	zfs_handle_t *ds = zfs_open(g_zfs, ds_name, ZFS_TYPE_FILESYSTEM);
	if (ds == NULL) {
		pam_syslog(pamh, LOG_ERR, "dataset %s not found", ds_name);
		return (-1);
	}
	int keystatus = zfs_prop_get_int(ds, ZFS_PROP_KEYSTATUS);
	zfs_close(ds);
	return (keystatus != ZFS_KEYSTATUS_UNAVAILABLE);
}

static int
change_key(pam_handle_t *pamh, const char *ds_name,
    const char *passphrase)
{
	zfs_handle_t *ds = zfs_open(g_zfs, ds_name, ZFS_TYPE_FILESYSTEM);
	if (ds == NULL) {
		pam_syslog(pamh, LOG_ERR, "dataset %s not found", ds_name);
		return (-1);
	}
	nvlist_t *nvlist = fnvlist_alloc();
	pw_password_t *key = prepare_passphrase(pamh, ds, passphrase, nvlist);
	if (key == NULL) {
		nvlist_free(nvlist);
		zfs_close(ds);
		return (-1);
	}
	if (nvlist_add_string(nvlist,
	    zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
	    "prompt")) {
		pam_syslog(pamh, LOG_ERR, "nvlist_add failed for keylocation");
		pw_free(key);
		nvlist_free(nvlist);
		zfs_close(ds);
		return (-1);
	}
	if (nvlist_add_uint64(nvlist,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT),
	    ZFS_KEYFORMAT_PASSPHRASE)) {
		pam_syslog(pamh, LOG_ERR, "nvlist_add failed for keyformat");
		pw_free(key);
		nvlist_free(nvlist);
		zfs_close(ds);
		return (-1);
	}
	int ret = lzc_change_key(ds_name, DCP_CMD_NEW_KEY, nvlist,
	    (uint8_t *)key->value, WRAPPING_KEY_LEN);
	pw_free(key);
	if (ret) {
		pam_syslog(pamh, LOG_ERR, "change_key failed: %d", ret);
		nvlist_free(nvlist);
		zfs_close(ds);
		return (-1);
	}
	nvlist_free(nvlist);
	zfs_close(ds);
	return (0);
}

typedef struct {
	char *homes_prefix;
	char *runstatedir;
	char *homedir;
	char *dsname;
	uid_t uid_min;
	uid_t uid_max;
	uid_t uid;
	const char *username;
	boolean_t unmount_and_unload;
	boolean_t force_unmount;
	boolean_t recursive_homes;
	boolean_t mount_recursively;
} zfs_key_config_t;

static int
zfs_key_config_load(pam_handle_t *pamh, zfs_key_config_t *config,
    int argc, const char **argv)
{
	config->homes_prefix = strdup("rpool/home");
	if (config->homes_prefix == NULL) {
		pam_syslog(pamh, LOG_ERR, "strdup failure");
		return (PAM_SERVICE_ERR);
	}
	config->runstatedir = strdup(RUNSTATEDIR "/pam_zfs_key");
	if (config->runstatedir == NULL) {
		pam_syslog(pamh, LOG_ERR, "strdup failure");
		free(config->homes_prefix);
		return (PAM_SERVICE_ERR);
	}
	const char *name;
	if (pam_get_user(pamh, &name, NULL) != PAM_SUCCESS) {
		pam_syslog(pamh, LOG_ERR,
		    "couldn't get username from PAM stack");
		free(config->runstatedir);
		free(config->homes_prefix);
		return (PAM_SERVICE_ERR);
	}
	struct passwd *entry = getpwnam(name);
	if (!entry) {
		free(config->runstatedir);
		free(config->homes_prefix);
		return (PAM_USER_UNKNOWN);
	}
	config->uid_min = 1000;
	config->uid_max = MAXUID;
	config->uid = entry->pw_uid;
	config->username = name;
	config->unmount_and_unload = B_TRUE;
	config->force_unmount = B_FALSE;
	config->recursive_homes = B_FALSE;
	config->mount_recursively = B_FALSE;
	config->dsname = NULL;
	config->homedir = NULL;
	for (int c = 0; c < argc; c++) {
		if (strncmp(argv[c], "homes=", 6) == 0) {
			free(config->homes_prefix);
			config->homes_prefix = strdup(argv[c] + 6);
		} else if (strncmp(argv[c], "runstatedir=", 12) == 0) {
			free(config->runstatedir);
			config->runstatedir = strdup(argv[c] + 12);
		} else if (strncmp(argv[c], "uid_min=", 8) == 0) {
			sscanf(argv[c] + 8, "%u", &config->uid_min);
		} else if (strncmp(argv[c], "uid_max=", 8) == 0) {
			sscanf(argv[c] + 8, "%u", &config->uid_max);
		} else if (strcmp(argv[c], "nounmount") == 0) {
			config->unmount_and_unload = B_FALSE;
		} else if (strcmp(argv[c], "forceunmount") == 0) {
			config->force_unmount = B_TRUE;
		} else if (strcmp(argv[c], "recursive_homes") == 0) {
			config->recursive_homes = B_TRUE;
		} else if (strcmp(argv[c], "mount_recursively") == 0) {
			config->mount_recursively = B_TRUE;
		} else if (strcmp(argv[c], "prop_mountpoint") == 0) {
			if (config->homedir == NULL)
				config->homedir = strdup(entry->pw_dir);
		}
	}
	return (PAM_SUCCESS);
}

typedef struct {
	pam_handle_t *pamh;
	zfs_key_config_t *target;
} mount_umount_dataset_data_t;

static int
mount_dataset(zfs_handle_t *zhp, void *data)
{
	mount_umount_dataset_data_t *mount_umount_dataset_data = data;

	zfs_key_config_t *target = mount_umount_dataset_data->target;
	pam_handle_t *pamh = mount_umount_dataset_data->pamh;

	/* Refresh properties to get the latest key status */
	zfs_refresh_properties(zhp);

	int ret = 0;

	/* Check if dataset type is filesystem */
	if (zhp->zfs_type != ZFS_TYPE_FILESYSTEM) {
		pam_syslog(pamh, LOG_DEBUG,
		    "dataset is not filesystem: %s, skipping.",
		    zfs_get_name(zhp));
		return (0);
	}

	/* Check if encryption key is available */
	if (zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_KEYSTATUS_UNAVAILABLE) {
		pam_syslog(pamh, LOG_WARNING,
		    "key unavailable for: %s, skipping",
		    zfs_get_name(zhp));
		return (0);
	}

	/* Check if prop canmount is on */
	if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) != ZFS_CANMOUNT_ON) {
		pam_syslog(pamh, LOG_INFO,
		    "canmount is not on for: %s, skipping",
		    zfs_get_name(zhp));
		return (0);
	}

	/* Get mountpoint prop for check */
	char mountpoint[ZFS_MAXPROPLEN];
	if ((ret = zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, 1)) != 0) {
		pam_syslog(pamh, LOG_ERR,
		    "failed to get mountpoint prop: %d", ret);
		return (-1);
	}

	/* Check if mountpoint isn't none or legacy */
	if (strcmp(mountpoint, ZFS_MOUNTPOINT_NONE) == 0 ||
	    strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0) {
		pam_syslog(pamh, LOG_INFO,
		    "mountpoint is none or legacy for: %s, skipping",
		    zfs_get_name(zhp));
		return (0);
	}

	/* Don't mount the dataset if already mounted */
	if (zfs_is_mounted(zhp, NULL)) {
		pam_syslog(pamh, LOG_INFO, "already mounted: %s",
		    zfs_get_name(zhp));
		return (0);
	}

	/* Mount the dataset */
	ret = zfs_mount(zhp, NULL, 0);
	if (ret) {
		pam_syslog(pamh, LOG_ERR,
		    "zfs_mount failed for %s with: %d", zfs_get_name(zhp),
		    ret);
		return (ret);
	}

	/* Recursively mount children if the recursive flag is set */
	if (target->mount_recursively) {
		ret = zfs_iter_filesystems_v2(zhp, 0, mount_dataset, data);
		if (ret != 0) {
			pam_syslog(pamh, LOG_ERR,
			    "child iteration failed: %d", ret);
			return (-1);
		}
	}

	return (ret);
}

static int
umount_dataset(zfs_handle_t *zhp, void *data)
{
	mount_umount_dataset_data_t *mount_umount_dataset_data = data;

	zfs_key_config_t *target = mount_umount_dataset_data->target;
	pam_handle_t *pamh = mount_umount_dataset_data->pamh;

	int ret = 0;
	/* Recursively umount children if the recursive flag is set */
	if (target->mount_recursively) {
		ret = zfs_iter_filesystems_v2(zhp, 0, umount_dataset, data);
		if (ret != 0) {
			pam_syslog(pamh, LOG_ERR,
			    "child iteration failed: %d", ret);
			return (-1);
		}
	}

	/* Check if dataset type is filesystem */
	if (zhp->zfs_type != ZFS_TYPE_FILESYSTEM) {
		pam_syslog(pamh, LOG_DEBUG,
		    "dataset is not filesystem: %s, skipping",
		    zfs_get_name(zhp));
		return (0);
	}

	/* Don't umount the dataset if already unmounted */
	if (zfs_is_mounted(zhp, NULL) == 0) {
		pam_syslog(pamh, LOG_INFO, "already unmounted: %s",
		    zfs_get_name(zhp));
		return (0);
	}

	/* Unmount the dataset */
	ret = zfs_unmount(zhp, NULL, target->force_unmount ? MS_FORCE : 0);
	if (ret) {
		pam_syslog(pamh, LOG_ERR,
		    "zfs_unmount failed for %s with: %d", zfs_get_name(zhp),
		    ret);
		return (ret);
	}

	return (ret);
}

static int
decrypt_mount(pam_handle_t *pamh, zfs_key_config_t *config, const char *ds_name,
	const char *passphrase, boolean_t noop)
{
	zfs_handle_t *ds = zfs_open(g_zfs, ds_name, ZFS_TYPE_FILESYSTEM);
	if (ds == NULL) {
		pam_syslog(pamh, LOG_ERR, "dataset %s not found", ds_name);
		return (-1);
	}
	pw_password_t *key = prepare_passphrase(pamh, ds, passphrase, NULL);
	if (key == NULL) {
		zfs_close(ds);
		return (-1);
	}
	int ret = lzc_load_key(ds_name, noop, (uint8_t *)key->value,
	    WRAPPING_KEY_LEN);
	pw_free(key);
	if (ret && ret != EEXIST) {
		pam_syslog(pamh, LOG_ERR, "load_key failed: %d", ret);
		zfs_close(ds);
		return (-1);
	}

	if (noop) {
		zfs_close(ds);
		return (0);
	}

	mount_umount_dataset_data_t data;
	data.pamh = pamh;
	data.target = config;

	ret = mount_dataset(ds, &data);
	if (ret != 0) {
		pam_syslog(pamh, LOG_ERR, "mount failed: %d", ret);
		zfs_close(ds);
		return (-1);
	}

	zfs_close(ds);
	return (0);
}

static int
unmount_unload(pam_handle_t *pamh, const char *ds_name,
    zfs_key_config_t *target)
{
	zfs_handle_t *ds = zfs_open(g_zfs, ds_name, ZFS_TYPE_FILESYSTEM);
	if (ds == NULL) {
		pam_syslog(pamh, LOG_ERR, "dataset %s not found", ds_name);
		return (-1);
	}

	mount_umount_dataset_data_t data;
	data.pamh = pamh;
	data.target = target;

	int ret = umount_dataset(ds, &data);
	if (ret) {
		pam_syslog(pamh, LOG_ERR,
		    "unmount_dataset failed with: %d", ret);
		zfs_close(ds);
		return (-1);
	}

	ret = lzc_unload_key(ds_name);
	if (ret) {
		pam_syslog(pamh, LOG_ERR, "unload_key failed with: %d", ret);
		zfs_close(ds);
		return (-1);
	}
	zfs_close(ds);
	return (0);
}

static void
zfs_key_config_free(zfs_key_config_t *config)
{
	free(config->homes_prefix);
	free(config->runstatedir);
	free(config->homedir);
	free(config->dsname);
}

static int
find_dsname_by_prop_value(zfs_handle_t *zhp, void *data)
{
	zfs_type_t type = zfs_get_type(zhp);
	zfs_key_config_t *target = data;
	char mountpoint[ZFS_MAXPROPLEN];

	/* Skip any datasets whose type does not match */
	if ((type & ZFS_TYPE_FILESYSTEM) == 0) {
		zfs_close(zhp);
		return (0);
	}

	/* Skip any datasets whose mountpoint does not match */
	(void) zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE);
	if (strcmp(target->homedir, mountpoint) != 0) {
		if (target->recursive_homes) {
			(void) zfs_iter_filesystems_v2(zhp, 0,
			    find_dsname_by_prop_value, target);
		}
		zfs_close(zhp);
		return (target->dsname != NULL);
	}

	target->dsname = strdup(zfs_get_name(zhp));
	zfs_close(zhp);
	return (1);
}

static char *
zfs_key_config_get_dataset(pam_handle_t *pamh, zfs_key_config_t *config)
{
	if (config->homedir != NULL &&
	    config->homes_prefix != NULL) {
		if (strcmp(config->homes_prefix, "*") == 0) {
			(void) zfs_iter_root(g_zfs,
			    find_dsname_by_prop_value, config);
		} else {
			zfs_handle_t *zhp = zfs_open(g_zfs,
			    config->homes_prefix, ZFS_TYPE_FILESYSTEM);
			if (zhp == NULL) {
				pam_syslog(pamh, LOG_ERR,
				    "dataset %s not found",
				    config->homes_prefix);
				return (NULL);
			}

			(void) zfs_iter_filesystems_v2(zhp, 0,
			    find_dsname_by_prop_value, config);
			zfs_close(zhp);
		}
		char *dsname = config->dsname;
		config->dsname = NULL;
		return (dsname);
	}

	if (config->homes_prefix == NULL) {
		return (NULL);
	}

	size_t len = ZFS_MAX_DATASET_NAME_LEN;
	size_t total_len = strlen(config->homes_prefix) + 1
	    + strlen(config->username);
	if (total_len > len) {
		return (NULL);
	}
	char *ret = malloc(len + 1);
	if (!ret) {
		return (NULL);
	}
	ret[0] = 0;
	(void) snprintf(ret, len + 1, "%s/%s", config->homes_prefix,
	    config->username);
	return (ret);
}

static int
zfs_key_config_modify_session_counter(pam_handle_t *pamh,
    zfs_key_config_t *config, int delta)
{
	const char *runtime_path = config->runstatedir;
	if (mkdir(runtime_path, S_IRWXU) != 0 && errno != EEXIST) {
		pam_syslog(pamh, LOG_ERR, "Can't create runtime path: %d",
		    errno);
		return (-1);
	}
	if (chown(runtime_path, 0, 0) != 0) {
		pam_syslog(pamh, LOG_ERR, "Can't chown runtime path: %d",
		    errno);
		return (-1);
	}
	if (chmod(runtime_path, S_IRWXU) != 0) {
		pam_syslog(pamh, LOG_ERR, "Can't chmod runtime path: %d",
		    errno);
		return (-1);
	}

	char *counter_path;
	if (asprintf(&counter_path, "%s/%u", runtime_path, config->uid) == -1)
		return (-1);

	const int fd = open(counter_path,
	    O_RDWR | O_CLOEXEC | O_CREAT | O_NOFOLLOW,
	    S_IRUSR | S_IWUSR);
	free(counter_path);
	if (fd < 0) {
		pam_syslog(pamh, LOG_ERR, "Can't open counter file: %d", errno);
		return (-1);
	}
	if (flock(fd, LOCK_EX) != 0) {
		pam_syslog(pamh, LOG_ERR, "Can't lock counter file: %d", errno);
		close(fd);
		return (-1);
	}
	char counter[20];
	char *pos = counter;
	int remaining = sizeof (counter) - 1;
	int ret;
	counter[sizeof (counter) - 1] = 0;
	while (remaining > 0 && (ret = read(fd, pos, remaining)) > 0) {
		remaining -= ret;
		pos += ret;
	}
	*pos = 0;
	long int counter_value = strtol(counter, NULL, 10);
	counter_value += delta;
	if (counter_value < 0) {
		counter_value = 0;
	}
	lseek(fd, 0, SEEK_SET);
	if (ftruncate(fd, 0) != 0) {
		pam_syslog(pamh, LOG_ERR, "Can't truncate counter file: %d",
		    errno);
		close(fd);
		return (-1);
	}
	snprintf(counter, sizeof (counter), "%ld", counter_value);
	remaining = strlen(counter);
	pos = counter;
	while (remaining > 0 && (ret = write(fd, pos, remaining)) > 0) {
		remaining -= ret;
		pos += ret;
	}
	close(fd);
	return (counter_value);
}

__attribute__((visibility("default")))
PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	(void) flags;

	if (geteuid() != 0) {
		pam_syslog(pamh, LOG_ERR,
		    "Cannot zfs_mount when not being root.");
		return (PAM_SERVICE_ERR);
	}
	zfs_key_config_t config;
	int config_err = zfs_key_config_load(pamh, &config, argc, argv);
	if (config_err != PAM_SUCCESS) {
		return (config_err);
	}
	if (config.uid < config.uid_min || config.uid > config.uid_max) {
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}

	const pw_password_t *token = pw_fetch_lazy(pamh,
	    PAM_AUTHTOK, PASSWORD_VAR_NAME);
	if (token == NULL) {
		zfs_key_config_free(&config);
		return (PAM_AUTH_ERR);
	}
	if (pam_zfs_init(pamh) != 0) {
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	char *dataset = zfs_key_config_get_dataset(pamh, &config);
	if (!dataset) {
		pam_zfs_free();
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	if (decrypt_mount(pamh, &config, dataset, token->value, B_TRUE) == -1) {
		free(dataset);
		pam_zfs_free();
		zfs_key_config_free(&config);
		return (PAM_AUTH_ERR);
	}
	free(dataset);
	pam_zfs_free();
	zfs_key_config_free(&config);
	return (PAM_SUCCESS);
}

__attribute__((visibility("default")))
PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	(void) pamh, (void) flags, (void) argc, (void) argv;
	return (PAM_SUCCESS);
}

__attribute__((visibility("default")))
PAM_EXTERN int
pam_sm_chauthtok(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	if (geteuid() != 0) {
		pam_syslog(pamh, LOG_ERR,
		    "Cannot zfs_mount when not being root.");
		return (PAM_PERM_DENIED);
	}
	zfs_key_config_t config;
	if (zfs_key_config_load(pamh, &config, argc, argv) != PAM_SUCCESS) {
		return (PAM_SERVICE_ERR);
	}
	if (config.uid < config.uid_min || config.uid > config.uid_max) {
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	const pw_password_t *old_token = pw_get(pamh,
	    PAM_OLDAUTHTOK, OLD_PASSWORD_VAR_NAME);
	{
		if (pam_zfs_init(pamh) != 0) {
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		char *dataset = zfs_key_config_get_dataset(pamh, &config);
		if (!dataset) {
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		if (!old_token) {
			pam_syslog(pamh, LOG_ERR,
			    "old password from PAM stack is null");
			free(dataset);
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		if (decrypt_mount(pamh, &config, dataset,
		    old_token->value, B_TRUE) == -1) {
			pam_syslog(pamh, LOG_ERR,
			    "old token mismatch");
			free(dataset);
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_PERM_DENIED);
		}
	}

	if ((flags & PAM_UPDATE_AUTHTOK) != 0) {
		const pw_password_t *token = pw_get(pamh, PAM_AUTHTOK,
		    PASSWORD_VAR_NAME);
		if (token == NULL) {
			pam_syslog(pamh, LOG_ERR, "new password unavailable");
			pam_zfs_free();
			zfs_key_config_free(&config);
			pw_clear(pamh, OLD_PASSWORD_VAR_NAME);
			return (PAM_SERVICE_ERR);
		}
		char *dataset = zfs_key_config_get_dataset(pamh, &config);
		if (!dataset) {
			pam_zfs_free();
			zfs_key_config_free(&config);
			pw_clear(pamh, OLD_PASSWORD_VAR_NAME);
			pw_clear(pamh, PASSWORD_VAR_NAME);
			return (PAM_SERVICE_ERR);
		}
		int was_loaded = is_key_loaded(pamh, dataset);
		if (!was_loaded && decrypt_mount(pamh, &config, dataset,
		    old_token->value, B_FALSE) == -1) {
			free(dataset);
			pam_zfs_free();
			zfs_key_config_free(&config);
			pw_clear(pamh, OLD_PASSWORD_VAR_NAME);
			pw_clear(pamh, PASSWORD_VAR_NAME);
			return (PAM_SERVICE_ERR);
		}
		int changed = change_key(pamh, dataset, token->value);
		if (!was_loaded) {
			unmount_unload(pamh, dataset, &config);
		}
		free(dataset);
		pam_zfs_free();
		zfs_key_config_free(&config);
		if (pw_clear(pamh, OLD_PASSWORD_VAR_NAME) == -1 ||
		    pw_clear(pamh, PASSWORD_VAR_NAME) == -1 || changed == -1) {
			return (PAM_SERVICE_ERR);
		}
	} else {
		zfs_key_config_free(&config);
	}
	return (PAM_SUCCESS);
}

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	(void) flags;

	if (geteuid() != 0) {
		pam_syslog(pamh, LOG_ERR,
		    "Cannot zfs_mount when not being root.");
		return (PAM_SUCCESS);
	}
	zfs_key_config_t config;
	if (zfs_key_config_load(pamh, &config, argc, argv) != PAM_SUCCESS) {
		return (PAM_SESSION_ERR);
	}

	if (config.uid < config.uid_min || config.uid > config.uid_max) {
		zfs_key_config_free(&config);
		return (PAM_SUCCESS);
	}

	int counter = zfs_key_config_modify_session_counter(pamh, &config, 1);
	if (counter != 1) {
		zfs_key_config_free(&config);
		return (PAM_SUCCESS);
	}

	const pw_password_t *token = pw_get(pamh,
	    PAM_AUTHTOK, PASSWORD_VAR_NAME);
	if (token == NULL) {
		zfs_key_config_free(&config);
		return (PAM_SESSION_ERR);
	}
	if (pam_zfs_init(pamh) != 0) {
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	char *dataset = zfs_key_config_get_dataset(pamh, &config);
	if (!dataset) {
		pam_zfs_free();
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	if (decrypt_mount(pamh, &config, dataset,
	    token->value, B_FALSE) == -1) {
		free(dataset);
		pam_zfs_free();
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	free(dataset);
	pam_zfs_free();
	zfs_key_config_free(&config);
	if (pw_clear(pamh, PASSWORD_VAR_NAME) == -1) {
		return (PAM_SERVICE_ERR);
	}
	return (PAM_SUCCESS);

}

__attribute__((visibility("default")))
PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	(void) flags;

	if (geteuid() != 0) {
		pam_syslog(pamh, LOG_ERR,
		    "Cannot zfs_mount when not being root.");
		return (PAM_SUCCESS);
	}
	zfs_key_config_t config;
	if (zfs_key_config_load(pamh, &config, argc, argv) != PAM_SUCCESS) {
		return (PAM_SESSION_ERR);
	}
	if (config.uid < config.uid_min || config.uid > config.uid_max) {
		zfs_key_config_free(&config);
		return (PAM_SUCCESS);
	}

	int counter = zfs_key_config_modify_session_counter(pamh, &config, -1);
	if (counter != 0) {
		zfs_key_config_free(&config);
		return (PAM_SUCCESS);
	}

	if (config.unmount_and_unload) {
		if (pam_zfs_init(pamh) != 0) {
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		char *dataset = zfs_key_config_get_dataset(pamh, &config);
		if (!dataset) {
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SESSION_ERR);
		}
		if (unmount_unload(pamh, dataset, &config) == -1) {
			free(dataset);
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SESSION_ERR);
		}
		free(dataset);
		pam_zfs_free();
	}

	zfs_key_config_free(&config);
	return (PAM_SUCCESS);
}
