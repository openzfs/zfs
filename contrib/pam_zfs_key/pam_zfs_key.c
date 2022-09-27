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

#include <sys/mman.h>

static const char PASSWORD_VAR_NAME[] = "pam_zfs_key_authtok";

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
pw_fetch(pam_handle_t *pamh)
{
	const char *token;
	if (pam_get_authtok(pamh, PAM_AUTHTOK, &token, NULL) != PAM_SUCCESS) {
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
pw_fetch_lazy(pam_handle_t *pamh)
{
	pw_password_t *pw = pw_fetch(pamh);
	if (pw == NULL) {
		return (NULL);
	}
	int ret = pam_set_data(pamh, PASSWORD_VAR_NAME, pw, destroy_pw);
	if (ret != PAM_SUCCESS) {
		pw_free(pw);
		pam_syslog(pamh, LOG_ERR, "pam_set_data failed");
		return (NULL);
	}
	return (pw);
}

static const pw_password_t *
pw_get(pam_handle_t *pamh)
{
	const pw_password_t *authtok = NULL;
	int ret = pam_get_data(pamh, PASSWORD_VAR_NAME,
	    (const void**)(&authtok));
	if (ret == PAM_SUCCESS)
		return (authtok);
	if (ret == PAM_NO_MODULE_DATA)
		return (pw_fetch_lazy(pamh));
	pam_syslog(pamh, LOG_ERR, "password not available");
	return (NULL);
}

static int
pw_clear(pam_handle_t *pamh)
{
	int ret = pam_set_data(pamh, PASSWORD_VAR_NAME, NULL, NULL);
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

static int
decrypt_mount(pam_handle_t *pamh, const char *ds_name,
    const char *passphrase)
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
	int ret = lzc_load_key(ds_name, B_FALSE, (uint8_t *)key->value,
	    WRAPPING_KEY_LEN);
	pw_free(key);
	if (ret) {
		pam_syslog(pamh, LOG_ERR, "load_key failed: %d", ret);
		zfs_close(ds);
		return (-1);
	}
	ret = zfs_mount(ds, NULL, 0);
	if (ret) {
		pam_syslog(pamh, LOG_ERR, "mount failed: %d", ret);
		zfs_close(ds);
		return (-1);
	}
	zfs_close(ds);
	return (0);
}

static int
unmount_unload(pam_handle_t *pamh, const char *ds_name)
{
	zfs_handle_t *ds = zfs_open(g_zfs, ds_name, ZFS_TYPE_FILESYSTEM);
	if (ds == NULL) {
		pam_syslog(pamh, LOG_ERR, "dataset %s not found", ds_name);
		return (-1);
	}
	int ret = zfs_unmount(ds, NULL, 0);
	if (ret) {
		pam_syslog(pamh, LOG_ERR, "zfs_unmount failed with: %d", ret);
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

typedef struct {
	char *homes_prefix;
	char *runstatedir;
	char *homedir;
	char *dsname;
	uid_t uid;
	const char *username;
	int unmount_and_unload;
} zfs_key_config_t;

static int
zfs_key_config_load(pam_handle_t *pamh, zfs_key_config_t *config,
    int argc, const char **argv)
{
	config->homes_prefix = strdup("rpool/home");
	if (config->homes_prefix == NULL) {
		pam_syslog(pamh, LOG_ERR, "strdup failure");
		return (-1);
	}
	config->runstatedir = strdup(RUNSTATEDIR "/pam_zfs_key");
	if (config->runstatedir == NULL) {
		pam_syslog(pamh, LOG_ERR, "strdup failure");
		free(config->homes_prefix);
		return (-1);
	}
	const char *name;
	if (pam_get_user(pamh, &name, NULL) != PAM_SUCCESS) {
		pam_syslog(pamh, LOG_ERR,
		    "couldn't get username from PAM stack");
		free(config->runstatedir);
		free(config->homes_prefix);
		return (-1);
	}
	struct passwd *entry = getpwnam(name);
	if (!entry) {
		free(config->runstatedir);
		free(config->homes_prefix);
		return (-1);
	}
	config->uid = entry->pw_uid;
	config->username = name;
	config->unmount_and_unload = 1;
	config->dsname = NULL;
	config->homedir = NULL;
	for (int c = 0; c < argc; c++) {
		if (strncmp(argv[c], "homes=", 6) == 0) {
			free(config->homes_prefix);
			config->homes_prefix = strdup(argv[c] + 6);
		} else if (strncmp(argv[c], "runstatedir=", 12) == 0) {
			free(config->runstatedir);
			config->runstatedir = strdup(argv[c] + 12);
		} else if (strcmp(argv[c], "nounmount") == 0) {
			config->unmount_and_unload = 0;
		} else if (strcmp(argv[c], "prop_mountpoint") == 0) {
			if (config->homedir == NULL)
				config->homedir = strdup(entry->pw_dir);
		}
	}
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
		zfs_close(zhp);
		return (0);
	}

	target->dsname = strdup(zfs_get_name(zhp));
	zfs_close(zhp);
	return (1);
}

static char *
zfs_key_config_get_dataset(zfs_key_config_t *config)
{
	if (config->homedir != NULL &&
	    config->homes_prefix != NULL) {
		zfs_handle_t *zhp = zfs_open(g_zfs, config->homes_prefix,
		    ZFS_TYPE_FILESYSTEM);
		if (zhp == NULL) {
			pam_syslog(NULL, LOG_ERR, "dataset %s not found",
			    config->homes_prefix);
			return (NULL);
		}

		(void) zfs_iter_filesystems(zhp, find_dsname_by_prop_value,
		    config);
		zfs_close(zhp);
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
	size_t runtime_path_len = strlen(runtime_path);
	size_t counter_path_len = runtime_path_len + 1 + 10;
	char *counter_path = malloc(counter_path_len + 1);
	if (!counter_path) {
		return (-1);
	}
	counter_path[0] = 0;
	strcat(counter_path, runtime_path);
	snprintf(counter_path + runtime_path_len, counter_path_len, "/%d",
	    config->uid);
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
	(void) flags, (void) argc, (void) argv;

	if (pw_fetch_lazy(pamh) == NULL) {
		return (PAM_AUTH_ERR);
	}

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
	if (zfs_key_config_load(pamh, &config, argc, argv) == -1) {
		return (PAM_SERVICE_ERR);
	}
	if (config.uid < 1000) {
		zfs_key_config_free(&config);
		return (PAM_SUCCESS);
	}
	{
		if (pam_zfs_init(pamh) != 0) {
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		char *dataset = zfs_key_config_get_dataset(&config);
		if (!dataset) {
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		int key_loaded = is_key_loaded(pamh, dataset);
		if (key_loaded == -1) {
			free(dataset);
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		free(dataset);
		pam_zfs_free();
		if (! key_loaded) {
			pam_syslog(pamh, LOG_ERR,
			    "key not loaded, returning try_again");
			zfs_key_config_free(&config);
			return (PAM_PERM_DENIED);
		}
	}

	if ((flags & PAM_UPDATE_AUTHTOK) != 0) {
		const pw_password_t *token = pw_get(pamh);
		if (token == NULL) {
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		if (pam_zfs_init(pamh) != 0) {
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		char *dataset = zfs_key_config_get_dataset(&config);
		if (!dataset) {
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		if (change_key(pamh, dataset, token->value) == -1) {
			free(dataset);
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SERVICE_ERR);
		}
		free(dataset);
		pam_zfs_free();
		zfs_key_config_free(&config);
		if (pw_clear(pamh) == -1) {
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
	zfs_key_config_load(pamh, &config, argc, argv);
	if (config.uid < 1000) {
		zfs_key_config_free(&config);
		return (PAM_SUCCESS);
	}

	int counter = zfs_key_config_modify_session_counter(pamh, &config, 1);
	if (counter != 1) {
		zfs_key_config_free(&config);
		return (PAM_SUCCESS);
	}

	const pw_password_t *token = pw_get(pamh);
	if (token == NULL) {
		zfs_key_config_free(&config);
		return (PAM_SESSION_ERR);
	}
	if (pam_zfs_init(pamh) != 0) {
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	char *dataset = zfs_key_config_get_dataset(&config);
	if (!dataset) {
		pam_zfs_free();
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	if (decrypt_mount(pamh, dataset, token->value) == -1) {
		free(dataset);
		pam_zfs_free();
		zfs_key_config_free(&config);
		return (PAM_SERVICE_ERR);
	}
	free(dataset);
	pam_zfs_free();
	zfs_key_config_free(&config);
	if (pw_clear(pamh) == -1) {
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
	if (zfs_key_config_load(pamh, &config, argc, argv) != 0) {
		return (PAM_SESSION_ERR);
	}
	if (config.uid < 1000) {
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
		char *dataset = zfs_key_config_get_dataset(&config);
		if (!dataset) {
			pam_zfs_free();
			zfs_key_config_free(&config);
			return (PAM_SESSION_ERR);
		}
		if (unmount_unload(pamh, dataset) == -1) {
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
