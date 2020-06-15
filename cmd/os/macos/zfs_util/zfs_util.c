/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 * Copyright (c) 2007 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.2 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/loadable_fs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/priv.h>

#include <sys/zfs_context.h>
#include <libzfs.h>
#include <libzutil.h>

#include <libkern/OSByteOrder.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/storage/IODVDMedia.h>

#include <CommonCrypto/CommonDigest.h>
#include <uuid/uuid.h>

#ifndef FSUC_GETUUID
#define	FSUC_GETUUID	'k'
#endif

#ifndef FSUC_SETUUID
#define	FSUC_SETUUID	's'
#endif

#define	ZPOOL_IMPORT_ALL_COOKIE	\
	"/var/run/org.openzfsonosx.zpool-import-all.didRun"
#define	INVARIANT_DISKS_IDLE_FILE \
	"/var/run/disk/invariant.idle"
#define	IS_INVARIANT_DISKS_LOADED_CMD \
	"/bin/launchctl list -x org.openzfsonosx.InvariantDisks &>/dev/null"
#define	INVARIANT_DISKS_TIMEOUT_SECONDS	60

#ifdef DEBUG
int zfs_util_debug = 1;
#else
int zfs_util_debug = 0;
#endif

#define	printf	zfs_util_log

// #define	ZFS_AUTOIMPORT_ZPOOL_CACHE_ONLY

const char *progname;
libzfs_handle_t *g_zfs;

static void
zfs_util_log(const char *format, ...)
{
	va_list args;
	char buf[1024];

	if (zfs_util_debug == 0)
		return;

	setlogmask(LOG_UPTO(LOG_NOTICE));

	va_start(args, format);
	(void) vsnprintf(buf, sizeof (buf), format, args);
	fputs(buf, stderr);
	va_end(args);

	if (*(&buf[strlen(buf) - 1]) == '\n')
		*(&buf[strlen(buf) - 1]) = '\0';
	va_start(args, format);
	vsyslog(LOG_NOTICE, format, args);
	va_end(args);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s action_arg device_arg [Flags] \n", progname);
	fprintf(stderr, "action_arg:\n");
	fprintf(stderr, "       -%c (Probe for mounting)\n", FSUC_PROBE);
	fprintf(stderr, "device_arg:\n");
	fprintf(stderr, "       device we are acting upon (for example, "
	    "'disk0s1')\n");
	fprintf(stderr, "Flags:\n");
	fprintf(stderr, "       required for Probe\n");
	fprintf(stderr, "       indicates removable or fixed (for example "
	    "'fixed')\n");
	fprintf(stderr, "       indicates readonly or writable (for example "
	    "'readonly')\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "       %s -p disk0s1 removable readonly\n", progname);
}

/*
 * Given disk2s1, look up "disk2" is IOKit and attempt to determine if
 * it is an optical device.
 */
static int
is_optical_media(const char *bsdname)
{
	CFMutableDictionaryRef matchingDict;
	int ret = 0;
	io_service_t service, start;
	kern_return_t kernResult;
	io_iterator_t iter;

	if ((matchingDict = IOBSDNameMatching(kIOMasterPortDefault,
	    0, bsdname)) == NULL)
		return (0);

	start = IOServiceGetMatchingService(kIOMasterPortDefault, matchingDict);
	if (IO_OBJECT_NULL == start)
		return (0);

	service = start;

	/*
	 * Create an iterator across all parents of the service object
	 * passed in. since only disk2 would match with ConfirmsTo,
	 * and not disk2s1, so we search the parents until we find "Whole",
	 * ie, disk2.
	 */
	kernResult = IORegistryEntryCreateIterator(service,
	    kIOServicePlane,
	    kIORegistryIterateRecursively | kIORegistryIterateParents,
	    &iter);

	if (KERN_SUCCESS == kernResult) {
		Boolean isWholeMedia = false;
		IOObjectRetain(service);
		do {
			// Lookup "Whole" if we can
			if (IOObjectConformsTo(service, kIOMediaClass)) {
				CFTypeRef wholeMedia;
				wholeMedia =
				    IORegistryEntryCreateCFProperty(service,
				    CFSTR(kIOMediaWholeKey),
				    kCFAllocatorDefault,
				    0);
				if (wholeMedia) {
					isWholeMedia =
					    CFBooleanGetValue(wholeMedia);
					CFRelease(wholeMedia);
				}
			}

			// If we found "Whole", check the service type.
			if (isWholeMedia &&
			    ((IOObjectConformsTo(service, kIOCDMediaClass)) ||
			    (IOObjectConformsTo(service, kIODVDMediaClass)))) {
				ret = 1; // Is optical, skip
			}

			IOObjectRelease(service);
		} while ((service = IOIteratorNext(iter)) && !isWholeMedia);
		IOObjectRelease(iter);
	}

	IOObjectRelease(start);
	return (ret);
}


#ifdef ZFS_AUTOIMPORT_ZPOOL_CACHE_ONLY

#define	PRIV_SYS_CONFIG 0
static __inline int
priv_ineffect(int priv)
{
	assert(priv == PRIV_SYS_CONFIG);
	return (geteuid() == 0);
}

/*
 * Perform the import for the given configuration.  This passes the heavy
 * lifting off to zpool_import_props(), and then mounts the datasets contained
 * within the pool.
 */
static int
do_import(nvlist_t *config, const char *newname, const char *mntopts,
    nvlist_t *props, int flags)
{
	zpool_handle_t *zhp;
	char *name;
	uint64_t state;
	uint64_t version;

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);

	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_POOL_STATE, &state) == 0);
	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_VERSION, &version) == 0);
	if (!SPA_VERSION_IS_SUPPORTED(version)) {
		printf("cannot import '%s': pool is formatted using an "
		    "unsupported ZFS version\n", name);
		return (1);
	} else if (state != POOL_STATE_EXPORTED &&
	    !(flags & ZFS_IMPORT_ANY_HOST)) {
		uint64_t hostid;

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID,
		    &hostid) == 0) {
			unsigned long system_hostid = gethostid() & 0xffffffff;

			if ((unsigned long)hostid != system_hostid) {
				char *hostname;
				uint64_t timestamp;
				time_t t;

				verify(nvlist_lookup_string(config,
				    ZPOOL_CONFIG_HOSTNAME, &hostname) == 0);
				verify(nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_TIMESTAMP, &timestamp) == 0);
				t = timestamp;
				printf("cannot import " "'%s': pool may be in "
				    "use from other system, it was last "
				    "accessed by %s (hostid: 0x%lx) on %s\n",
				    name, hostname, (unsigned long)hostid,
				    asctime(localtime(&t)));
				printf("use '-f' to import anyway\n");
				return (1);
			}
		} else {
			printf("cannot import '%s': pool may be in use from "
			    "other system\n", name);
			printf("use '-f' to import anyway\n");
			return (1);
		}
	}

	if (zpool_import_props(g_zfs, config, newname, props, flags) != 0)
		return (1);

	if (newname != NULL)
		name = (char *)newname;

	if ((zhp = zpool_open_canfail(g_zfs, name)) == NULL)
		return (1);

	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    !(flags & ZFS_IMPORT_ONLY) &&
	    zpool_enable_datasets(zhp, mntopts, 0) != 0) {
		zpool_close(zhp);
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

static int
zpool_import_by_guid(uint64_t searchguid)
{
	int err = 0;
	nvlist_t *pools = NULL;
	nvpair_t *elem;
	nvlist_t *config;
	nvlist_t *found_config = NULL;
	nvlist_t *policy = NULL;
	boolean_t first;
	int flags = ZFS_IMPORT_NORMAL;
	uint32_t rewind_policy = ZPOOL_NO_REWIND;
	uint64_t pool_state, txg = -1ULL;
	importargs_t idata = { 0 };
#ifdef ZFS_AUTOIMPORT_ZPOOL_STATUS_OK_ONLY
	char *msgid;
	zpool_status_t reason;
	zpool_errata_t errata;
#endif

	if ((g_zfs = libzfs_init()) == NULL)
		return (1);

	/* In the future, we can capture further policy and include it here */
	if (nvlist_alloc(&policy, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint64(policy, ZPOOL_LOAD_REQUEST_TXG, txg) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY,
	    rewind_policy) != 0)
		goto error;

	if (!priv_ineffect(PRIV_SYS_CONFIG)) {
		printf("cannot discover pools: permission denied\n");
		nvlist_free(policy);
		return (1);
	}

	idata.guid = searchguid;

	pools = zpool_search_import(g_zfs, &idata);

	if (pools == NULL && idata.exists) {
		printf("cannot import '%llu': a pool with that guid is already "
		    "created/imported\n", searchguid);
		err = 1;
	} else if (pools == NULL) {
		printf("cannot import '%llu': no such pool available\n",
		    searchguid);
		err = 1;
	}

	if (err == 1) {
		nvlist_free(policy);
		return (1);
	}

	/*
	 * At this point we have a list of import candidate configs. Even though
	 * we were searching by guid, we still need to post-process the list to
	 * deal with pool state.
	 */
	err = 0;
	elem = NULL;
	first = B_TRUE;
	while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {

		verify(nvpair_value_nvlist(elem, &config) == 0);

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    &pool_state) == 0);
		if (pool_state == POOL_STATE_DESTROYED)
			continue;

		verify(nvlist_add_nvlist(config, ZPOOL_LOAD_POLICY,
		    policy) == 0);

		uint64_t guid;

		/*
		 * Search for a pool by guid.
		 */
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		if (guid == searchguid)
			found_config = config;
	}

	/*
	 * If we were searching for a specific pool, verify that we found a
	 * pool, and then do the import.
	 */
	if (err == 0) {
		if (found_config == NULL) {
			printf("zfs.util: FATAL cannot import '%llu': "
			    "no such pool available\n", searchguid);
			err = B_TRUE;
		} else {
#ifdef ZFS_AUTOIMPORT_ZPOOL_STATUS_OK_ONLY
			reason = zpool_import_status(config, &msgid, &errata);
			if (reason == ZPOOL_STATUS_OK)
				err |= do_import(found_config, NULL, NULL, NULL,
				    flags);
			else
				err = 1;
#else
			err |= do_import(found_config, NULL, NULL, NULL, flags);
#endif
		}
	}

error:
	nvlist_free(pools);
	nvlist_free(policy);
	libzfs_fini(g_zfs);

	return (err ? 1 : 0);
}
#endif // ZFS_AUTOIMPORT_ZPOOL_CACHE_ONLY

struct probe_args {
	char *pool_name;
	int name_len;
	uint64_t pool_guid;
	uint64_t vdev_guid;
};
typedef struct probe_args probe_args_t;

char *UNKNOWN_STRING = "Unknown";

static int
zfs_probe(const char *devpath, probe_args_t *args)
{
	nvlist_t *config = NULL;
	int ret = FSUR_UNRECOGNIZED;
	int fd;
	uint64_t guid;
	int i, again = 0;
	struct stat sbuf;

	// printf("+zfs_probe : devpath %s\n", devpath);

	if (system(IS_INVARIANT_DISKS_LOADED_CMD) == 0) {
		/* InvariantDisks is loaded */
		i = 0;
		while (i != INVARIANT_DISKS_TIMEOUT_SECONDS) {
			if (stat(INVARIANT_DISKS_IDLE_FILE, &sbuf) == 0) {
				// printf("Found %s after %d iterations of "
				//    "sleeping 1 second\n",
				//    INVARIANT_DISKS_IDLE_FILE, i);
				break;
			}
			sleep(1);
			i++;
		}
		if (i == INVARIANT_DISKS_TIMEOUT_SECONDS) {
			printf("zfs.util: FATAL: File %s not found within "
			    "%d seconds\n",
			    INVARIANT_DISKS_IDLE_FILE,
			    INVARIANT_DISKS_TIMEOUT_SECONDS);
		}
	}

retry:

	if ((fd = open(devpath, O_RDONLY)) < 0) {
		printf("zfs.util: FATAL: Could not open devpath %s: %d\n",
		    devpath, errno);
		goto out;
	}

	if (zpool_read_label(fd, &config, NULL) != 0) {
		printf("zfs.util: FATAL: Could not read label devpath %s: %d\n",
		    devpath, errno);
		(void) close(fd);
		goto out;
	}

	(void) close(fd);

	if (config != NULL) {
		char *name;
		ret = FSUR_RECOGNIZED;
		args->pool_guid = (nvlist_lookup_uint64(config,
		    ZPOOL_CONFIG_POOL_GUID, &guid) == 0) ? guid : 0;
		args->vdev_guid = (nvlist_lookup_uint64(config,
		    ZPOOL_CONFIG_GUID, &guid) == 0) ? guid : 0;
		if (args->pool_name &&
		    nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0)
			strlcpy(args->pool_name, name, MAXPATHLEN);
		nvlist_free(config);
	} else {
		if (again++ < 5) {
			// printf("zfs.util: read_label config is NULL\n");
			sleep(1);
			goto retry;
		}
		// printf("zfs.util: FATAL: read_label config is NULL\n");
	}
out:
	printf("-zfs_probe : ret %s\n",
	    ret == FSUR_RECOGNIZED ? "FSUR_RECOGNIZED" : "FSUR_UNRECOGNIZED");
	return (ret);
}

/* Look up "/dev/rdisk5" in ioreg to see if it is a pseudodisk */
static int
zfs_probe_iokit(const char *devpath, probe_args_t *args)
{
	const char *name;
	CFMutableDictionaryRef matchingDict;
	io_service_t service = IO_OBJECT_NULL;
	CFStringRef cfstr = NULL;
	int result = FSUR_UNRECOGNIZED;

	// Make sure it is "disk5s1" not "/dev/" and not "rdisk"
	if (strncmp("/dev/disk", devpath, 9) == 0)
		name = &devpath[5];
	else if (strncmp("/dev/rdisk", devpath, 10) == 0)
		name = &devpath[6];
	else if (strncmp("rdisk", devpath, 5) == 0)
		name = &devpath[1];
	else
		name = devpath;

	printf("%s: looking for '%s' in ioreg\n", __func__, name);

	matchingDict = IOBSDNameMatching(kIOMasterPortDefault, 0, name);
	if (NULL == matchingDict) {
		printf("%s: IOBSDNameMatching returned NULL dictionary\n",
		    __func__);
		goto fail;
	}

	/*
	 * Fetch the object with the matching BSD node name.
	 * Note that there should only be one match, so
	 * IOServiceGetMatchingService is used instead of
	 * IOServiceGetMatchingServices to simplify the code.
	 */
	service = IOServiceGetMatchingService(kIOMasterPortDefault,
	    matchingDict);

	if (IO_OBJECT_NULL == service) {
		printf("%s: IOServiceGetMatchingService returned NULL.\n",
		    __func__);
		goto fail;
	}

	if (IOObjectConformsTo(service, kIOMediaClass)) {
		cfstr = IORegistryEntryCreateCFProperty(service,
		    CFSTR("ZFS Dataset"), kCFAllocatorDefault, 0);
		if (cfstr != NULL) {
			const char *str;

			str = CFStringGetCStringPtr(cfstr,
			    kCFStringEncodingUTF8);

			if (str != NULL)
				(void) strlcpy(args->pool_name, str,
				    sizeof (io_name_t));

			result = FSUR_RECOGNIZED;
		}
	}

fail:
	if (service != IO_OBJECT_NULL)
		IOObjectRelease(service);
	if (cfstr != NULL)
		CFRelease(cfstr);

	printf("%s: result %s name '%s'\n", __func__,
	    result == FSUR_RECOGNIZED ? "FSUR_RECOGNIZED" :
	    result == FSUR_UNRECOGNIZED ? "FSUR_UNRECOGNIZED" :
	    "UNKNOWN",
	    result == FSUR_RECOGNIZED ? args->pool_name : "");

	return (result);
}

#ifdef ZFS_AUTOIMPORT_ZPOOL_CACHE_ONLY
void
zpool_read_cachefile(void)
{
	int fd;
	struct stat stbf;
	void *buf = NULL;
	nvlist_t *nvlist, *child;
	nvpair_t *nvpair;
	uint64_t guid;
	int importrc = 0;

	// printf("reading cachefile\n");

	fd = open(ZPOOL_CACHE, O_RDONLY);
	if (fd < 0)
		return;

	if (fstat(fd, &stbf) || !stbf.st_size)
		goto out;

	buf = kmem_alloc(stbf.st_size, 0);
	if (!buf)
		goto out;

	if (read(fd, buf, stbf.st_size) != stbf.st_size)
		goto out;

	if (nvlist_unpack(buf, stbf.st_size, &nvlist, KM_PUSHPAGE) != 0)
		goto out;

	nvpair = NULL;
	while ((nvpair = nvlist_next_nvpair(nvlist, nvpair)) != NULL) {
		if (nvpair_type(nvpair) != DATA_TYPE_NVLIST)
			continue;

		VERIFY(nvpair_value_nvlist(nvpair, &child) == 0);

		printf("Cachefile has pool '%s'\n", nvpair_name(nvpair));

		if (nvlist_lookup_uint64(child, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0) {
			printf("Cachefile has pool '%s' guid %llu\n",
			    nvpair_name(nvpair), guid);

			importrc = zpool_import_by_guid(guid);
			printf("zpool import error %d\n", importrc);
		}

	}
	nvlist_free(nvlist);

out:
	close(fd);
	if (buf)
		kmem_free(buf, stbf.st_size);

}
#endif


/*
 * Each vdev in a pool should each have unique uuid?
 */
static int
zfs_util_uuid_gen(probe_args_t *probe, char *uuid_str)
{
	unsigned char uuid[CC_MD5_DIGEST_LENGTH];
	// MD5_CTX  md5c;
	CC_MD5_CTX md5c;
	/* namespace (generated by uuidgen) */
	/* 50670853-FBD2-4EC3-9802-73D847BF7E62 */
	char namespace[16] = {0x50, 0x67, 0x08, 0x53, /* - */
			0xfb, 0xd2, /* - */ 0x4e, 0xc3, /* - */
			0x98, 0x02, /* - */
			0x73, 0xd8, 0x47, 0xbf, 0x7e, 0x62};

	/* Validate arguments */
	if (!probe->vdev_guid) {
		printf("zfs.util: FATAL: %s missing argument\n", __func__);
		return (EINVAL);
	}

	/*
	 * UUID version 3 (MD5) namespace variant:
	 * hash namespace (uuid) together with name
	 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

	CC_MD5_Init(&md5c);
	CC_MD5_Update(&md5c, &namespace, sizeof (namespace));
	CC_MD5_Update(&md5c, &probe->vdev_guid, sizeof (probe->vdev_guid));
	CC_MD5_Final(uuid, &md5c);

#pragma GCC diagnostic pop

	/*
	 * To make UUID version 3, twiddle a few bits:
	 * xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
	 * [uint32]-[uin-t32]-[uin-t32][uint32]
	 * M should be 0x3 to indicate uuid v3
	 * N should be 0x8, 0x9, 0xa, or 0xb
	 */
	uuid[6] = (uuid[6] & 0x0F) | 0x30;
	uuid[8] = (uuid[8] & 0x3F) | 0x80;

	// Convert binary to ascii
	uuid_unparse_upper(uuid, uuid_str);

	return (0);
}

struct attrNameBuf {
	uint32_t length;
	attrreference_t nameRef;
	char name[MAXPATHLEN];
} __attribute__((aligned(4), packed));

int
main(int argc, char **argv)
{
	struct statfs *statfs;
	char blockdevice[MAXPATHLEN];
	char rawdevice[MAXPATHLEN];
	char what;
	char *cp;
	char *devname;
	probe_args_t probe_args;
	struct stat sb;
	int ret = FSUR_INVAL;
	int i, num, len, is_mounted = 0;
	struct attrlist attr;
	struct attrNameBuf nameBuf;
	char volname[MAXPATHLEN];
	char *pool_name = NULL;

	/* save & strip off program name */
	progname = argv[0];
	argc--;
	argv++;

	if (argc < 2 || argv[0][0] != '-') {
		usage();
		goto out;
	}

	what = argv[0][1];
	printf("zfs.util called with option %c: pid %d\n", what, getpid());

	devname = argv[1];
	cp = strrchr(devname, '/');
	if (cp != 0)
		devname = cp + 1;
	if (*devname == 'r')
		devname++;

/* XXX Only checking ZFS pseudo devices, so this can be skipped */
/* We have to check all probe devices to get rid of the popup */
	if (is_optical_media(devname)) {
		printf("zfs.util: is_optical_media(%s)\n", devname);
		goto out;
	}

	(void) snprintf(rawdevice, sizeof (rawdevice), "/dev/r%s", devname);
	(void) snprintf(blockdevice, sizeof (blockdevice), "/dev/%s", devname);
	// printf("blockdevice is %s\n", blockdevice);


	/* Sometimes this is a bit of a race, so we will retry a few times */
	for (i = 0; i < 5; i++) {

		if (stat(blockdevice, &sb) == 0) break;

		// printf("%s: %d stat %s failed, %s\n", progname, i,
		// blockdevice, strerror(errno));
		sleep(1);
	}
	if (i >= 5) {
		printf("%s: FATAL: stat %s failed, %s\n", progname, blockdevice,
		    strerror(errno));
		goto out;
	}

	/*
	 * XXX Should check vfs_typenum is ZFS, and also must check
	 * for com.apple.mimic_hfs mounts (somehow)
	 * Check if the blockdevice refers to a mounted filesystem
	 */
	do {
		num = getmntinfo(&statfs, MNT_NOWAIT);
		if (num <= 0) {
			printf("%s: FATAL: getmntinfo error %d\n",
			    __func__, num);
			break;
		}

		len = strlen(blockdevice);
		for (i = 0; i < num; i++) {
			if (strlen(statfs[i].f_mntfromname) == len &&
			    strcmp(statfs[i].f_mntfromname,
			    blockdevice) == 0) {
				// printf("matched mountpoint %s\n",
				//  statfs[i].f_mntonname);
				is_mounted = B_TRUE;
				break;
			}
			/* Skip this mountpoint */
		}

		if (!is_mounted) {
			printf("%s no match - not mounted\n", __func__);
			break;
		}
	} while (0);


	bzero(&probe_args, sizeof (probe_args_t));
	len = MAXNAMELEN;
	pool_name = kmem_alloc(len, KM_SLEEP);
	if (!pool_name) {
		printf("FATAL: alloc failed\n");
		ret = FSUR_UNRECOGNIZED;
		goto out;
	}

	probe_args.pool_name = pool_name;
	probe_args.name_len = len;

	/* Check the request type */
	switch (what) {
		case FSUC_PROBE:

		/* XXX For now only checks mounted fs (root fs) */
		if (!is_mounted) {
			printf("FSUR_PROBE : unmounted fs: %s\n",
			    rawdevice);

			/* rawdevice might be pseudo for devdisk mounts */
			ret = zfs_probe_iokit(rawdevice, &probe_args);

			/* otherwise, read disk */
			if (ret == FSUR_UNRECOGNIZED)
				ret = zfs_probe(rawdevice, &probe_args);

			/*
			 * Validate guid and name, valid vdev
			 * must have a vdev_guid, but not
			 * necessarily a pool_guid
			 */
			if (ret == FSUR_RECOGNIZED &&
			    (probe_args.vdev_guid == 0)) {
				ret = FSUR_UNRECOGNIZED;
			}

			if (ret == FSUR_RECOGNIZED) {
				printf("FSUC_PROBE %s : FSUR_RECOGNIZED :"
				    " %s : pool guid 0x%016LLx vdev guid "
				    "0x%016LLx\n",
				    blockdevice, probe_args.pool_name,
				    probe_args.pool_guid,
				    probe_args.vdev_guid);
				/* Output pool name for DiskArbitration */
				write(1, probe_args.pool_name,
				    strlen(probe_args.pool_name));
			} else {
				printf("FSUC_PROBE %s : FSUR_UNRECOGNIZED :"
				    " %d\n", blockdevice, ret);
				ret = FSUR_UNRECOGNIZED;
			}

			break;

		} else {  /* is_mounted == true */

			bzero(&attr, sizeof (attr));
			bzero(&nameBuf, sizeof (nameBuf));
			bzero(&volname, sizeof (volname));
			attr.bitmapcount = 5;
			attr.volattr = ATTR_VOL_INFO | ATTR_VOL_NAME;

			ret = getattrlist(statfs[i].f_mntonname, &attr,
			    &nameBuf, sizeof (nameBuf), 0);
			if (ret != 0) {
				printf("%s FATAL: couldn't stat mount [%s]\n",
				    __func__, statfs[i].f_mntonname);
				ret = FSUR_UNRECOGNIZED;
				break;
			}
			if (nameBuf.length < offsetof(struct attrNameBuf,
			    name)) {
				printf("PROBE: FATAL: short attrlist return\n");
				ret = FSUR_UNRECOGNIZED;
				break;
			}
			if (nameBuf.length > sizeof (nameBuf)) {
				printf("PROBE: FATAL: overflow attrlist\n");
				ret = FSUR_UNRECOGNIZED;
				break;
			}

			snprintf(volname, nameBuf.nameRef.attr_length, "%s",
			    ((char *)&nameBuf.nameRef) +
			    nameBuf.nameRef.attr_dataoffset);

			printf("volname [%s]\n", volname);
			write(1, volname, strlen(volname));
			ret = FSUR_RECOGNIZED;
			break;

		} // ismounted

		break;

		/* Done */

		case FSUC_GETUUID:
		{
			uint32_t buf[5];

			/* Try to get a UUID either way */
			/* First, zpool vdev disks */
			if (!is_mounted) {
				char uuid[40];

				bzero(&probe_args, sizeof (probe_args_t));

				ret = zfs_probe(rawdevice, &probe_args);

				/* Validate vdev guid */
				if (ret == FSUR_RECOGNIZED &&
				    probe_args.vdev_guid == 0) {
					ret = FSUR_UNRECOGNIZED;
				}

				if (ret != FSUR_RECOGNIZED) {
					printf("FSUC_GET_UUID %s : "
					    "FSUR_UNRECOGNIZED %d\n",
					    blockdevice, ret);
					ret = FSUR_IO_FAIL;
					break;
				}

				/* Generate valid UUID from guids */
				if (zfs_util_uuid_gen(&probe_args, uuid) != 0) {
					printf("FSUC_GET_UUID %s : "
					    "uuid_gen error %d\n",
					    blockdevice, ret);
					ret = FSUR_IO_FAIL;
					break;
				}

				printf("FSUC_GET_UUID %s : FSUR_RECOGNIZED :"
				    " pool guid 0x%016llx :"
				    " vdev guid 0x%016llx : UUID %s\n",
				    blockdevice, probe_args.pool_guid,
				    probe_args.vdev_guid, uuid);

				/* Output the vdev guid for DiskArbitration */
				write(1, uuid, sizeof (uuid));
				ret = FSUR_IO_SUCCESS;

				break;

			} else { /* is_mounted == true */

				/* Otherwise, ZFS filesystem pseudo device */

				// struct attrlist attr;
				bzero(&buf, sizeof (buf));
				bzero(&attr, sizeof (attr));
				attr.bitmapcount = 5;
				attr.volattr = ATTR_VOL_INFO | ATTR_VOL_UUID;

				/* Retrieve UUID from mp */
				ret = getattrlist(statfs[i].f_mntonname, &attr,
				    &buf, sizeof (buf), 0);
				if (ret != 0) {
					printf("%s FATAL: couldn't stat "
					    "mount [%s]\n",
					    __func__, statfs[i].f_mntonname);
					ret = FSUR_IO_FAIL;
					break;
				}

				/*
				 * buf[0] is count of uint32_t values returned,
				 * including itself
				 */
				if (buf[0] < (5 * sizeof (uint32_t))) {
					printf("FATAL: getattrlist result "
					    "len %d != %d\n",
					    buf[0], (5 * sizeof (uint32_t)));
					ret = FSUR_IO_FAIL;
					break;
				}

				/*
				 * getattr results are big-endian uint32_t
				 * and need to be swapped to host.
				 * Verified by reading UUID from mounted HFS
				 * via getattrlist and validating result.
				 */
				buf[1] = OSSwapBigToHostInt32(buf[1]);
				buf[2] = OSSwapBigToHostInt32(buf[2]);
				buf[3] = OSSwapBigToHostInt32(buf[3]);
				buf[4] = OSSwapBigToHostInt32(buf[4]);

				/*
				 * Validate UUID version 3
				 * (namespace variant w/MD5)
				 * We need to check a few bits:
				 * xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
				 * [uint32]-[ uint32]-[ uint32][uint32]
				 * M should be 0x3xxx to indicate version 3
				 * N should be 0x8xxx, 0x9xxx, 0xaxxx, or 0xbxxx
				 */
				if (buf[2] !=
				    ((buf[2] & 0xFFFF0FFF) | 0x00003000)) {
					printf("FATAL: missing v3 in UUID\n");
					ret = FSUR_IO_FAIL;
				}
				if (buf[3] !=
				    ((buf[3] & 0x3FFFFFFF) | 0x80000000)) {
					printf("FATAL: missing variant bits\n");
					ret = FSUR_IO_FAIL;
				}
				if (ret == FSUR_IO_FAIL)
					break;

				/*
				 * As char (reverse)
				 * result_uuid[6]=(result_uuid[6]&0x0F) | 0x30;
				 * result_uuid[8]=(result_uuid[8]&0x3F) | 0x80;
				 */
				printf("uuid: %08X-%04X-%04X-%04X-%04X%08X\n",
				    buf[1], (buf[2]&0xffff0000)>>16,
				    buf[2]&0x0000ffff,
				    (buf[3]&0xffff0000)>>16, buf[3]&0x0000ffff,
				    buf[4]);
				/* Print all caps to please DiskArbitration */

				/* Print UUID string (no newline) to stdout */
				fprintf(stdout, "%08X-%04X-%04X-%04X-%04X%08X",
				    buf[1], (buf[2]&0xffff0000)>>16,
				    buf[2]&0x0000ffff,
				    (buf[3]&0xffff0000)>>16, buf[3]&0x0000ffff,
				    buf[4]);
				ret = FSUR_IO_SUCCESS;

				break;
			}
			break;
		}

		case FSUC_SETUUID:
			/* Set a UUID */
			printf("FSUC_SETUUID\n");
			ret = FSUR_INVAL;
			break;
		case FSUC_MOUNT:
			/* Reject automount */
			printf("FSUC_MOUNT\n");
			ret = FSUR_IO_FAIL;
			break;
		case FSUC_UNMOUNT:
			/* Reject unmount */
			printf("FSUC_UNMOUNT\n");
			ret = FSUR_IO_FAIL;
			break;
		default:
			printf("unrecognized command %c\n", what);
			ret = FSUR_INVAL;
			usage();
	}
out:
	if (pool_name)
		kmem_free(pool_name, len);
	printf("Clean exit: %d (%d)\n", getpid(), ret);
	closelog();
	exit(ret);

	return (ret);	/* ...and make main fit the ANSI spec. */
}
