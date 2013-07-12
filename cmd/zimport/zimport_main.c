/*
 * zimport_main.c
 *
 * @author : Prakash Surya <surya1@llnl.gov>
 * @date   : June 27, 2012
 *
 * TODO: Replace this heading with proper CDDL and ZFS heading.
 */

#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <sys/stat.h>

#include <libzfs.h>
#include <sys/nvpair_impl.h>
#include <sys/spa_impl.h>

/* XXX: Start libzfs_import.c
 *
 * This is ugly, but I think I need access to these prototypes and
 * definitions. For now just copy them here; this is obviously not a
 * long term solution.
 */

typedef struct config_entry {
	uint64_t		ce_txg;
	nvlist_t		*ce_config;
	struct config_entry	*ce_next;
} config_entry_t;

typedef struct vdev_entry {
	uint64_t		ve_guid;
	config_entry_t		*ve_configs;
	struct vdev_entry	*ve_next;
} vdev_entry_t;

typedef struct pool_entry {
	uint64_t		pe_guid;
	vdev_entry_t		*pe_vdevs;
	struct pool_entry	*pe_next;
} pool_entry_t;

typedef struct name_entry {
	char			*ne_name;
	uint64_t		ne_guid;
	struct name_entry	*ne_next;
} name_entry_t;

typedef struct pool_list {
	pool_entry_t		*pools;
	name_entry_t		*names;
} pool_list_t;

int add_config(libzfs_handle_t *, pool_list_t *, const char *, int, nvlist_t *);
nvlist_t * get_configs(libzfs_handle_t *, pool_list_t *, boolean_t);

/* XXX: End libzfs_import.c */

char *g_cache_path = "/tmp/zimport.cache";
libzfs_handle_t *g_zfs;

static int
zimport_config_write(nvlist_t *nvl, char *config_path)
{
	size_t buflen;
	char *buf, *tmp;
	int fd, ret = 0;

	VERIFY(nvlist_size(nvl, &buflen, NV_ENCODE_XDR) == 0);

	buf = malloc(buflen);
	if (buf == NULL) {
		fprintf(stderr, strerror(errno));
		ret = -errno;
		goto out;
	}

	tmp = malloc(MAXPATHLEN);
	if (tmp == NULL) {
		fprintf(stderr, strerror(errno));
		ret = -errno;
		goto free_buf;
	}

	VERIFY(nvlist_pack(nvl, &buf, &buflen, NV_ENCODE_XDR, KM_SLEEP) == 0);

	/*
	 * Write the configuration to disk.  We need to do the traditional
	 * 'write to temporary file, sync, move over original' to make sure we
	 * always have a consistent view of the data.
	 */

	(void) snprintf(tmp, MAXPATHLEN, "%s.tmp", config_path);

	if ((fd = open64(tmp, O_WRONLY | O_CREAT, 0644)) < 0) {
		fprintf(stderr, "cannot open '%s': %s\n",
						tmp, strerror(errno));
		ret = -errno;
		goto free_tmp;
	}

	if (pwrite64(fd, buf, buflen, 0) != buflen) {
		fprintf(stderr, "cannot write to '%s': %s\n",
						tmp, strerror(errno));
		ret = -errno;
		goto close;
	}

	if (fsync(fd) != 0) {
		fprintf(stderr, "failed to fsync '%s': %s\n",
						tmp, strerror(errno));
		ret = -errno;
		goto close;
	}

	if (rename(tmp, config_path) != 0) {
		fprintf(stderr, "cannot rename '%s' to '%s': %s\n",
					tmp, config_path, strerror(errno));
		ret = -errno;
		goto close;
	}

	remove(tmp);
close:
	close(fd);
free_tmp:
	free(tmp);
free_buf:
	free(buf);
out:
	return ret;
}

static int
zimport_cache_load(nvlist_t **nvl, char *config_path)
{
	struct stat64 statbuf;
	int fd, ret = 0;
	void *buf;

	if ((fd = open64(config_path, O_RDONLY)) < 0) {
		if (errno == ENOENT) {
			nvlist_alloc(nvl, NV_UNIQUE_NAME, 0);
			goto out;
		}

		fprintf(stderr, "connot open '%s': %s\n",
					config_path, strerror(errno));
		ret = -errno;
		goto out;
	}

	if (fstat64(fd, &statbuf) != 0) {
		fprintf(stderr, "failed to stat '%s': %s\n",
					config_path, strerror(errno));
		ret = -errno;
		goto close;
	}

	buf = malloc(statbuf.st_size);
	if (buf == NULL) {
		fprintf(stderr, strerror(errno));
		ret = -errno;
		goto close;
	}

	if (pread64(fd, buf, statbuf.st_size, 0) != statbuf.st_size) {
		fprintf(stderr, "cannot read from '%s': %s\n",
					config_path, strerror(errno));
		ret = -errno;
		goto free;
	}

	VERIFY(nvlist_unpack(buf, statbuf.st_size, nvl, KM_SLEEP) == 0);

free:
	free(buf);
close:
	close(fd);
out:
	return ret;
}

static int
zimport_cache_add(nvlist_t *cache, char *path, nvlist_t *nvl)
{
	int ret = 0;

	if ((ret = nvlist_add_nvlist(cache, path, nvl)) != 0) {
		fprintf(stderr, "failed to add nvlist\n");
		goto out;
	}

out:
	return ret;
}

static int
zimport_cache_rm(nvlist_t *cache, char *path, nvlist_t *nvl)
{
	int ret = 0;

	if ((ret = nvlist_remove(cache, path, DATA_TYPE_NVLIST)) != 0) {
		fprintf(stderr, "failed to remove nvlist\n");
		goto out;
	}

out:
	return ret;

}

static int
zimport_device_read(nvlist_t **nvl, char *path)
{
	struct stat64 statbuf;
	int fd, ret = 0;

	if ((fd = open64(path, O_RDONLY)) < 0) {
		fprintf(stderr, "cannot open '%s': %s\n",
						path, strerror(errno));
		ret = -errno;
		goto out;
	}

	if (fstat64_blk(fd, &statbuf) != 0) {
		fprintf(stderr, "failed to stat '%s': %s\n",
						path, strerror(errno));
		ret = -errno;
		goto close;
	}

	if ((ret = zpool_read_label(fd, nvl)) != 0) {
		fprintf(stderr, "failed to read label\n");
		goto close;
	}

close:
	close(fd);
out:
	return ret;
}

static int
zimport_pools_nvlist(nvlist_t **pools, nvlist_t *cache)
{
	pool_list_t pool_list = { 0 };
	nvpair_t *elem = NULL;
	nvlist_t *value;
	pool_entry_t *pe, *penext;
	vdev_entry_t *ve, *venext;
	name_entry_t *ne, *nenext;
	config_entry_t *ce, *cenext;
	char *path;
	int ret = 0;

	while ((elem = nvlist_next_nvpair(cache, elem)) != NULL) {
		path = nvpair_name(elem);
		VERIFY(nvpair_value_nvlist(elem, &value) == 0);
		VERIFY(add_config(g_zfs, &pool_list, path, 0, value) == 0);
	}

	*pools = get_configs(g_zfs, &pool_list, B_TRUE);
	if (*pools == NULL)
		ret = -1;

	for (pe = pool_list.pools; pe != NULL; pe = penext) {
		penext = pe->pe_next;
		for (ve = pe->pe_vdevs; ve != NULL; ve = venext) {
			venext = ve->ve_next;
			for (ce = ve->ve_configs; ce != NULL; ce = cenext) {
				cenext = ce->ce_next;
				if (ce->ce_config)
					nvlist_free(ce->ce_config);
				free(ce);
			}
			free(ve);
		}
		free(pe);
	}

	for (ne = pool_list.names; ne != NULL; ne = nenext) {
		nenext = ne->ne_next;
		if (ne->ne_name)
			free(ne->ne_name);
		free(ne);
	}

	return ret;
}

static int
zimport_path_in_cache(nvlist_t *cache, char *path)
{
	nvpair_t *elem = NULL;
	char *cachedev;

	while ((elem = nvlist_next_nvpair(cache, elem)) != NULL) {
		cachedev = nvpair_name(elem);
		if (strcmp(cachedev, path) == 0)
			return 0;
	}

	return -1;
}

static int
zimport_verify_mirror_devices(nvlist_t *cache, nvlist_t **vdevs, uint_t count)
{
	char *path;
	int i;

	for (i = 0; i < count; i++) {
		VERIFY(nvlist_lookup_string(vdevs[i],
					ZPOOL_CONFIG_PATH, &path) == 0);

		/* if path is not in the cache */
		if (zimport_path_in_cache(cache, path) != 0)
			return -1;
	}

	return 0;
}

static int
zimport_verify_raidz_devices(nvlist_t *cache, nvlist_t **vdevs,
			     uint_t count, uint64_t nparity)
{
	char *path;
	int i;

	for (i = 0; i < count; i++) {
		VERIFY(nvlist_lookup_string(vdevs[i],
					ZPOOL_CONFIG_PATH, &path) == 0);

		/* if path is not in the cache */
		if (zimport_path_in_cache(cache, path) != 0)
			return -1;
	}

	return 0;
}

static int
zimport_verify_root_devices(nvlist_t *cache, nvlist_t **vdevs, uint_t count)
{
	nvlist_t **kids;
	uint_t nkids;
	int i;
	char *type;
	uint64_t nparity;

	for (i = 0; i < count; i++) {
		VERIFY(nvlist_lookup_string(vdevs[i],
					ZPOOL_CONFIG_TYPE, &type) == 0);

		if (strcmp(type, "missing") == 0) {
			return -1;
		} else if (strcmp(type, "mirror") == 0) {
			VERIFY(nvlist_lookup_nvlist_array(vdevs[i],
				ZPOOL_CONFIG_CHILDREN, &kids, &nkids) == 0);
			if (zimport_verify_mirror_devices(cache,
							kids, nkids) != 0)
				return -1;
		} else if (strcmp(type, "raidz") == 0) {
			VERIFY(nvlist_lookup_nvlist_array(vdevs[i],
				ZPOOL_CONFIG_CHILDREN, &kids, &nkids) == 0);
			VERIFY(nvlist_lookup_uint64(vdevs[i],
				ZPOOL_CONFIG_NPARITY, &nparity) == 0);
			if (zimport_verify_raidz_devices(cache,
						kids, nkids, nparity) != 0)
				return -1;
		} else {
			/* unknown type */
			ASSERT(0);
		}
	}

	return 0;
}

static int
zimport_verify_pool_devices(nvlist_t *cache, nvlist_t *pool)
{
	nvlist_t *vdev_tree, **root;
	uint_t nroot;

	VERIFY(nvlist_lookup_nvlist(pool,
			ZPOOL_CONFIG_VDEV_TREE, &vdev_tree) == 0);

	VERIFY(nvlist_lookup_nvlist_array(vdev_tree,
			ZPOOL_CONFIG_CHILDREN, &root, &nroot) == 0);

	return zimport_verify_root_devices(cache, root, nroot);
}

int
main(int argc, char **argv)
{
	nvlist_t *cache, *device, *pools, *pool;
	nvpair_t *elem = NULL;
	char c, dev_path[MAXPATHLEN] = { '\0' };
	char cache_path[MAXPATHLEN] = { '\0' };
	boolean_t aopt = B_FALSE, ropt = B_FALSE;
	int ret = 0;

	/* Set up default cache file path */
	strncpy(cache_path, g_cache_path, MAXPATHLEN);
	cache_path[MAXPATHLEN-1] = '\0';

	while ((c = getopt(argc, argv, "a:c:r:")) != -1) {
		switch (c) {
		case 'a':
			aopt = B_TRUE;
			strncpy(dev_path, optarg, MAXPATHLEN);
			dev_path[MAXPATHLEN-1] = '\0';
			break;
		case 'r':
			ropt = B_TRUE;
			strncpy(dev_path, optarg, MAXPATHLEN);
			dev_path[MAXPATHLEN-1] = '\0';
			break;
		case 'c':
			if (strlen(optarg) >= MAXPATHLEN) {
				fprintf(stderr, "config path exceeds max "
							"length '%s'", optarg);
				return 1;
			}
			strncpy(cache_path, optarg, MAXPATHLEN);
			cache_path[MAXPATHLEN-1] = '\0';
			break;
		default:
			fprintf(stderr, "invalid option '%c'\n", optopt);
			break;
		}
	}

	if (aopt && ropt) {
		fprintf(stderr, "'a' and 'r' options are mutually exclusive\n");
		return 1;
	}

	if (!aopt && !ropt) {
		fprintf(stderr, "'a' or 'r' option must be specified\n");
		return 1;
	}

	if ((g_zfs = libzfs_init()) == NULL) {
		fprintf(stderr, "failed to initialize libzfs\n");
		ret = -EAGAIN;
		goto out;
	}

	libzfs_print_on_error(g_zfs, B_TRUE);

	if ((ret = zimport_device_read(&device, dev_path)) != 0 ||
	    device == NULL) {
		fprintf(stderr, "failed to read device\n");
		goto fini;
	}

	if ((ret = zimport_cache_load(&cache, cache_path)) != 0) {
		fprintf(stderr, "failed to load cache\n");
		goto free_device;
	}

	if (aopt) {
		if ((ret = zimport_cache_add(cache, dev_path, device)) != 0) {
			fprintf(stderr, "failed to add device\n");
			goto free_cache;
		}
	} else if (ropt) {
		if ((ret = zimport_cache_rm(cache, dev_path, device)) != 0) {
			fprintf(stderr, "failed to remove device\n");
			goto free_cache;
		}
	}

	if ((ret = zimport_config_write(cache, cache_path)) != 0) {
		fprintf(stderr, "failed to write config\n");
		goto free_cache;
	}

	/* do not try to import when removing a device */
	if (ropt)
		goto free_cache;

	if ((ret = zimport_pools_nvlist(&pools, cache)) != 0) {
		fprintf(stderr, "failed to build pools nvlist\n");
		goto free_cache;
	}

	while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {

		VERIFY(nvpair_value_nvlist(elem, &pool) == 0);

		if (zimport_verify_pool_devices(cache, pool) != 0) {
			fprintf(stderr, "missing devices\n");
			ret = -1;
			goto free_pools;
		}

		ret = zpool_import(g_zfs, pool, NULL, NULL);
		if (ret != 0 && errno != 0)
			fprintf(stderr, "import failed\n");
	}

free_pools:
	nvlist_free(pools);
free_cache:
	nvlist_free(cache);
free_device:
	nvlist_free(device);
fini:
	libzfs_fini(g_zfs);
out:
	return ret;
}
