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
 * Copyright (c) 2016, Evan Susarret.  All rights reserved.
 */

#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOService.h>

extern "C" {
#include <sys/spa_impl.h>
#include <sys/spa.h>
} /* extern "C" */

#include <sys/ZFSPool.h>

#define	DPRINTF_FUNC()	do { dprintf("%s\n", __func__); } while (0);

#if 0
/* block size is 512 B, count is 512 M blocks */
#define	ZFS_POOL_DEV_BSIZE	(UInt64)(1<<9)
#define	ZFS_POOL_DEV_BCOUNT	(UInt64)(2<<29)
#endif

/*
 * Returns handle to ZFS IOService, with a retain count.
 */
static IOService *
copy_zfs_handle()
{
	/* Get the ZFS service handle the 'hard way' */
	OSDictionary *matching;
	IOService *service = 0;

	matching = IOService::serviceMatching("org_openzfsonosx_zfs_zvol");
	if (matching) {
		service = IOService::copyMatchingService(matching);
		OSSafeReleaseNULL(matching);
	}

	if (!service) {
		dprintf("couldn't get zfs IOService");
		return (NULL);
	}

	return (service);
#if 0
	/* Got service, make sure it casts */
	zfs_hl = OSDynamicCast(org_openzfsonosx_zfs_zvol, service);
	if (zfs_hl == NULL) {
		dprintf("couldn't get zfs_hl");
		/* Drop retain from copyMatchingService */
		OSSafeReleaseNULL(service);
		return (NULL);
	}

	return (zfs_hl);
#endif
}

OSDefineMetaClassAndStructors(ZFSPool, IOService);

#if 0
bool
ZFSPool::open(IOService *client, IOOptionBits options, void *arg)
{
	bool ret;

	IOLog("ZFSPool %s\n", __func__);

	ret = IOService::open(client, options, arg);

	IOLog("ZFSPool %s ret %d\n", __func__, ret);

	return (ret);
}

bool
ZFSPool::isOpen(const IOService *forClient) const
{
	IOLog("ZFSPool %s\n", __func__);
	return (false);
}

void
ZFSPool::close(IOService *client, IOOptionBits options)
{
	IOLog("ZFSPool %s\n", __func__);
	IOService::close(client, options);
}
#endif

bool
ZFSPool::handleOpen(IOService *client,
    IOOptionBits options, void *arg)
{
	bool ret = true;

	dprintf("");
	// IOLog("ZFSPool %s\n", __func__);

	/* XXX IOService open() locks for arbitration around handleOpen */
	// lockForArbitration();
	_openClients->setObject(client);
	ret = _openClients->containsObject(client);
	// unlockForArbitration();

	return (ret);
//	return (IOService::handleOpen(client, options, NULL));
}

bool
ZFSPool::handleIsOpen(const IOService *client) const
{
	bool ret;

	dprintf("");
	// IOLog("ZFSPool %s\n", __func__);

	/* XXX IOService isOpen() locks for arbitration around handleIsOpen */
	// lockForArbitration();
	ret = _openClients->containsObject(client);
	// unlockForArbitration();

	return (ret);
//	return (IOService::handleIsOpen(client));
}

void
ZFSPool::handleClose(IOService *client,
    IOOptionBits options)
{
	dprintf("");
	// IOLog("ZFSPool %s\n", __func__);

	/* XXX IOService close() locks for arbitration around handleClose */
	// lockForArbitration();
	if (_openClients->containsObject(client) == false) {
		dprintf("not open");
	}
	/* Remove client from set */
	_openClients->removeObject(client);
	// unlockForArbitration();

//	IOService::handleClose(client, options);
}

#if 0
/* XXX IOBlockStorageDevice */
void
ZFSPool::read(IOService *client, UInt64 byteStart,
    IOMemoryDescriptor *buffer, IOStorageAttributes *attr,
    IOStorageCompletion *completion)
{
	IOLog("ZFSPool %s\n", __func__);
	IOStorage::complete(completion, kIOReturnError, 0);
}

void
ZFSPool::write(IOService *client, UInt64 byteStart,
    IOMemoryDescriptor *buffer, IOStorageAttributes *attr,
    IOStorageCompletion *completion)
{
	IOLog("ZFSPool %s\n", __func__);
	IOStorage::complete(completion, kIOReturnError, 0);
}
#endif

bool
ZFSPool::setPoolName(const char *name)
{
/* Assign dataset name from null-terminated string */
	OSString *dsstr;
	// const OSSymbol *dsstr;
#if 0
	OSDictionary *dict;
	char *newname, *oldname;
#else
	char *newname;
#endif
	size_t len;

	DPRINTF_FUNC();

	/* Validate arguments */
	if (!name || (len = strnlen(name,
	    ZFS_MAX_DATASET_NAME_LEN)) == 0) {
		dprintf("missing argument");
		return (false);
	}

	/* Truncate too-long names (shouldn't happen) */
	if (len == ZFS_MAX_DATASET_NAME_LEN &&
	    name[ZFS_MAX_DATASET_NAME_LEN] != '\0') {
		dprintf("name too long [%s]", name);
		/* XXX Just truncate the name */
		len--;
	}

	/* Allocate room for name plus null char */
	newname = (char *)kmem_alloc(len+1, KM_SLEEP);
	if (!newname) {
		dprintf("string alloc failed");
		return (false);
	}
	snprintf(newname, len+1, "%s", name);
	newname[len] = '\0'; /* just in case */

	/* Save an OSString copy for IORegistry */
	dsstr = OSString::withCString(newname);
	// dsstr = OSSymbol::withCString(newname);

	kmem_free(newname, len+1);

	if (!dsstr) {
		dprintf("OSString failed");
		return (false);
	}

#if 0
	/* Swap into class private var */
	oldname = (char *)productString;
	productString = newname;
	newname = 0;
	if (oldname) {
		kmem_free(oldname, strlen(oldname)+1);
		oldname = 0;
	}

	/* Get and clone device characteristics prop dict */
	if ((dict = OSDynamicCast(OSDictionary,
	    getProperty(kIOPropertyDeviceCharacteristicsKey))) == NULL ||
	    (dict = OSDictionary::withDictionary(dict)) == NULL) {
		dprintf("couldn't clone prop dict");
		/* Should only happen during initialization */
	}

	if (dict) {
		/* Copy string, add to dictionary, and replace prop dict */
		if (dict->setObject(kIOPropertyProductNameKey,
		    dsstr) == false ||
		    setProperty(kIOPropertyDeviceCharacteristicsKey,
		    dict) == false) {
			dprintf("couldn't set name");
			OSSafeReleaseNULL(dsstr);
			OSSafeReleaseNULL(dict);
			return (false);
		}
		OSSafeReleaseNULL(dict);
	}
#endif

	/* Set Pool name IOregistry property */
	setProperty(kZFSPoolNameKey, dsstr);

	/* Finally, set the IORegistryEntry/IOService name */
	setName(dsstr->getCStringNoCopy());
	OSSafeReleaseNULL(dsstr);

	return (true);
}

bool
ZFSPool::init(OSDictionary *properties, spa_t *spa)
{
#if 0
	/* Allocate dictionaries and symbols */
	OSDictionary *pdict = OSDictionary::withCapacity(2);
	OSDictionary *ddict = OSDictionary::withCapacity(4);
	const OSSymbol *virtualSymbol = OSSymbol::withCString(
	    kIOPropertyPhysicalInterconnectTypeVirtual);
	const OSSymbol *locationSymbol = OSSymbol::withCString(
	    kIOPropertyInternalExternalKey);
	const OSSymbol *ssdSymbol = OSSymbol::withCString(
	    kIOPropertyMediumTypeSolidStateKey);
	OSNumber *physSize = NULL, *logSize = NULL;
	const OSSymbol *vendorSymbol = 0;
	const OSSymbol *revisionSymbol = 0;
	const OSSymbol *blankSymbol = 0;
	OSBoolean *rdonly = 0;
	UInt64 phys_bsize, log_bsize;
	OSString *str = 0;
	const char *cstr = 0;
#endif
	uint64_t space;
	bool ret = false;

	DPRINTF_FUNC();

#if 0
	physSize = OSNumber::withNumber((uint32_t)ZFS_POOL_DEV_BSIZE, 32);
	logSize = OSNumber::withNumber((uint32_t)ZFS_POOL_DEV_BSIZE, 32);
#endif
	if (!spa) {
		dprintf("missing spa");
		goto error;
	}

#if 0
	/* Get physical and logical size from spa */
	phys_bsize = (1ULL<<spa->spa_max_ashift);
	log_bsize = (1ULL<<spa->spa_min_ashift);
#endif

#if 0
	/* Workaround glitchy behavior with large bsize in xnu */
	if (log_bsize > 8192) log_bsize = 8192;
#endif

#if 0
	/* XXX Shouldn't be possible */
	if (log_bsize == 0) log_bsize = DEV_BSIZE;

	physSize = OSNumber::withNumber((uint32_t)phys_bsize, 32);
	logSize = OSNumber::withNumber((uint32_t)log_bsize, 32);

	/* Validate allocations */
	if (!pdict || !ddict || !virtualSymbol || !locationSymbol ||
	    !ssdSymbol || !physSize || !logSize) {
		dprintf("allocation failed");
		goto error;
	}
#endif

	/* Need an OSSet for open clients */
	_openClients = OSSet::withCapacity(1);
	if (_openClients == NULL) {
		dprintf("client OSSet failed");
		goto error;
	}

	/* Set spa pointer and this Pool object's name to match */
	if (!spa) {
		dprintf("missing spa");
		goto error;
	}
	_spa = spa;
	// setName(spa_name(spa));

#if 0
	/* Init class statics every time an instance inits */
	/* Shared across instances, but doesn't hurt to reprint */
	if (vendorString == NULL) {
		char *string;
		int len = strlen("zpool")+1;
		string = (char *)kmem_alloc(len, KM_SLEEP);
		if (!string) goto error;
		snprintf(string, len, "zpool");
		vendorString = string;
	}

	if (revisionString == NULL) {
		char *string;
		int len = strlen("0.1")+1;
		string = (char *)kmem_alloc(len, KM_SLEEP);
		if (!string) goto error;
		snprintf(string, len, "0.1");
		revisionString = string;
	}

	if (revisionString == NULL) {
		char *string;
		int len = strlen("ZFS Pool")+1;
		string = (char *)kmem_alloc(len, KM_SLEEP);
		if (!string) goto error;
		snprintf(string, len, "ZFS pool");
		infoString = string;
	}

	/* For IORegistry keys, cache OSSymbols for class statics */
	/* Leverages OSSymbol cahce pool to reuse across instances */
	vendorSymbol = OSSymbol::withCString(vendorString);
	revisionSymbol = OSSymbol::withCString(revisionString);
	blankSymbol = OSSymbol::withCString("");
	if (!vendorSymbol || !revisionSymbol || !blankSymbol) {
		dprintf("class symbols failed");
		goto error;
	}
#endif

	/* Call super init */
	if (IOService::init(properties) == false) {
		dprintf("device init failed");
		goto error;
	}

#if 0
	/* Set class private vars */
	productString = NULL;
	isReadOnly = false; // XXX should really be true initially

	/* Set Protocol Characteristics */
	if (pdict->setObject(kIOPropertyPhysicalInterconnectLocationKey,
	    locationSymbol) == false ||
	    pdict->setObject(kIOPropertyPhysicalInterconnectTypeKey,
	    virtualSymbol) == false) {
		dprintf("pdict set properties failed");
		goto error;
	}
	setProperty(kIOPropertyProtocolCharacteristicsKey, pdict);

	/* Set Device Characteristics */
	if (ddict->setObject(kIOPropertyVendorNameKey,
	    vendorSymbol) == false ||
	    ddict->setObject(kIOPropertyProductRevisionLevelKey,
	    revisionSymbol) == false ||
	    ddict->setObject(kIOPropertyProductSerialNumberKey,
	    blankSymbol) == false ||
	    ddict->setObject(kIOPropertyPhysicalBlockSizeKey,
	    physSize) == false ||
	    ddict->setObject(kIOPropertyLogicalBlockSizeKey,
	    logSize) == false ||
	    ddict->setObject(kIOPropertyMediumTypeKey,
	    ssdSymbol) == false) {
		dprintf("ddict set properties failed");
		goto error;
	}
	setProperty(kIOPropertyDeviceCharacteristicsKey, ddict);

	/* Check for passed in readonly status */
	if (properties && (rdonly = OSDynamicCast(OSBoolean,
	    properties->getObject(kZFSPoolReadOnlyKey))) != NULL) {
		/* Got the boolean */
		isReadOnly = rdonly->getValue();
		dprintf("set %s", (isReadOnly ? "readonly" : "readwrite"));
	}

	/* Check for passed in pool GUID */
	if (properties && (str = OSDynamicCast(OSString,
	    properties->getObject(kZFSPoolGUIDKey))) != NULL) {
		/* Got the string, try to set GUID */
		str->retain();
		if (ddict->setObject(kZFSPoolGUIDKey, str) == false) {
			dprintf("couldn't set GUID");
			OSSafeReleaseNULL(str);
			goto error;
		}
#ifdef DEBUG
		cstr = str->getCStringNoCopy();
		dprintf("set GUID");
		cstr = 0;
#endif
		OSSafeReleaseNULL(str);
	}
#endif

	if (setPoolName(spa_name(spa)) == false) {
		dprintf("setPoolName failed");
		goto error;
	}

	space = spa_get_dspace(spa);
dprintf("space %llu", space);
	setProperty(kZFSPoolSizeKey, space, 64);

#if 0
	/* Check for passed in pool name */
	if (properties && (str = OSDynamicCast(OSString,
	    properties->getObject(kZFSPoolNameKey))) != NULL &&
	    (cstr = str->getCStringNoCopy()) != NULL) {
		/* Got the string, try to set name */
		str->retain();
		if (setPoolName(cstr) == false) {
			/* Unlikely */
			dprintf("couldn't setup pool"
			    " name property [%s]", cstr);
			OSSafeReleaseNULL(str);
			goto error;
		}

		dprintf("set pool name [%s]", cstr);
		OSSafeReleaseNULL(str);
	} else {
		if (setPoolName("invalid") == false) {
			dprintf("setPoolName failed");
			goto error;
		}
		dprintf("set name [invalid]");
	}
#endif

	/* Success */
	ret = true;

error:
#if 0
	/* All of these will be released on error */
	OSSafeReleaseNULL(pdict);
	OSSafeReleaseNULL(ddict);
	OSSafeReleaseNULL(virtualSymbol);
	OSSafeReleaseNULL(locationSymbol);
	OSSafeReleaseNULL(ssdSymbol);
	OSSafeReleaseNULL(physSize);
	OSSafeReleaseNULL(logSize);
	OSSafeReleaseNULL(vendorSymbol);
	OSSafeReleaseNULL(revisionSymbol);
	OSSafeReleaseNULL(blankSymbol);
	OSSafeReleaseNULL(str);
#endif
	return (ret);
}

void
ZFSPool::free()
{
	OSSet *oldSet;
#if 0
	char *pstring;
#endif

	if (_openClients) {
		oldSet = _openClients;
		_openClients = 0;
		OSSafeReleaseNULL(oldSet);
	}
	_spa = 0;

#if 0
	pstring = (char *)productString;
	productString = 0;
	if (pstring) kmem_free(pstring, strlen(pstring) + 1);
#endif

	IOService::free();
}

extern "C" {

void
spa_iokit_pool_proxy_destroy(spa_t *spa)
{
	ZFSPool *proxy;
	spa_iokit_t *wrapper;

	if (!spa) {
		printf("missing spa");
		return;
	}

	/* Get pool proxy */
	wrapper = spa->spa_iokit_proxy;
	spa->spa_iokit_proxy = NULL;

	if (wrapper == NULL) {
		printf("missing spa_iokit_proxy");
		return;
	}

	proxy = wrapper->proxy;

	/* Free the struct */
	kmem_free(wrapper, sizeof (spa_iokit_t));
	if (!proxy) {
		printf("missing proxy");
		return;
	}

	if (proxy->terminate(kIOServiceSynchronous|
	    kIOServiceRequired) == false) {
		dprintf("terminate failed");
	}
	proxy->release();

	/*
	 * IOService *provider;
	 * provider = proxy->getProvider();
	 *
	 * proxy->detach(provider);
	 * proxy->stop(provider);
	 *
	 * proxy->release();
	 */
}

int
spa_iokit_pool_proxy_create(spa_t *spa)
{
	IOService *zfs_hl;
	ZFSPool *proxy;
	spa_iokit_t *wrapper;

	if (!spa) {
		dprintf("missing spa");
		return (EINVAL);
	}

	/* Allocate C struct */
	if ((wrapper = (spa_iokit_t *)kmem_alloc(sizeof (spa_iokit_t),
	    KM_SLEEP)) == NULL) {
		dprintf("couldn't allocate wrapper");
		return (ENOMEM);
	}

	/* Get ZFS IOService */
	if ((zfs_hl = copy_zfs_handle()) == NULL) {
		dprintf("couldn't get ZFS handle");
		kmem_free(wrapper, sizeof (spa_iokit_t));
		return (ENODEV);
	}

	/* Allocate and init ZFS pool proxy */
	proxy = ZFSPool::withProviderAndPool(zfs_hl, spa);
	if (!proxy) {
		dprintf("Pool proxy creation failed");
		kmem_free(wrapper, sizeof (spa_iokit_t));
		OSSafeReleaseNULL(zfs_hl);
		return (ENOMEM);
	}
	/* Drop retain from copy_zfs_handle */
	OSSafeReleaseNULL(zfs_hl);

	/* Set pool proxy */
	wrapper->proxy = proxy;
	spa->spa_iokit_proxy = wrapper;

	return (0);
}

} /* extern "C" */

ZFSPool *
ZFSPool::withProviderAndPool(IOService *zfs_hl, spa_t *spa)
{
	ZFSPool *proxy = new ZFSPool;

	if (!proxy) {
		printf("allocation failed");
		return (0);
	}

	if (proxy->init(0, spa) == false ||
	    proxy->attach(zfs_hl) == false) {
		printf("init/attach failed");
		OSSafeReleaseNULL(proxy);
		return (0);
	}

	if (proxy->start(zfs_hl) == false) {
		printf("start failed");
		proxy->detach(zfs_hl);
		OSSafeReleaseNULL(proxy);
		return (0);
	}

	/* Open zfs_hl, adding proxy to its open clients */
	// if (proxy->open(zfs_hl) == false) {
	if (zfs_hl->open(proxy) == false) {
		printf("open failed");
		proxy->stop(zfs_hl);
		proxy->detach(zfs_hl);
		OSSafeReleaseNULL(proxy);
		return (0);
	}
	proxy->registerService(kIOServiceAsynchronous);

	return (proxy);
}

#if 0
/* XXX IOBlockStorageDevice */
IOReturn
ZFSPool::doSynchronizeCache(void)
{
	dprintf("");
	return (kIOReturnSuccess);
}

IOReturn
ZFSPool::doAsyncReadWrite(IOMemoryDescriptor *buffer,
    UInt64 block, UInt64 nblks,
    IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	char zero[ZFS_POOL_DEV_BSIZE];
	size_t len, cur, off = 0;

	DPRINTF_FUNC();

	if (!buffer) {
		IOStorage::complete(completion, kIOReturnError, 0);
		return (kIOReturnSuccess);
	}

	/* Read vs. write */
	if (buffer->getDirection() == kIODirectionIn) {
		/* Zero the read buffer */
		memset(zero, 0, ZFS_POOL_DEV_BSIZE);
		len = buffer->getLength();
		while (len > 0) {
			cur = (len > ZFS_POOL_DEV_BSIZE ?
			    ZFS_POOL_DEV_BSIZE : len);
			buffer->writeBytes(/* offset */ off,
			    /* buf */ zero, /* length */ cur);
			off += cur;
			len -= cur;
		}
		// dprintf("read: %llu %llu", block, nblks);
		IOStorage::complete(completion, kIOReturnSuccess,
			    buffer->getLength());
		return (kIOReturnSuccess);
	}

	if (buffer->getDirection() != kIODirectionOut) {
		dprintf("invalid direction %d", buffer->getDirection());
		IOStorage::complete(completion, kIOReturnError, 0);
		return (kIOReturnSuccess);
	}

	/*
	 * XXX For now this just returns error for all writes.
	 * If it turns out that mountroot/bdevvp try to
	 * verify writable status by reading a block and writing
	 * it back to disk, lie and say it succeeded.
	 */
	dprintf("write: %llu %llu", block, nblks);
	IOStorage::complete(completion, kIOReturnError, 0);
	return (kIOReturnSuccess);
}

IOReturn
ZFSPool::doEjectMedia()
{
	DPRINTF_FUNC();
	/* XXX Called at shutdown, maybe return success? */
	return (kIOReturnError);
}

IOReturn
ZFSPool::doFormatMedia(UInt64 byteCapacity)
{
	DPRINTF_FUNC();
	/* XXX shouldn't need it */
	return (kIOReturnError);
	// return (kIOReturnSuccess);
}

UInt32
ZFSPool::doGetFormatCapacities(UInt64 *capacities,
    UInt32 capacitiesMaxCount) const
{
	DPRINTF_FUNC();
	if (capacities && capacitiesMaxCount > 0) {
		capacities[0] = (ZFS_POOL_DEV_BSIZE * ZFS_POOL_DEV_BCOUNT);
		dprintf("capacity %llu", capacities[0]);
	}

	/* Always inform caller of capacity count */
	return (1);
}

/* Returns full pool name from instance private var */
char *
ZFSPool::getProductString()
{
	if (productString) dprintf("[%s]", productString);
	/* Return class private string */
	return ((char *)productString);
}

/* Returns readonly status from instance private var */
IOReturn
ZFSPool::reportWriteProtection(bool *isWriteProtected)
{
	DPRINTF_FUNC();
	if (isWriteProtected) *isWriteProtected = isReadOnly;
	return (kIOReturnSuccess);
}

/* These return class static string for all instances */
char *
ZFSPool::getVendorString()
{
	dprintf("[%s]", vendorString);
	/* Return class static string */
	return ((char *)vendorString);
}
char *
ZFSPool::getRevisionString()
{
	dprintf("[%s]", revisionString);
	/* Return class static string */
	return ((char *)revisionString);
}
char *
ZFSPool::getAdditionalDeviceInfoString()
{
	dprintf("[%s]", infoString);
	/* Return class static string */
	return ((char *)infoString);
}

/* Always return media present and unchanged */
IOReturn
ZFSPool::reportMediaState(bool *mediaPresent,
    bool *changedState)
{
	DPRINTF_FUNC();
	if (mediaPresent) *mediaPresent = true;
	if (changedState) *changedState = false;
	return (kIOReturnSuccess);
}

/* Always report nonremovable and nonejectable */
IOReturn
ZFSPool::reportRemovability(bool *isRemoveable)
{
	DPRINTF_FUNC();
	if (isRemoveable) *isRemoveable = false;
	return (kIOReturnSuccess);
}
IOReturn
ZFSPool::reportEjectability(bool *isEjectable)
{
	DPRINTF_FUNC();
	if (isEjectable) *isEjectable = false;
	return (kIOReturnSuccess);
}

/* Always report 512b blocksize */
IOReturn
ZFSPool::reportBlockSize(UInt64 *blockSize)
{
	DPRINTF_FUNC();
	if (!blockSize)
		return (kIOReturnError);

	*blockSize = ZFS_POOL_DEV_BSIZE;
	return (kIOReturnSuccess);
}

/* XXX Calculate from dev_bcount, should get size from objset */
/* XXX Can issue message kIOMessageMediaParametersHaveChanged to update */
IOReturn
ZFSPool::reportMaxValidBlock(UInt64 *maxBlock)
{
	DPRINTF_FUNC();
	if (!maxBlock)
		return (kIOReturnError);

	// *maxBlock = 0;
	*maxBlock = ZFS_POOL_DEV_BCOUNT - 1;
	dprintf("maxBlock %llu", *maxBlock);

	return (kIOReturnSuccess);
}
#endif
