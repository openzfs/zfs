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
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright 2017 Jason King
 * Copyright (c) 2017, Intel Corporation.
 */

#include <assert.h>
#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/spa.h>
#include <sys/fs/zfs.h>
#include <sys/refcount.h>
#include <sys/zfs_ioctl.h>
#include <dlfcn.h>
#include <libzutil.h>

/*
 * Routines needed by more than one client of libzpool.
 */

static void
show_vdev_stats(const char *desc, const char *ctype, nvlist_t *nv, int indent)
{
	vdev_stat_t *vs;
	vdev_stat_t *v0 = { 0 };
	uint64_t sec;
	uint64_t is_log = 0;
	nvlist_t **child;
	uint_t c, children;
	char used[6], avail[6];
	char rops[6], wops[6], rbytes[6], wbytes[6], rerr[6], werr[6], cerr[6];

	v0 = umem_zalloc(sizeof (*v0), UMEM_NOFAIL);

	if (indent == 0 && desc != NULL) {
		(void) printf("                           "
		    " capacity   operations   bandwidth  ---- errors ----\n");
		(void) printf("description                "
		    "used avail  read write  read write  read write cksum\n");
	}

	if (desc != NULL) {
		char *suffix = "", *bias = NULL;
		char bias_suffix[32];

		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_LOG, &is_log);
		(void) nvlist_lookup_string(nv, ZPOOL_CONFIG_ALLOCATION_BIAS,
		    &bias);
		if (nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t **)&vs, &c) != 0)
			vs = v0;

		if (bias != NULL) {
			(void) snprintf(bias_suffix, sizeof (bias_suffix),
			    " (%s)", bias);
			suffix = bias_suffix;
		} else if (is_log) {
			suffix = " (log)";
		}

		sec = MAX(1, vs->vs_timestamp / NANOSEC);

		nicenum(vs->vs_alloc, used, sizeof (used));
		nicenum(vs->vs_space - vs->vs_alloc, avail, sizeof (avail));
		nicenum(vs->vs_ops[ZIO_TYPE_READ] / sec, rops, sizeof (rops));
		nicenum(vs->vs_ops[ZIO_TYPE_WRITE] / sec, wops, sizeof (wops));
		nicenum(vs->vs_bytes[ZIO_TYPE_READ] / sec, rbytes,
		    sizeof (rbytes));
		nicenum(vs->vs_bytes[ZIO_TYPE_WRITE] / sec, wbytes,
		    sizeof (wbytes));
		nicenum(vs->vs_read_errors, rerr, sizeof (rerr));
		nicenum(vs->vs_write_errors, werr, sizeof (werr));
		nicenum(vs->vs_checksum_errors, cerr, sizeof (cerr));

		(void) printf("%*s%s%*s%*s%*s %5s %5s %5s %5s %5s %5s %5s\n",
		    indent, "",
		    desc,
		    (int)(indent+strlen(desc)-25-(vs->vs_space ? 0 : 12)),
		    suffix,
		    vs->vs_space ? 6 : 0, vs->vs_space ? used : "",
		    vs->vs_space ? 6 : 0, vs->vs_space ? avail : "",
		    rops, wops, rbytes, wbytes, rerr, werr, cerr);
	}
	umem_free(v0, sizeof (*v0));

	if (nvlist_lookup_nvlist_array(nv, ctype, &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		nvlist_t *cnv = child[c];
		char *cname = NULL, *tname;
		uint64_t np;
		int len;
		if (nvlist_lookup_string(cnv, ZPOOL_CONFIG_PATH, &cname) &&
		    nvlist_lookup_string(cnv, ZPOOL_CONFIG_TYPE, &cname))
			cname = "<unknown>";
		len = strlen(cname) + 2;
		tname = umem_zalloc(len, UMEM_NOFAIL);
		(void) strlcpy(tname, cname, len);
		if (nvlist_lookup_uint64(cnv, ZPOOL_CONFIG_NPARITY, &np) == 0)
			tname[strlen(tname)] = '0' + np;
		show_vdev_stats(tname, ctype, cnv, indent + 2);
		umem_free(tname, len);
	}
}

void
show_pool_stats(spa_t *spa)
{
	nvlist_t *config, *nvroot;
	char *name;

	VERIFY(spa_get_stats(spa_name(spa), &config, NULL, 0) == 0);

	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	VERIFY(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);

	show_vdev_stats(name, ZPOOL_CONFIG_CHILDREN, nvroot, 0);
	show_vdev_stats(NULL, ZPOOL_CONFIG_L2CACHE, nvroot, 0);
	show_vdev_stats(NULL, ZPOOL_CONFIG_SPARES, nvroot, 0);

	nvlist_free(config);
}

/*
 * Sets given global variable in libzpool to given unsigned 32-bit value.
 * arg: "<variable>=<value>"
 */
int
set_global_var(char *arg)
{
	void *zpoolhdl;
	char *varname = arg, *varval;
	u_longlong_t val;

#ifndef _LITTLE_ENDIAN
	/*
	 * On big endian systems changing a 64-bit variable would set the high
	 * 32 bits instead of the low 32 bits, which could cause unexpected
	 * results.
	 */
	fprintf(stderr, "Setting global variables is only supported on "
	    "little-endian systems\n");
	return (ENOTSUP);
#endif
	if (arg != NULL && (varval = strchr(arg, '=')) != NULL) {
		*varval = '\0';
		varval++;
		val = strtoull(varval, NULL, 0);
		if (val > UINT32_MAX) {
			fprintf(stderr, "Value for global variable '%s' must "
			    "be a 32-bit unsigned integer\n", varname);
			return (EOVERFLOW);
		}
	} else {
		return (EINVAL);
	}

	zpoolhdl = dlopen("libzpool.so", RTLD_LAZY);
	if (zpoolhdl != NULL) {
		uint32_t *var;
		var = dlsym(zpoolhdl, varname);
		if (var == NULL) {
			fprintf(stderr, "Global variable '%s' does not exist "
			    "in libzpool.so\n", varname);
			return (EINVAL);
		}
		*var = (uint32_t)val;

		dlclose(zpoolhdl);
	} else {
		fprintf(stderr, "Failed to open libzpool.so to set global "
		    "variable\n");
		return (EIO);
	}

	return (0);
}

static nvlist_t *
refresh_config(void *unused, nvlist_t *tryconfig)
{
	return (spa_tryimport(tryconfig));
}

static int
pool_active(void *unused, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	zfs_cmd_t *zcp;
	nvlist_t *innvl;
	char *packed = NULL;
	size_t size = 0;
	int fd, ret;

	/*
	 * Use ZFS_IOC_POOL_SYNC to confirm if a pool is active
	 */

	fd = open(ZFS_DEV, O_RDWR);
	if (fd < 0)
		return (-1);

	zcp = umem_zalloc(sizeof (zfs_cmd_t), UMEM_NOFAIL);

	innvl = fnvlist_alloc();
	fnvlist_add_boolean_value(innvl, "force", B_FALSE);

	(void) strlcpy(zcp->zc_name, name, sizeof (zcp->zc_name));
	packed = fnvlist_pack(innvl, &size);
	zcp->zc_nvlist_src = (uint64_t)(uintptr_t)packed;
	zcp->zc_nvlist_src_size = size;

	ret = ioctl(fd, ZFS_IOC_POOL_SYNC, zcp);

	fnvlist_pack_free(packed, size);
	free((void *)(uintptr_t)zcp->zc_nvlist_dst);
	nvlist_free(innvl);
	umem_free(zcp, sizeof (zfs_cmd_t));

	(void) close(fd);

	*isactive = (ret == 0);

	return (0);
}

const pool_config_ops_t libzpool_config_ops = {
	.pco_refresh_config = refresh_config,
	.pco_pool_active = pool_active,
};
