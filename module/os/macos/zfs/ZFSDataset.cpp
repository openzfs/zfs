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
 */
/*
 * ZFSDataset - proxy disk for legacy and com.apple.devicenode mounts.
 */

#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <sys/ZFSDatasetScheme.h>
#include <sys/ZFSDataset.h>
#include <sys/spa_impl.h>
#include <sys/dsl_prop.h>
#include <sys/zfs_vfsops.h>
#include <sys/ZFSPool.h>

#define	DPRINTF_FUNC()	do { dprintf(""); } while (0);

OSDefineMetaClassAndStructors(ZFSDataset, IOMedia);

#if 0
/* XXX Only for debug tracing */
bool
ZFSDataset::open(IOService *client,
	    IOOptionBits options, IOStorageAccess access)
{
	bool ret;
	DPRINTF_FUNC();

	ret = IOMedia::open(client, options, access);

	dprintf("ZFSDataset %s ret %d", ret);
	return (ret);
}

bool
ZFSDataset::isOpen(const IOService *forClient) const
{
	DPRINTF_FUNC();
	return (false);
}

void
ZFSDataset::close(IOService *client,
	    IOOptionBits options)
{
	DPRINTF_FUNC();
	IOMedia::close(client, options);
}

bool
ZFSDataset::handleOpen(IOService *client,
	    IOOptionBits options, void *access)
{
	bool ret;
	DPRINTF_FUNC();

	ret = IOMedia::handleOpen(client, options, access);

	dprintf("ZFSDataset %s ret %d", ret);
	return (ret);
}

bool
ZFSDataset::handleIsOpen(const IOService *client) const
{
	bool ret;
	DPRINTF_FUNC();

	ret = IOMedia::handleIsOpen(client);

	dprintf("ZFSDataset %s ret %d", ret);
	return (ret);
}

void
ZFSDataset::handleClose(IOService *client,
    IOOptionBits options)
{
	DPRINTF_FUNC();
	IOMedia::handleClose(client, options);
}

bool
ZFSDataset::attach(IOService *provider)
{
	DPRINTF_FUNC();
	return (IOMedia::attach(provider));
}

void
ZFSDataset::detach(IOService *provider)
{
	DPRINTF_FUNC();
	IOMedia::detach(provider);
}

bool
ZFSDataset::start(IOService *provider)
{
	DPRINTF_FUNC();
	return (IOMedia::start(provider));
}

void
ZFSDataset::stop(IOService *provider)
{
	DPRINTF_FUNC();
	IOMedia::stop(provider);
}
#endif

/* XXX Only for debug tracing */
void
ZFSDataset::free()
{
	DPRINTF_FUNC();
	IOMedia::free();
}

/*
 * Override init to call IOMedia init then setup properties.
 */
bool
ZFSDataset::init(UInt64 base, UInt64 size,
    UInt64 preferredBlockSize,
    IOMediaAttributeMask attributes,
    bool isWhole, bool isWritable,
    const char *contentHint,
    OSDictionary *properties)
{
	OSDictionary *newProps = NULL, *deviceDict;
	OSNumber *physSize, *logSize;
#if 0
	OSDictionary *protocolDict;
	const OSSymbol *virtualSymbol, *internalSymbol;
#endif
	bool ret;

	DPRINTF_FUNC();

	/* Clone or create new properties dictionary */
	if (properties) newProps = OSDictionary::withDictionary(properties);
	if (!newProps) newProps = OSDictionary::withCapacity(2);

	/* Allocate dictionaries, numbers, and string symbols */
	deviceDict = OSDictionary::withCapacity(2);
#if 0
	protocolDict = OSDictionary::withCapacity(2);
#endif

	physSize = OSNumber::withNumber(4096, 32);
	logSize = OSNumber::withNumber(512, 32);

#if 0
	kIOPropertyPhysicalInterconnectTypeVirtual
	    kIOPropertyPhysicalInterconnectTypeKey
	    kIOPropertyInterconnectFileKey
	    kIOPropertyInternalKey
	    kIOPropertyPhysicalInterconnectLocationKey

	    kIOPropertyProtocolCharacteristicsKey
	    kIOPropertyMediumTypeKey
	    kIOPropertyLogicalBlockSizeKey
	    kIOPropertyPhysicalBlockSizeKey
	    kIOPropertyBytesPerPhysicalSectorKey
	    kIOPropertyDeviceCharacteristicsKey
	    kIOBlockStorageDeviceTypeKey
	    kIOBlockStorageDeviceTypeGeneric
#endif

#if 0
	    virtualSymbol = OSSymbol::withCString(
	    kIOPropertyPhysicalInterconnectTypeVirtual);
	internalSymbol = OSSymbol::withCString(
	    kIOPropertyInternalKey);
#endif

	/* Validate allocations */
	if (!newProps || !deviceDict || !physSize || !logSize
#if 0
	    // || !protocolDict || !virtualSymbol || !internalSymbol
#endif
	    ) {
		dprintf("symbol allocation failed");
		OSSafeReleaseNULL(newProps);
		OSSafeReleaseNULL(deviceDict);
#if 0
		OSSafeReleaseNULL(protocolDict);
#endif
		OSSafeReleaseNULL(physSize);
		OSSafeReleaseNULL(logSize);
#if 0
		OSSafeReleaseNULL(virtualSymbol);
		OSSafeReleaseNULL(internalSymbol);
#endif
		return (false);
	}

	/* Setup device characteristics */
	deviceDict->setObject(kIOPropertyPhysicalBlockSizeKey, physSize);
	deviceDict->setObject(kIOPropertyLogicalBlockSizeKey, logSize);
	OSSafeReleaseNULL(physSize);
	OSSafeReleaseNULL(logSize);

#if 0
	/* Setup protocol characteristics */
	protocolDict->setObject(kIOPropertyPhysicalInterconnectTypeKey,
	    virtualSymbol);
	protocolDict->setObject(kIOPropertyPhysicalInterconnectLocationKey,
	    internalSymbol);
	OSSafeReleaseNULL(virtualSymbol);
	OSSafeReleaseNULL(internalSymbol);
#endif

	/* XXX Setup required IOMedia props */

	/* Set new device and protocol dictionaries */
	if (newProps->setObject(kIOPropertyDeviceCharacteristicsKey,
	    deviceDict) == false
#if 0
		// ||
	    // newProps->setObject(kIOPropertyProtocolCharacteristicsKey,
	    // protocolDict) == false
#endif
	    ) {
		dprintf("setup properties failed");
		OSSafeReleaseNULL(newProps);
		OSSafeReleaseNULL(deviceDict);
#if 0
		OSSafeReleaseNULL(protocolDict);
#endif
		return (false);
	}
	OSSafeReleaseNULL(deviceDict);
#if 0
	OSSafeReleaseNULL(protocolDict);
#endif

	/* Call IOMedia init with size and newProps */
	ret = IOMedia::init(base, size, preferredBlockSize,
	    attributes, isWhole, isWritable, contentHint,
	    newProps);
	OSSafeReleaseNULL(newProps);

	if (!ret) dprintf("IOMedia init failed");

	return (ret);

#if 0
	/* Get current device and protocol dictionaries */
	lockForArbitration();
	oldDeviceDict = OSDynamicCast(OSDictionary,
	    getProperty(kIOStorageDeviceCharacteristicsKey));
	oldProtocolDict = OSDynamicCast(OSDictionary,
	    getProperty(kIOStorageProtocolCharacteristicsKey));
	if (oldDeviceDict) oldDeviceDict->retain();
	if (oldProtocolDict) oldProtocolDict->retain();
	unlockForArbitration();

	/* Clone existing dictionaries */
	if (oldDeviceDict) {
		newDeviceDict = OSDictionary::withDict(oldDeviceDict);
		OSSafeReleaseNULL(oldDeviceDict);
	}
	if (oldProtocolDict) {
		newProtocolDict = OSDictionary::withDict(oldProtocolDict);
		OSSafeReleaseNULL(oldDeviceDict);
	}

	/* Make new if missing */
	if (!newDeviceDict)
		newDeviceDict = OSDictionary::withCapacity(2);
	if (!newProtocolDict)
		newProtocolDict = OSDictionary::withCapacity(2);

	/* Setup device characteristics */
	newDeviceDict->setObject(kIOStoragePhysicalBlocksizeKey, physSize);
	newDeviceDict->setObject(kIOStorageLogicalBlocksizeKey, logSize);
	OSSafeReleaseNULL(physSize);
	OSSafeReleaseNULL(logSize);

	/* Setup protocol characteristics */
	newProtocolDict->setObject(kIOStorageProtocolInterconnectTypeKey,
	    virtualSymbol);
	newProtocolDict->setObject(kIOStorageProtocolInterconnectNameKey,
	    internalSymbol);
	OSSafeReleaseNULL(virtualSymbol);
	OSSafeReleaseNULL(internalSymbol);

	/* XXX Setup required IOMedia props */

	/* Set new device and protocol dictionaries */
	lockForArbitration();
	setProperty(kIOStorageDeviceCharacteristicsKey, newDeviceDict);
	setProperty(kIOStorageProtocolCharacteristicsKey, newProtocolDict);
	unlockForArbitration();

	/* Cleanup and return success */
	OSSafeReleaseNULL(newDeviceDict);
	OSSafeReleaseNULL(newProtocolDict);
	return (true);
#endif
}

/*
 * Set both the IOService name and the ZFS Dataset property.
 */
bool
ZFSDataset::setDatasetName(const char *name)
{
	OSDictionary *prevDict, *newDict = NULL;
	OSString *datasetString;
	const char *newname;

	if (!name || name[0] == '\0') {
		dprintf("missing name");
		return (false);
	}

	if ((newname = strrchr(name, '/')) == NULL) {
		newname = name;
	} else {
		/* Advance beyond slash */
		newname++;
	}

#if 0
	size_t len;
	/* Length of IOMedia name plus null terminator */
	len = (strlen(kZFSIOMediaPrefix) + strlen(name) +
	    strlen(kZFSIOMediaSuffix) + 1);
	// len = strlen("ZFS ") + strlen(name) + strlen(" Media") + 1;

	newname = (char *)kmem_alloc(len, KM_SLEEP);
#endif
	datasetString = OSString::withCString(name);

#if 0
	nameString = OSString::withCString(newname);
	if (newname == NULL || nameString == NULL) {
		dprintf("couldn't make name strings");
		OSSafeReleaseNULL(nameString);
		if (newname) kmem_free(newname, len);
		return (false);
	}
#else
	if (datasetString == NULL) {
		dprintf("couldn't make name strings");
		return (false);
	}
#endif

#if 0
	memset(newname, 0, len);
	snprintf(newname, len, "%s%s%s", kZFSIOMediaPrefix,
	    name, kZFSIOMediaSuffix);

	ASSERT3U(strlen(newname), ==, len-1);
#endif

	/* Lock IORegistryEntry and get current prop dict */
	lockForArbitration();
	if ((prevDict = OSDynamicCast(OSDictionary,
	    getProperty(kIOPropertyDeviceCharacteristicsKey))) == NULL) {
	    /* Unlock IORegistryEntry */
		unlockForArbitration();
		dprintf("couldn't get prop dict");
	}
	prevDict->retain();
	unlockForArbitration();

	/* Clone existing dictionary */
	if (prevDict) {
		if ((newDict = OSDictionary::withDictionary(prevDict)) ==
		    NULL) {
			dprintf("couldn't clone prop dict");
		}
		OSSafeReleaseNULL(prevDict);
		/* Non-fatal at the moment */
	}

	/* If prevDict did not exist or couldn't be copied, make new */
	if (!newDict && (newDict = OSDictionary::withCapacity(1)) == NULL) {
		dprintf("couldn't make new prop dict");
	}

	/* If we have a new or copied dict at this point */
	if (newDict) {
		/* Add or replace dictionary Product Name string */
		if (newDict->setObject(kIOPropertyProductNameKey,
		    datasetString) == false) {
			dprintf("couldn't set name");
			OSSafeReleaseNULL(datasetString);
			// OSSafeReleaseNULL(nameString);
			// kmem_free(newname, len);
			OSSafeReleaseNULL(newDict);
			return (false);
		}

		/* Lock IORegistryEntry and replace prop dict */
		lockForArbitration();
		if (setProperty(kIOPropertyDeviceCharacteristicsKey,
		    newDict) == false) {
			unlockForArbitration();
			dprintf("couldn't set name");
			OSSafeReleaseNULL(datasetString);
			// OSSafeReleaseNULL(nameString);
			// kmem_free(newname, len);
			OSSafeReleaseNULL(newDict);
			return (false);
		}
		unlockForArbitration();
		OSSafeReleaseNULL(newDict);
	}

	/* Lock IORegistryEntry to replace property and set name */
	lockForArbitration();
	/* Assign plain ZFS Dataset name */
	setProperty(kZFSDatasetNameKey, datasetString);
	/* Assign IOMedia name */
	// setName(name);
	setName(newname);

	/* Unlock IORegistryEntry and cleanup allocations */
	unlockForArbitration();
	OSSafeReleaseNULL(datasetString);
	// kmem_free(newname, len);
	// OSSafeReleaseNULL(nameString);
	return (true);
}

#if 0
static inline uint64_t
get_objnum(const char *name)
{
	objset_t *os = NULL;
	uint64_t objnum;
	int error;

	if (!name)
		return (0);

	error = dmu_objset_own(name, DMU_OST_ZFS, B_TRUE, FTAG, &os);
	if (error != 0) {
		dprintf("couldn't open dataset %d", error);
		return (0);
	}

	objnum = dmu_objset_id(os);

	dmu_objset_disown(os, FTAG);

	return (objnum);
}
#endif

/*
 * Create a proxy device, name it appropriately, and return it.
 */
ZFSDataset *
ZFSDataset::withDatasetNameAndSize(const char *name, uint64_t size)
{
	ZFSDataset *dataset = NULL;
	objset_t *os = NULL;
	OSString *uuidStr = NULL;
	OSObject *property = NULL;
	char uuid_cstr[37];
	uint64_t objnum, readonly, guid;
#if 0
	// uint64_t ref_size, avail_size, obj_count, obj_free;
#endif
	uuid_t uuid;
	int error;
	bool isWritable;

	DPRINTF_FUNC();

	if (!name || name[0] == '\0') {
		dprintf("missing name");
		/* Nothing allocated or retained yet */
		return (NULL);
	}
	memset(uuid_cstr, 0, sizeof (uuid_cstr));

#if 0
	OSNumber *sizeNum = NULL;
	property = copyProperty(kZFSPoolSizeKey, gIOServicePlane,
	    kIORegistryIterateRecursively|kIORegistryIterateParents);
	if (!property) {
		dprintf("couldn't get pool size");
		/* Nothing allocated or retained yet */
		return (NULL);
	}
	if ((sizeNum = OSDynamicCast(OSNumber, property)) == NULL) {
		dprintf("couldn't cast pool size");
		goto error;
	}
	size = sizeNum->unsigned64BitValue();
	sizeNum = NULL;
	OSSafeReleaseNULL(property);
#endif

	if (zfs_vfs_uuid_gen(name, uuid) != 0) {
		dprintf("UUID gen failed");
		goto error;
	}
	// uuid_unparse(uuid, uuid_cstr);
	zfs_vfs_uuid_unparse(uuid, uuid_cstr);
	// snprintf(uuid_cstr, sizeof (uuid_cstr), "");

	uuidStr = OSString::withCString(uuid_cstr);
	if (!uuidStr) {
		dprintf("uuidStr alloc failed");
		goto error;
	}

	dataset = new ZFSDataset;
	if (!dataset) {
		dprintf("allocation failed");
		goto error;
	}

	/* Own the dmu objset to get properties */
	error = dmu_objset_own(name, DMU_OST_ZFS, B_TRUE, B_FALSE, FTAG, &os);
	if (error != 0) {
		dprintf("couldn't open dataset %d", error);
		goto error;
	}

	/* Get the dsl_dir to lookup object number */
	objnum = dmu_objset_id(os);

#if 0
	dmu_objset_space(os, &ref_size, &avail_size, &obj_count, &obj_free);
#endif

	// if (os->os_dsl_dataset)
	//	guid = dsl_dataset_phys(os->os_dsl_dataset)->ds_guid;
	guid = dmu_objset_fsid_guid(os);
	// dsl_prop_get_integer(name, "guid", &guid, NULL) != 0) {

	if (dsl_prop_get_integer(name, "readonly", &readonly, NULL) != 0) {
		dmu_objset_disown(os, B_FALSE, FTAG);
		dprintf("get readonly property failed");
		goto error;
	}
	// size = (1<<30);
	// isWritable = true;
	dmu_objset_disown(os, B_FALSE, FTAG);

#if 0
	size = ref_size + avail_size;
#endif

	isWritable = (readonly == 0ULL);

	if (dataset->init(/* base */ 0, size, DEV_BSIZE,
	    /* attributes */ 0, /* isWhole */ false, isWritable,
	    kZFSContentHint, /* properties */ NULL) == false) {
		dprintf("init failed");
		goto error;
	}

	if (dataset->setDatasetName(name) == false) {
		dprintf("invalid name");
		goto error;
	}

	/* Set media UUID */
	dataset->setProperty(kIOMediaUUIDKey, uuidStr);
	OSSafeReleaseNULL(uuidStr);

	return (dataset);

error:
	OSSafeReleaseNULL(property);
	OSSafeReleaseNULL(uuidStr);
	OSSafeReleaseNULL(dataset);
	return (NULL);
}

/*
 * Compatibility method simulates a read but returns all zeros.
 */
void
ZFSDataset::read(IOService *client,
    UInt64 byteStart, IOMemoryDescriptor *buffer,
    IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	IOByteCount total, cur_len, done = 0;
	addr64_t cur;

	DPRINTF_FUNC();
	if (!buffer) {
		if (completion) complete(completion, kIOReturnInvalid, 0);
		return;
	}

	total = buffer->getLength();

	/* XXX Get each physical segment of the buffer and zero it */
	while (done < total) {
		cur_len = 0;
		cur = buffer->getPhysicalSegment(done, &cur_len);
		if (cur == 0) break;
		if (cur_len != 0) bzero_phys(cur, cur_len);
		done += cur_len;
		ASSERT3U(done, <=, total);
	}
	ASSERT3U(done, ==, total);

	// if (!completion || !completion->action) {
	if (!completion) {
		dprintf("invalid completion");
		return;
	}

//	(completion->action)(completion->target, completion->parameter,
//	    kIOReturnSuccess, total);
	complete(completion, kIOReturnSuccess, total);
}

/*
 * Compatibility method simulates a write as a no-op.
 */
void
ZFSDataset::write(IOService *client,
    UInt64 byteStart, IOMemoryDescriptor *buffer,
    IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	IOByteCount total;
	DPRINTF_FUNC();

	if (!buffer) {
		if (completion) complete(completion, kIOReturnInvalid);
		return;
	}

	total = buffer->getLength();

	// if (!completion || !completion->action) {
	if (!completion) {
		dprintf("invalid completion");
		return;
	}

	/* XXX No-op, just return success */
//	(completion->action)(completion->target, completion->parameter,
//	    kIOReturnSuccess, total);
	complete(completion, kIOReturnSuccess, total);
}

#ifdef DEBUG
volatile SInt64 num_sync = 0;
#endif

/*
 * Compatibility method simulates a barrier sync as a no-op.
 */
#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
IOReturn
ZFSDataset::synchronize(IOService *client,
    UInt64 byteStart, UInt64 byteCount,
    IOStorageSynchronizeOptions options)
#else
IOReturn
ZFSDataset::synchronizeCache(IOService *client)
#endif
{
#ifdef DEBUG
	SInt64 cur_sync = 0;
	DPRINTF_FUNC();
	cur_sync = OSIncrementAtomic64(&num_sync);
	dprintf("sync called %lld times", cur_sync);
#endif

	/* XXX Needs to report success for mount_common() */
	return (kIOReturnSuccess);
}

/*
 * Compatibility method returns failure (unsupported).
 */
IOReturn
ZFSDataset::unmap(IOService *client,
    IOStorageExtent *extents, UInt32 extentsCount,
#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	IOStorageUnmapOptions	options)
#else
	UInt32	options)
#endif
{
	DPRINTF_FUNC();
	return (kIOReturnUnsupported);
}

/*
 * Compatibility method returns failure (no result).
 */
IOStorage *
ZFSDataset::copyPhysicalExtent(IOService *client,
    UInt64 *byteStart, UInt64 *byteCount)
{
	DPRINTF_FUNC();
	return (0);
	// return (IOMedia::copyPhysicalExtent(client, byteStart, byteCount));
}

/*
 * Compatibility method simulates lock as a no-op.
 */
bool
ZFSDataset::lockPhysicalExtents(IOService *client)
{
	DPRINTF_FUNC();
	// return (IOMedia::unlockPhysicalExtents(client));
	return (true);
}

/*
 * Compatibility method simulates unlock as a no-op.
 */
void
ZFSDataset::unlockPhysicalExtents(IOService *client)
{
	DPRINTF_FUNC();
	// IOMedia::unlockPhysicalExtents(client);
}

/*
 * Compatibility method returns failure (unsupported).
 */
#if defined(MAC_OS_X_VERSION_10_10) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_10)
IOReturn
ZFSDataset::setPriority(IOService *client,
    IOStorageExtent *extents, UInt32 extentsCount,
    IOStoragePriority priority)
{
	DPRINTF_FUNC();
	return (kIOReturnUnsupported);
	// return (IOMedia::setPriority(client, extents,
	// extentsCount, priority));
}
#endif

/*
 * Compatibility method returns default system blocksize.
 */
UInt64
ZFSDataset::getPreferredBlockSize() const
{
	DPRINTF_FUNC();
	return (DEV_BSIZE);
	// return (IOMedia::getPreferredBlockSize());
}

/* XXX Only for debug tracing */
UInt64
ZFSDataset::getSize() const
{
	DPRINTF_FUNC();
	return (IOMedia::getSize());
}

/* XXX Only for debug tracing */
UInt64
ZFSDataset::getBase() const
{
	DPRINTF_FUNC();
	return (IOMedia::getBase());
}

/* XXX Only for debug tracing */
bool
ZFSDataset::isEjectable() const
{
	DPRINTF_FUNC();
	return (IOMedia::isEjectable());
}

/* XXX Only for debug tracing */
bool
ZFSDataset::isFormatted() const
{
	DPRINTF_FUNC();
	return (IOMedia::isFormatted());
}

/* XXX Only for debug tracing */
bool
ZFSDataset::isWhole() const
{
	DPRINTF_FUNC();
	return (IOMedia::isWhole());
}

/* XXX Only for debug tracing */
bool
ZFSDataset::isWritable() const
{
	DPRINTF_FUNC();
	return (IOMedia::isWritable());
}

/* XXX Only for debug tracing */
const char *
ZFSDataset::getContent() const
{
	DPRINTF_FUNC();
	return (IOMedia::getContent());
}

/* XXX Only for debug tracing */
const char *
ZFSDataset::getContentHint() const
{
	DPRINTF_FUNC();
	return (IOMedia::getContentHint());
}

/* XXX Only for debug tracing */
IOMediaAttributeMask
ZFSDataset::getAttributes() const
{
	DPRINTF_FUNC();
	return (IOMedia::getAttributes());
}
