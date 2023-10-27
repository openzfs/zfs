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
 * Copyright (c) 2015, Evan Susarret.  All rights reserved.
 * Copyright (c) 2017, Jorgen Lundman.  All rights reserved.
 */

#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <libkern/libkern.h>
#include <sys/ZFSDatasetScheme.h>
#include <sys/ZFSDatasetProxy.h>
#include <sys/ZFSDataset.h>
#include <sys/ZFSPool.h>
#include <sys/spa_impl.h>
#include <IOKit/storage/IOBlockStorageDriver.h>

static ZFSDatasetScheme *
zfs_osx_proxy_scheme_by_osname(const char *osname)
{
	ZFSDatasetScheme *scheme = NULL;
	OSDictionary *matching;
	OSObject *object;
	OSString *str;
	OSIterator *iter;
	char *pool_name, *slash;
	size_t len;

	slash = strchr(osname, '/');
	if (slash) {
		len = (slash - osname) + 1;
	} else {
		len = strlen(osname) + 1;
	}

	pool_name = (char *)kmem_alloc(len, KM_SLEEP);
	if (!pool_name) {
		dprintf("string alloc failed");
		return (NULL);
	}
	snprintf(pool_name, len, "%s", osname);
	dprintf("pool_name [%s] from %s", pool_name, osname);

	matching = IOService::serviceMatching(kZFSDatasetSchemeClass);
	if (!matching) {
		dprintf("couldn't get match dict");
		kmem_free(pool_name, len);
		return (NULL);
	}

	/* Add the pool name for exact match */
	str = OSString::withCString(pool_name);
	matching->setObject(kZFSPoolNameKey, str);
	OSSafeReleaseNULL(str);

	object = IOService::copyMatchingService(matching);

	if (object && (scheme = OSDynamicCast(ZFSDatasetScheme,
	    object)) == NULL) {
		object->release();
	}
	object = NULL;

	if (scheme && ((str = OSDynamicCast(OSString,
	    scheme->getProperty(kZFSPoolNameKey))) == NULL ||
	    str->isEqualTo(pool_name) == false)) {
		scheme->release();
		scheme = NULL;
	}

	if (!scheme) {
		int i;
		for (i = 0; i < 12; i++) { // up to 6s
			iter = IOService::getMatchingServices(matching);
			if (iter) break;
			IOSleep(500);
		}

		if (i) dprintf("%s: tried %d times\n", __func__, i);

		if (!iter) {
			dprintf("couldn't get iterator");
			kmem_free(pool_name, len);
			OSSafeReleaseNULL(matching);
			return (NULL);
		}

		while ((object = iter->getNextObject())) {
			if (iter->isValid() == false) {
				iter->reset();
				continue;
			}
			scheme = OSDynamicCast(ZFSDatasetScheme, object);
			if (!scheme) continue;

			object = scheme->getProperty(kZFSPoolNameKey,
			    gIOServicePlane, kIORegistryIterateParents |
			    kIORegistryIterateRecursively);
			if (!object) continue;

			str = OSDynamicCast(OSString, object);
			if (!str) continue;

			if (str->isEqualTo(pool_name)) break;

			str = NULL;
			object = NULL;
			scheme = NULL;
		}

		if (scheme) scheme->retain();
		OSSafeReleaseNULL(iter);
	}
	OSSafeReleaseNULL(matching);
	kmem_free(pool_name, len);
	pool_name = 0;

	if (scheme == NULL) {
		dprintf("no matching pool proxy");
	}
	return (scheme);

#if 0
	spa_t *spa;
	ZFSPool *pool = 0;

	if (!osname || osname[0] == '\0') {
		dprintf("missing dataset argument");
		return (EINVAL);
	}

	/* Lookup the pool spa */
	mutex_enter(&spa_namespace_lock);
	spa = spa_lookup(osname);
	if (spa && spa->spa_iokit_proxy) {
		pool = spa->spa_iokit_proxy->proxy;
		if (pool) pool->retain();
	}
	mutex_exit(&spa_namespace_lock);

	/* Need a pool proxy to attach to */
	if (!pool) {
		dprintf("couldn't get pool proxy");
		return (EINVAL);
	}
	return (0);
#endif
}

/*
 * Get the proxy device by matching a property name and value.
 *
 * Inputs:
 * property: const char string.
 * value: const char string.
 *
 * Return:
 * Pointer to proxy on success, NULL on error or missing.
 */
static ZFSDataset *
zfs_osx_proxy_lookup(const char *property, OSObject *value)
{
	OSIterator *iter = NULL;
	OSDictionary *matching = NULL;
	OSObject *next = NULL, *prop = NULL;
	ZFSDataset *dataset = NULL;

	/* Validate arguments */
	if (!property || !value || property[0] == '\0') {
		dprintf("invalid argument");
		return (NULL);
	}

	/*
	 * Create the matching dictionary for class.
	 * Add property and value to match dict.
	 */
	matching = IOService::serviceMatching(kZFSDatasetClassKey);
	if ((matching) == NULL ||
	    (matching->setObject(property, value) == false)) {
		dprintf("match dictionary create failed");
		OSSafeReleaseNULL(matching);
		return (NULL);
	}

	/* Try to copy if there is only one match */
	next = IOService::copyMatchingService(matching);
	if (next != NULL && ((dataset = OSDynamicCast(ZFSDataset,
	    next)) != NULL) &&
	    (prop = dataset->getProperty(property)) != NULL &&
	    (prop->isEqualTo(value))) {
		dprintf("quick matched dataset");
		OSSafeReleaseNULL(matching);
		/* Leave retain taken by copyMatching */
		return (dataset);
	}
	/* Unretained references */
	prop = NULL;
	dataset = NULL;
	/* If set, it was retained by copyMatchingService */
	OSSafeReleaseNULL(next);

	iter = IOService::getMatchingServices(matching);
	OSSafeReleaseNULL(matching);
	if (iter == NULL) {
		dprintf("iterator failed");
		return (NULL);
	}

	while ((next = iter->getNextObject())) {
		dataset = OSDynamicCast(ZFSDataset, next);
		if (!dataset) continue;

		if ((prop = dataset->getProperty(property)) == NULL) {
			dataset = NULL;
			continue;
		}

		if (prop->isEqualTo(value)) {
			/* Take a reference on the match */
			dprintf("found match");
			dataset->retain();
			prop = NULL;
			break;
		}

		prop = NULL;
		dataset = NULL;
	}
	/* Release iterator */
	OSSafeReleaseNULL(iter);

	/* Leave retain */
	return (dataset);
#if 0
	/*
	 * Copy (first) matching service.
	 * Cast service to proxy class.
	 */
	if ((service = IOService::copyMatchingService(matching)) == NULL ||
	    (dataset = OSDynamicCast(ZFSDataset, service)) == NULL) {
		dprintf("matching failed");
		OSSafeReleaseNULL(service);
		return (NULL);
	}

	/* Leave retain from copyMatchingService */
	return (dataset);
#endif
}

/*
 * Get the proxy device for a given dataset name.
 *
 * Input:
 * osname: dataset name e.g. pool/dataset
 *
 * Return:
 * Valid ZFSDataset service, or NULL on error or missing.
 */
ZFSDataset *
zfs_osx_proxy_get(const char *osname)
{
	ZFSDataset *dataset;
	OSString *osstr;

	/* Validate arguments, osname is limited to MAXNAMELEN */
	if (!osname || osname[0] == '\0' || osname[0] == '/' ||
	    strnlen(osname, MAXNAMELEN+1) == (MAXNAMELEN+1)) {
		dprintf("invalid argument");
		return (NULL);
	}

	osstr = OSString::withCString(osname);
	if (!osstr) {
		dprintf("string alloc failed");
		return (NULL);
	}

	dataset = zfs_osx_proxy_lookup(kZFSDatasetNameKey, osstr);
	OSSafeReleaseNULL(osstr);

	if (!dataset) {
		dprintf("lookup failed");
		return (NULL);
	}

	return (dataset);
}

/*
 * Get the proxy device for a given a device name or path.
 *
 * Input:
 * devpath: BSD name as const char* string, e.g. "/dev/diskN" or "diskN"
 *  must be null-terminated
 *
 * Return:
 * Valid ZFSDataset service, or NULL on error or missing.
 */
static ZFSDataset *
zfs_osx_proxy_from_devpath(const char *devpath)
{
	/* XXX No need to init, will be assigned */
	ZFSDataset *dataset;
	OSString *bsdstr;
	const char *bsdname;

	/* Validate arguments, devpath is limited to MAXPATHLEN */
	if (!devpath || devpath[0] == '\0' ||
	    strnlen(devpath, MAXPATHLEN+1) == (MAXPATHLEN+1)) {
		dprintf("invalid argument");
		return (NULL);
	}

	/* If we have a path, remove prefix */
	if (strncmp(devpath, "/dev/", 5) == 0) {
		bsdname = devpath + 5;
	} else {
		bsdname = devpath;
	}

	/* Make sure we have (at least) "diskN" at this point */
	if (strncmp(bsdname, "disk", 4) != 0 || bsdname[4] == '\0') {
		dprintf("invalid bsdname %s from %s", bsdname, devpath);
		return (NULL);
	}

	bsdstr = OSString::withCString(bsdname);
	if (!bsdstr) {
		dprintf("string alloc failed");
		return (NULL);
	}

	dataset = zfs_osx_proxy_lookup(kIOBSDNameKey, bsdstr);
	OSSafeReleaseNULL(bsdstr);

	if (!dataset) {
		dprintf("lookup with %s failed", bsdname);
		return (NULL);
	}

	return (dataset);
}

/*
 * Given a dataset, get the desired property and write its
 * value to the caller-supplied buffer.
 *
 * Inputs:
 * dataset: valid ZFSDataset object, should be retained by
 * caller.
 * property: const char* of the desired property name key.
 * value: char* buffer which should be at least 'len' bytes.
 * len: length of value buffer.
 *
 * Return:
 * 0 on success, positive int on error.
 */
static int
zfs_osx_proxy_get_prop_string(ZFSDataset *dataset,
    const char *property, char *value, int len)
{
	OSObject *obj;
	OSString *valueString;

	/* Validate arguments */
	if (!dataset || !property || !value || len == 0) {
		dprintf("invalid argument");
		return (EINVAL);
	}

	/* Lock proxy while getting property */
	dataset->lockForArbitration();
	obj = dataset->copyProperty(property);
	dataset->unlockForArbitration();

	if (!obj) {
		dprintf("no property %s", property);
		return (ENXIO);
	}

	valueString = OSDynamicCast(OSString, obj);
	/* Validate property value */
	if (!valueString) {
		dprintf("couldn't cast value for %s", property);
		OSSafeReleaseNULL(obj);
		return (ENXIO);
	}

	/* Write up to len bytes */
	snprintf(value, len, "%s", valueString->getCStringNoCopy());

	/* Release string and proxy */
	valueString = 0;
	OSSafeReleaseNULL(obj);

	return (0);
}

extern "C" {

/*
 * Given a ZFS dataset name, get the proxy device and write the
 * BSD Name to the caller-supplied buffer.
 *
 * Inputs:
 * osname: dataset name as char* string, e.g. "pool/dataset"
 *  must be null-terminated
 * bsdname: char* string buffer where bsdname will be written
 * len: length of bsdname buffer
 *
 * Return:
 * 0 on success, positive int errno on failure.
 */
int
zfs_osx_proxy_get_bsdname(const char *osname,
    char *bsdname, int len)
{
	/* XXX No need to init, will be assigned */
	ZFSDataset *dataset;
	int ret;

	/* Validate arguments */
	if (!osname || !bsdname || len == 0) {
		dprintf("invalid argument");
		return (EINVAL);
	}

	/* Get dataset proxy (takes a retain) */
	dataset = zfs_osx_proxy_get(osname);
	if (!dataset) {
		dprintf("no proxy matching %s", osname);
		return (ENOENT);
	}

	/* Get BSD name property and write to bsdname buffer */
	ret = zfs_osx_proxy_get_prop_string(dataset,
	    kIOBSDNameKey, bsdname, len);
	OSSafeReleaseNULL(dataset);

	if (ret != 0) {
		dprintf("ret %d", ret);
	}

	return (ret);
}

/*
 * Given a device name or path, get the proxy device and write the
 * ZFS Dataset name to the caller-supplied buffer.
 *
 * Inputs:
 * devpath: BSD name as const char* string, e.g. "/dev/diskN" or "diskN"
 *  must be null-terminated
 * osname: char* string buffer where osname will be written
 * len: length of osname buffer
 *
 * Return:
 * 0 on success, positive int errno on failure.
 */
int
zfs_osx_proxy_get_osname(const char *devpath, char *osname, int len)
{
	/* XXX No need to init, will be assigned */
	ZFSDataset *dataset;
	int ret;

	/* Validate arguments */
	if (!devpath || !osname || len == 0) {
		dprintf("invalid argument");
		return (EINVAL);
	}

	/* Get dataset proxy (takes a retain) */
	dataset = zfs_osx_proxy_from_devpath(devpath);
	if (!dataset) {
		dprintf("no proxy matching %s", devpath);
		return (ENOENT);
	}

	/* Get dataset name property and write to osname buffer */
	ret = zfs_osx_proxy_get_prop_string(dataset,
	    kZFSDatasetNameKey, osname, len);
	OSSafeReleaseNULL(dataset);

	if (ret != 0) {
		dprintf("ret %d", ret);
	}

	return (ret);
}

/*
 * Check if a dataset has a proxy device.
 *
 * Input:
 * osname: dataset name e.g. pool/dataset
 *
 * Return:
 * 1 if exists, 0 on error or missing.
 */
int
zfs_osx_proxy_exists(const char *osname)
{
	ZFSDataset *dataset;

	/* Get dataset proxy (takes a retain) */
	if ((dataset = zfs_osx_proxy_get(osname)) != NULL) {
		OSSafeReleaseNULL(dataset);
		return (1);
	}

	return (0);
}

/*
 * Remove the proxy device for a given dataset name.
 *
 * Input:
 * osname: dataset name e.g. pool/dataset
 */
void
zfs_osx_proxy_remove(const char *osname)
{
	ZFSDataset *dataset;
	ZFSDatasetScheme *provider;

	/* Get dataset proxy (takes a retain) */
	dataset = zfs_osx_proxy_get(osname);
	if (dataset == NULL) {
		dprintf("couldn't get dataset");
		return;
	}
#if 0
	/* Terminate and release retain */
	dataset->terminate(kIOServiceSynchronous | kIOServiceRequired);
	OSSafeReleaseNULL(dataset);
#endif
	provider = OSDynamicCast(ZFSDatasetScheme,
	    dataset->getProvider());

	OSSafeReleaseNULL(dataset);

	if (!provider) {
		dprintf("invalid provider");
		return;
	}

	dprintf("removing %s", osname);
	provider->removeDataset(osname, /* force */ true);
}

/*
 * Create a proxy device for a given dataset name, unless one exists.
 *
 * Input:
 * osname: dataset name e.g. pool/dataset
 *
 * Return:
 * 0 on success, or positive int on error.
 */
int
zfs_osx_proxy_create(const char *osname)
{
	ZFSDatasetScheme *provider = NULL;

	if (!osname || osname[0] == '\0') {
		dprintf("missing dataset argument");
		return (EINVAL);
	}

	provider = zfs_osx_proxy_scheme_by_osname(osname);
	if (provider == NULL) {
		dprintf("can't get pool proxy");
		return (ENOENT);
	}

	if (provider->addDataset(osname) == false) {
		dprintf("couldn't add dataset");
		provider->release();
		return (ENXIO);
	}

	provider->release();
	return (0);
}

} /* extern "C" */

static SInt32
orderHoles(const OSMetaClassBase *obj1, const OSMetaClassBase *obj2,
    __unused void *context)
{
	OSNumber *num1, *num2;

	if (obj1 == NULL ||
	    (num1 = OSDynamicCast(OSNumber, obj1)) == NULL) {
		/* Push invalid OSNumbers to end of list */
		return (-1);
	}
	if (obj2 == NULL ||
	    (num2 = OSDynamicCast(OSNumber, obj2)) == NULL) {
		/* If both are non-OSNumber, same ordering */
		if (num1 == NULL)
			return (0);
		/* If num1 is a valid OSNumber, push num2 to end */
		return (1);
	}

	/*
	 * A comparison result of the object:
	 * <ul>
	 *   <li>a negative value if obj2 should precede obj1,</li>
	 *   <li>a positive value if obj1 should precede obj2,</li>
	 *   <li>and 0 if obj1 and obj2 have an equivalent ordering.</li>
	 * </ul>
	 */
	if (num1->isEqualTo(num2))
		return (0);

	if (num1->unsigned32BitValue() < num2->unsigned32BitValue()) {
		return (1);
	} else {
		return (-1);
	}
}

OSDefineMetaClassAndStructors(ZFSDatasetScheme, IOPartitionScheme);

void
ZFSDatasetScheme::free()
{
	OSSafeReleaseNULL(_datasets);
	OSSafeReleaseNULL(_holes);
	_max_id = 0;

	IOPartitionScheme::free();
}

bool
ZFSDatasetScheme::init(OSDictionary *properties)
{
	_datasets = OSSet::withCapacity(1);
	_holes = OSOrderedSet::withCapacity(1, orderHoles);
	_max_id = 0;

	if (!_datasets || !_holes) {
		dprintf("OSSet allocation failed");
		OSSafeReleaseNULL(_datasets);
		OSSafeReleaseNULL(_holes);
		return (false);
	}

	OSDictionary *newProps = NULL;
	if (properties) newProps = OSDictionary::withDictionary(properties);
	if (!newProps) newProps = OSDictionary::withCapacity(2);
	OSString *str;
	str = OSString::withCString("IOGUIDPartitionScheme");
	newProps->setObject("IOClass", str);
	OSSafeReleaseNULL(str);
	str = OSString::withCString("GUID_partition_scheme");
	newProps->setObject("Content Mask", str);
	OSSafeReleaseNULL(str);

	if (IOPartitionScheme::init(newProps) == false) {
		dprintf("IOPartitionScheme init failed");
		OSSafeReleaseNULL(newProps);
		OSSafeReleaseNULL(_datasets);
		OSSafeReleaseNULL(_holes);
		return (false);
	}
	OSSafeReleaseNULL(newProps);

	return (true);
}

bool
ZFSDatasetScheme::start(IOService *provider)
{
	OSObject *pool_name;

	if (IOPartitionScheme::start(provider) == false) {
		dprintf("IOPartitionScheme start failed");
		return (false);
	}

	pool_name = getProperty(kZFSPoolNameKey,
	    gIOServicePlane, kIORegistryIterateRecursively|
	    kIORegistryIterateParents);
	if (pool_name) {
		setProperty(kZFSPoolNameKey, pool_name);
	}

	// registerService(kIOServiceAsynchronous);
	registerService(kIOServiceSynchronous);

	return (true);
}

IOService *
ZFSDatasetScheme::probe(IOService *provider, SInt32 *score)
{
	OSObject *property;
	IOService *parent;

	/* First ask IOPartitionScheme to probe */
	if (IOPartitionScheme::probe(provider, score) == 0) {
		dprintf("IOPartitionScheme probe failed");
		return (0);
	}

	/* Check for ZFS Pool Name first */
	property = getProperty(kZFSPoolNameKey, gIOServicePlane,
	    kIORegistryIterateRecursively|kIORegistryIterateParents);
	if (!property) {
		dprintf("no pool name");
		return (0);
	}

	/* Make sure we have a target, and valid provider below */
	if (provider == NULL ||
	    OSDynamicCast(IOMedia, provider) == NULL ||
	    (parent = provider->getProvider()) == NULL) {
		dprintf("invalid provider");
		return (0);
	}

	/* Make sure provider is driver, and has valid provider below */
	if (OSDynamicCast(IOBlockStorageDriver, parent) == NULL ||
	    (parent = parent->getProvider()) == NULL) {
		dprintf("invalid parent");
		return (0);
	}

	/* Make sure the parent provider is a proxy */
	if (OSDynamicCast(ZFSDatasetProxy, parent) == NULL) {
		dprintf("invalid grandparent");
		return (0);
	}

	/* Successful match */
	dprintf("Match");
	// *score = 5000;
	return (this);
}

uint32_t
ZFSDatasetScheme::getNextPartitionID()
{
	uint32_t ret_id = 0ULL;

	/* Try to lock, unless service is terminated */
	if (lockForArbitration(false) == false) {
		dprintf("service is terminated");
		return (0ULL);
	}

	/* If the partiton list is sparse (has holes) */
	if (_holes->getCount() != 0) {
		OSNumber *id_num = OSDynamicCast(OSNumber,
		    _holes->getFirstObject());

		/* Just in case the list is invalid */
#ifdef DEBUG
		if (!id_num) panic("invalid hole list");
#endif

		if (id_num) {
			id_num->retain();
			_holes->removeObject(id_num);
			ret_id = id_num->unsigned32BitValue();
			OSSafeReleaseNULL(id_num);
			goto out;
		}
	}

	/* If no holes were found, just get next id */
	ret_id = (_max_id += 1);

out:
	unlockForArbitration();
	return (ret_id);
}

void ZFSDatasetScheme::returnPartitionID(uint32_t part_id)
{
	OSNumber *id_num = OSNumber::withNumber(part_id, 32);

	if (!id_num) dprintf("alloc failed");
	/* XXX Continue and try to decrement max_id if possible */

	if (lockForArbitration(false) == false) {
		dprintf("service is terminated");
		OSSafeReleaseNULL(id_num);
		return;
	}

	/* Decrementing highest part id */
	if (part_id == _max_id) {
		/* First, decrement max */
		_max_id--;
		/* no longer needed */
		OSSafeReleaseNULL(id_num);

		/* Now iterate down the hole list */
		while ((id_num = OSDynamicCast(OSNumber,
		    _holes->getLastObject()))) {
			/* Only need to remove consecutive matches */
			if (id_num->unsigned32BitValue() != (_max_id)) {
				break;
			}

			/* Remove this num from hole list */
			id_num->retain();
			_holes->removeObject(id_num);
			OSSafeReleaseNULL(id_num);
			/* Decrement max */
			_max_id--;
		}
	/* Creating a new 'hole' in the ID namespace */
	} else {
		/* Better have been able to allocate OSNum */
		if (!id_num) {
			unlockForArbitration();
#ifdef DEBUG
			panic("ZFSDatasetScheme %s failed to return partID",
			    __func__);
#endif
			return;
		}

		/*
		 * OSOrderedSet only enforces ordering when
		 * using setObject(anObject) interface.
		 * Therefore _holes must not use setFirstObject,
		 * setLastObject, setObject(index, anObject)
		 */

		/* Add a new OSNum to hole list */
		_holes->setObject(id_num);
		OSSafeReleaseNULL(id_num);
	}

	unlockForArbitration();
}

bool
ZFSDatasetScheme::addDataset(const char *osname)
{
	ZFSDataset *dataset;
	OSObject *obj;
	OSNumber *sizeNum;
	char location[24];
	uint64_t size;
	uint32_t part_id;

	obj = copyProperty(kZFSPoolSizeKey, gIOServicePlane,
	    kIORegistryIterateRecursively|kIORegistryIterateParents);
	if (!obj) {
		dprintf("missing pool size");
		return (false);
	}
	sizeNum = OSDynamicCast(OSNumber, obj);
	if (!sizeNum) {
		dprintf("invalid pool size");
		return (false);
	}
	size = sizeNum->unsigned64BitValue();
	sizeNum = 0;
	OSSafeReleaseNULL(obj);

	part_id = getNextPartitionID();
	/* Only using non-zero partition ids */
	if (part_id == 0) {
		dprintf("invalid partition ID");
		return (false);
	}
	snprintf(location, sizeof (location), "%u", part_id);

#if 0
	OSString *locationStr;
	locationStr = OSString::withCString(location);
	if (!locationStr) {
		dprintf("location string alloc failed");
		return (false);
	}
	OSSafeReleaseNULL(locationStr);
#endif

	dataset = ZFSDataset::withDatasetNameAndSize(osname, size);
	if (!dataset) {
		dprintf("couldn't add %s", osname);
		return (false);
	}

	/* Set location in plane and partiton ID property */
	dataset->setLocation(location);
#ifdef kIOMediaBaseKey
	dataset->setProperty(kIOMediaBaseKey, 0ULL, 64);
#endif
	dataset->setProperty(kIOMediaPartitionIDKey, part_id, 32);

	// This sets the "diskutil list -> TYPE" field
	dataset->setProperty("Content", "ZFS Dataset");
	// This matches with Info.plist, so it calls zfs.util for NAME
	dataset->setProperty("Content Hint",
	    "6A898CC3-1DD2-11B2-99A6-080020736631");

	if (dataset->attach(this) == false) {
		dprintf("attach failed");
		OSSafeReleaseNULL(dataset);
		return (false);
	}

	if (dataset->start(this) == false) {
		dprintf("start failed");
		dataset->detach(this);
		OSSafeReleaseNULL(dataset);
		return (false);
	}

	/* Protect the OSSet by taking IOService lock */
	lockForArbitration();
	_datasets->setObject(dataset);
	unlockForArbitration();

	// dataset->registerService(kIOServiceAsynchronous);
	dataset->registerService(kIOServiceSynchronous);

	/* Adding to OSSet takes a retain */
	OSSafeReleaseNULL(dataset);

	return (true);
}

bool
ZFSDatasetScheme::removeDataset(const char *osname, bool force)
{
	OSCollectionIterator *iter;
	ZFSDataset *dataset = NULL;
	OSNumber *partNum;
	uint32_t part_id = 0;
	bool locked;

	if ((locked = lockForArbitration(false)) == false) {
		dprintf("couldn't lock terminated service");
	}

	iter = OSCollectionIterator::withCollection(_datasets);
	if (!iter) {
		dprintf("couldn't get dataset iterator");
		if (locked) unlockForArbitration();
		return (false);
	}

	while ((dataset = OSDynamicCast(ZFSDataset,
	    iter->getNextObject())) != NULL) {
		OSObject *property;
		OSString *str;

		property = dataset->getProperty(kZFSDatasetNameKey);
		if (!property) continue;

		str = OSDynamicCast(OSString, property);
		if (!str) continue;

		if (str->isEqualTo(osname)) {
			_datasets->removeObject(dataset);
			break;
		}
	}

	if (!dataset) {
		dprintf("couldn't get dataset");
		iter->release();
		if (locked) unlockForArbitration();
		return (false);
	}

	dataset->retain();
	iter->release();
	iter = 0;

	if (locked) unlockForArbitration();

	partNum = OSDynamicCast(OSNumber,
	    dataset->getProperty(kIOMediaPartitionIDKey));
	if (!partNum) {
		dprintf("couldn't get partition number");
	} else {
		part_id = partNum->unsigned32BitValue();
	}

	if (force) {
		dataset->terminate(kIOServiceSynchronous|
		    kIOServiceRequired);
	} else {
		dataset->terminate(kIOServiceSynchronous);
	}

	dataset->release();
	dataset = 0;

	/* Only return non-zero partition ids */
	if (part_id != 0) {
		dprintf("terminated partition %u", part_id);
		returnPartitionID(part_id);
	}

	return (true);
}

/* Compatibility shims */
void
ZFSDatasetScheme::read(IOService *client,
    UInt64		byteStart,
    IOMemoryDescriptor	*buffer,
    IOStorageAttributes	*attributes,
    IOStorageCompletion	*completion)
{
	IOStorage::complete(completion, kIOReturnError, 0);
}

void
ZFSDatasetScheme::write(IOService *client,
    UInt64		byteStart,
    IOMemoryDescriptor	*buffer,
    IOStorageAttributes	*attributes,
    IOStorageCompletion	*completion)
{
	IOStorage::complete(completion, kIOReturnError, 0);
}

#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
IOReturn
ZFSDatasetScheme::synchronize(IOService *client,
    UInt64			byteStart,
    UInt64			byteCount,
    IOStorageSynchronizeOptions	options)
#else
IOReturn
ZFSDatasetScheme::synchronizeCache(IOService *client)
#endif
{
	return (kIOReturnUnsupported);
}

IOReturn
ZFSDatasetScheme::unmap(IOService *client,
    IOStorageExtent		*extents,
    UInt32			extentsCount,
#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	    IOStorageUnmapOptions	options)
#else
	    UInt32	options)
#endif
{
	return (kIOReturnUnsupported);
}

bool
ZFSDatasetScheme::lockPhysicalExtents(IOService *client)
{
	return (false);
}

IOStorage *
ZFSDatasetScheme::copyPhysicalExtent(IOService *client,
    UInt64 *    byteStart,
    UInt64 *    byteCount)
{
	return (NULL);
}

void
ZFSDatasetScheme::unlockPhysicalExtents(IOService *client)
{
}

#if defined(MAC_OS_X_VERSION_10_10) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_10)
IOReturn
ZFSDatasetScheme::setPriority(IOService *client,
    IOStorageExtent	*extents,
    UInt32		extentsCount,
    IOStoragePriority	priority)
{
	return (kIOReturnUnsupported);
}
#endif
