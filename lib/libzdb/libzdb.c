#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <openssl/evp.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_sa.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_bookmark.h>
#include <sys/dbuf.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/dmu_send.h>
#include <sys/dmu_traverse.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/zfs_fuid.h>
#include <sys/arc.h>
#include <sys/arc_impl.h>
#include <sys/ddt.h>
#include <sys/zfeature.h>
#include <sys/abd.h>
#include <sys/blkptr.h>
#include <sys/dsl_crypt.h>
#include <sys/dsl_scan.h>
#include <sys/btree.h>
#include <sys/brt.h>
#include <sys/brt_impl.h>
#include <zfs_comutil.h>
#include <sys/zstd/zstd.h>

#include <libnvpair.h>
#include <libzutil.h>

#include <libzdb.h>

const char *
zdb_ot_name(dmu_object_type_t type)
{
	if (type < DMU_OT_NUMTYPES)
		return (dmu_ot[type].ot_name);
	else if ((type & DMU_OT_NEWTYPE) &&
	    ((type & DMU_OT_BYTESWAP_MASK) < DMU_BSWAP_NUMFUNCS))
		return (dmu_ot_byteswap[type & DMU_OT_BYTESWAP_MASK].ob_name);
	else
		return ("UNKNOWN");
}

int
livelist_compare(const void *larg, const void *rarg)
{
	const blkptr_t *l = larg;
	const blkptr_t *r = rarg;

	/* Sort them according to dva[0] */
	uint64_t l_dva0_vdev, r_dva0_vdev;
	l_dva0_vdev = DVA_GET_VDEV(&l->blk_dva[0]);
	r_dva0_vdev = DVA_GET_VDEV(&r->blk_dva[0]);
	if (l_dva0_vdev < r_dva0_vdev)
		return (-1);
	else if (l_dva0_vdev > r_dva0_vdev)
		return (+1);

	/* if vdevs are equal, sort by offsets. */
	uint64_t l_dva0_offset;
	uint64_t r_dva0_offset;
	l_dva0_offset = DVA_GET_OFFSET(&l->blk_dva[0]);
	r_dva0_offset = DVA_GET_OFFSET(&r->blk_dva[0]);
	if (l_dva0_offset < r_dva0_offset) {
		return (-1);
	} else if (l_dva0_offset > r_dva0_offset) {
		return (+1);
	}

	/*
	 * Since we're storing blkptrs without cancelling FREE/ALLOC pairs,
	 * it's possible the offsets are equal. In that case, sort by txg
	 */
	if (l->blk_birth < r->blk_birth) {
		return (-1);
	} else if (l->blk_birth > r->blk_birth) {
		return (+1);
	}
	return (0);
}
