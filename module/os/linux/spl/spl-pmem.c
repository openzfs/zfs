#include <sys/pmem_spl.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/kmem.h>

#include <linux/dax.h>

struct spl_dax_device {
	struct dax_device *spl_dax_dax;
	void *spl_dax_base;
	u64 spl_dax_len;
};
EXPORT_SYMBOL(spl_open_dax_device);

int spl_open_dax_device(struct block_device *bdev, uint64_t expect_capacity,
    struct spl_dax_device **out)
{
	ASSERT3P(bdev, !=, NULL);
	ASSERT3P(out, !=, NULL);
	struct dax_device *d;
	if (bdev_dax_supported(bdev, PAGE_SIZE)) {
		d = fs_dax_get_by_bdev(bdev);
		if (d == NULL) {
			return (ENOTSUP);
		}
		/* fallthrough */
	} else {
		return (ENOTSUP);
	}

	void *base = NULL;
	pfn_t base_pfn;
	long avail;
	avail = dax_direct_access(d, 0, LONG_MAX/PAGE_SIZE, &base, &base_pfn);
	if (avail < 0) {
		/*
		 * Pages is negative errno, but it's Linux-specific errno
		 * so we don't want to pass it up verbatim.
		 */
		switch (-avail) {
		case EOPNOTSUPP:
		case ENXIO:
		case ERANGE:
			return (-avail);
		default:
			return (ENOTSUP);
		}
	}

	if (avail * PAGE_SIZE != expect_capacity) {
		return (ENXIO);
	}

	struct spl_dax_device *ret;
	ret = kmem_alloc(sizeof (**out), KM_SLEEP);
	ret->spl_dax_dax = d;
	ret->spl_dax_base = base;
	ret->spl_dax_len = avail * PAGE_SIZE;
	*out = ret;

	return (0);
}

void
spl_close_dax_device(struct spl_dax_device *dev)
{
	kmem_free(dev, sizeof (*dev));
}
EXPORT_SYMBOL(spl_close_dax_device);

void spl_dax_device_base_len(struct spl_dax_device *dev, void **base,
    uint64_t *len)
{
	*base = dev->spl_dax_base;
	*len = dev->spl_dax_len;
}
EXPORT_SYMBOL(spl_dax_device_base_len);

int
spl_memcpy_mc(void *dst, const void *src_checked, size_t size)
{
	/* Linux 5.8 and forward: copy_mc_to_kernel()
	 * https://patchwork.kernel.org/project/linux-nvdimm/patch/160195561680.2163339.11574962055305783722.stgit@dwillia2-desk3.amr.corp.intel.com/
	 * (ec6347bb43395cb92126788a1a5b25302543f815)
	 *
	 * Earlier: memcpy_mcsafe()
	 *
	 * AFAICT they have the same semantics.
	 */

	/* XXX Compat with earlier Linux versions */
	return (copy_mc_to_kernel(dst, src_checked, size));
}
EXPORT_SYMBOL(spl_memcpy_mc);

void
spl_memcpy_flushcache(void *dst, const void *src, size_t size)
{
	return (memcpy_flushcache(dst, src, size));
}
EXPORT_SYMBOL(spl_memcpy_flushcache);
