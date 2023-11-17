/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

	ASSERT3U(spa_config_held(spa, SCL_ALL, RW_WRITER), ==, SCL_ALL);

	pad2 = abd_alloc_for_io(VDEV_PAD_SIZE, B_TRUE);
	abd_copy_from_buf(pad2, buf, size);
	abd_zero_off(pad2, size, VDEV_PAD_SIZE - size);

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

static void
vdev_child_done(zio_t *zio)
{
	zio_t *pio = zio->io_private;

	mutex_enter(&pio->io_lock);
	pio->io_error = zio_worst_error(pio->io_error, zio->io_error);
	mutex_exit(&pio->io_lock);
}

/*
 * Check if the reserved boot area is in-use.
 *
 * When booting FreeBSD with an MBR partition with ZFS, the zfsboot file
 * (which understands the ZFS file system) is written to the ZFS BOOT
 * reserve area (at offset 512K). We check for that here before attaching
 * a disk to raidz which would then corrupt this boot data.
 */
int
vdev_check_boot_reserve(spa_t *spa, vdev_t *childvd)
{
	ASSERT(childvd->vdev_ops->vdev_op_leaf);

	size_t size = SPA_MINBLOCKSIZE;
	abd_t *abd = abd_alloc_linear(size, B_FALSE);

	zio_t *pio = zio_root(spa, NULL, NULL, 0);
	/*
	 * Note: zio_vdev_child_io() adds VDEV_LABEL_START_SIZE to the offset
	 * to calculate the physical offset to write to.  Passing in a negative
	 * offset lets us access the boot area.
	 */
	zio_nowait(zio_vdev_child_io(pio, NULL, childvd,
	    VDEV_BOOT_OFFSET - VDEV_LABEL_START_SIZE, abd, size, ZIO_TYPE_READ,
	    ZIO_PRIORITY_ASYNC_READ, 0, vdev_child_done, pio));
	zio_wait(pio);

	unsigned char *buf = abd_to_buf(abd);

	/*
	 * The BTX server has a special header at the begining.
	 *
	 * btx_hdr:	.byte 0xeb		# Machine ID
	 *		.byte 0xe		# Header size
	 *		.ascii "BTX"		# Magic
	 *		.byte 0x1		# Major version
	 *		.byte 0x2		# Minor version
	 *		.byte BTX_FLAGS		# Flags
	 */
	if (buf[0] == 0xeb && buf[1] == 0x0e &&
	    buf[2] == 'B' && buf[3] == 'T' && buf[4] == 'X') {
		abd_free(abd);
		return (EBUSY);
	}

	abd_free(abd);
	return (0);
}
