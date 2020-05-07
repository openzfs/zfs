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

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/vdev.h>
#include <sys/vdev_os.h>
#include <sys/vdev_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/zio.h>
#include <sys/dsl_scan.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>

int
vdev_label_write_pad2(vdev_t *vd, const char *buf, size_t size)
{
	spa_t *spa = vd->vdev_spa;
	zio_t *zio;
	abd_t *pad2;
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL;
	int error;

	if (size > VDEV_PAD_SIZE)
		return (EINVAL);

	if (!vd->vdev_ops->vdev_op_leaf)
		return (ENODEV);
	if (vdev_is_dead(vd))
		return (ENXIO);

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	pad2 = abd_alloc_for_io(VDEV_PAD_SIZE, B_TRUE);
	abd_zero(pad2, VDEV_PAD_SIZE);
	abd_copy_from_buf(pad2, buf, size);

retry:
	zio = zio_root(spa, NULL, NULL, flags);
	vdev_label_write(zio, vd, 0, pad2,
	    offsetof(vdev_label_t, vl_be),
	    VDEV_PAD_SIZE, NULL, NULL, flags);
	error = zio_wait(zio);
	if (error != 0 && !(flags & ZIO_FLAG_TRYHARD)) {
		flags |= ZIO_FLAG_TRYHARD;
		goto retry;
	}

	abd_free(pad2);
	return (error);
}
