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

#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <sys/ZFSDatasetProxy.h>
#include <sys/ZFSPool.h>
#include <sys/zfs_debug.h>

#define	DPRINTF_FUNC()	do { dprintf(""); } while (0);

/* block size is 512 B, count is 512 M blocks */
#define	ZFS_PROXY_DEV_BSIZE	(UInt64)(1<<9)
#define	ZFS_PROXY_DEV_BCOUNT	(UInt64)(2<<29)
#define	kZFSProxyGUIDKey	"ZFS Pool GUID"
#define	kZFSProxyReadOnlyKey	"ZFS Pool Read-Only"

OSDefineMetaClassAndStructors(ZFSDatasetProxy, IOBlockStorageDevice);

void
ZFSDatasetProxy::free()
{
	char *str;

	/* vendor, revision, and info share a null char */
	if (vendorString) {
		str = (char *)vendorString;
		vendorString = 0;
		if (revisionString == str) revisionString = 0;
		if (infoString == str) infoString = 0;
		IOFree(str, strlen(str)+1);
	}

	/* Product string contains pool name */
	if (productString) {
		str = (char *)productString;
		productString = 0;
		IOFree(str, strlen(str)+1);
	}

	IOBlockStorageDevice::free();
}

bool
ZFSDatasetProxy::init(OSDictionary *properties)
{
	char *str = (char *)IOMalloc(1);

	if (!str) {
		dprintf("string allocation failed\n");
		return (false);
	}
	str[0] = '\0';
	vendorString = str;
	revisionString = str;
	infoString = str;

	if (IOBlockStorageDevice::init(properties) == false) {
		dprintf("BlockStorageDevice start failed");
		goto error;
	}

	return (true);

error:
	if (str) {
		vendorString = 0;
		revisionString = 0;
		infoString = 0;
		IOFree(str, 1);
	}
	return (false);
}

bool
ZFSDatasetProxy::start(IOService *provider)
{
	OSObject *property = NULL, *size = NULL;
	OSString *nameString = NULL;
	OSNumber *sizeNum = NULL;
	OSDictionary *deviceDict = NULL, *protocolDict = NULL;
	const OSSymbol *virtualSymbol = NULL, *internalSymbol = NULL;
	const char *cstr = NULL;
	char *pstring = NULL;
	int plen = 0;
	bool started = false;

	size = copyProperty(kZFSPoolSizeKey, gIOServicePlane,
	    (kIORegistryIterateRecursively|kIORegistryIterateParents));
	property = copyProperty(kZFSPoolNameKey, gIOServicePlane,
	    (kIORegistryIterateRecursively|kIORegistryIterateParents));

	if (!size || !property) {
		dprintf("couldn't get pool name or size");
		goto error;
	}

	nameString = OSDynamicCast(OSString, property);
	if (!nameString) {
		dprintf("missing pool name");
		goto error;
	}
#if 0
	/* Try hard to get the name string */
	do {
		nameString = OSDynamicCast(OSString, property);

		if (nameString) nameString->retain();

		if (!nameString) {
			OSSymbol *nameSymbol;
			nameSymbol = OSDynamicCast(OSSymbol, property);
			if (!nameSymbol) {
				dprintf("couldn't get name");
				goto error;
			}
			nameString = OSString::withCString(
			    nameSymbol->getCStringNoCopy());
		}
	} while (0);
#endif

	sizeNum = OSDynamicCast(OSNumber, size);
	if (!sizeNum) {
		dprintf("invalid size");
		goto error;
	}
	_pool_bcount = sizeNum->unsigned64BitValue() / DEV_BSIZE;
	sizeNum = 0;
	size->release();
	size = 0;

	cstr = nameString->getCStringNoCopy();
	if (!cstr || (plen = strlen(cstr) + 1) == 1) {
		goto error;
	}
	pstring = (char *)IOMalloc(plen);
	if (!pstring) {
		goto error;
	}
	snprintf(pstring, plen, "%s", cstr);
	productString = pstring;
	pstring = 0;

	if (IOBlockStorageDevice::start(provider) == false) {
		dprintf("BlockStorageDevice start failed");
		goto error;
	}
	started = true;

	deviceDict = OSDynamicCast(OSDictionary,
	    getProperty(kIOPropertyDeviceCharacteristicsKey));
	if (deviceDict) {
		/* Clone a new dictionary */
		deviceDict = OSDictionary::withDictionary(deviceDict);
		if (!deviceDict) {
			dprintf("dict clone failed");
			goto error;
		}
	}

	if (!deviceDict) {
		dprintf("creating new device dict");
		deviceDict = OSDictionary::withCapacity(1);
	}

	if (!deviceDict) {
		dprintf("missing device dict");
		goto error;
	}

	deviceDict->setObject(kIOPropertyProductNameKey, nameString);
	OSSafeReleaseNULL(nameString);

	if (setProperty(kIOPropertyDeviceCharacteristicsKey,
	    deviceDict) == false) {
		dprintf("device dict setProperty failed");
		goto error;
	}
	OSSafeReleaseNULL(deviceDict);

	protocolDict = OSDynamicCast(OSDictionary,
	    getProperty(kIOPropertyProtocolCharacteristicsKey));
	if (protocolDict) {
		/* Clone a new dictionary */
		protocolDict = OSDictionary::withDictionary(protocolDict);
		if (!protocolDict) {
			dprintf("dict clone failed");
			goto error;
		}
	}

	if (!protocolDict) {
		dprintf("creating new protocol dict");
		protocolDict = OSDictionary::withCapacity(1);
	}

	if (!protocolDict) {
		dprintf("missing protocol dict");
		goto error;
	}

	virtualSymbol = OSSymbol::withCString(
	    kIOPropertyPhysicalInterconnectTypeVirtual);
	internalSymbol = OSSymbol::withCString(
	    kIOPropertyInternalKey);
	if (!virtualSymbol || !internalSymbol) {
		dprintf("symbol alloc failed");
		goto error;
	}

	protocolDict->setObject(kIOPropertyPhysicalInterconnectTypeKey,
	    virtualSymbol);
	protocolDict->setObject(kIOPropertyPhysicalInterconnectLocationKey,
	    internalSymbol);

	OSSafeReleaseNULL(virtualSymbol);
	OSSafeReleaseNULL(internalSymbol);

	if (setProperty(kIOPropertyProtocolCharacteristicsKey,
	    protocolDict) == false) {
		dprintf("protocol dict setProperty failed");
		goto error;
	}
	OSSafeReleaseNULL(protocolDict);
	registerService(kIOServiceAsynchronous);

	return (true);

error:
	OSSafeReleaseNULL(size);
	OSSafeReleaseNULL(property);
	OSSafeReleaseNULL(deviceDict);
	OSSafeReleaseNULL(protocolDict);
	OSSafeReleaseNULL(nameString);
	OSSafeReleaseNULL(virtualSymbol);
	OSSafeReleaseNULL(internalSymbol);
	if (pstring) IOFree(pstring, plen);
	if (started) IOBlockStorageDevice::stop(provider);
	return (false);
}

/* XXX IOBlockStorageDevice */
IOReturn
ZFSDatasetProxy::doSynchronizeCache(void)
{
	DPRINTF_FUNC();
	return (kIOReturnSuccess);
}

IOReturn
ZFSDatasetProxy::doAsyncReadWrite(IOMemoryDescriptor *buffer,
    UInt64 block, UInt64 nblks,
    IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	char zero[ZFS_PROXY_DEV_BSIZE];
	size_t len, cur, off = 0;

	DPRINTF_FUNC();

	if (!buffer) {
		IOStorage::complete(completion, kIOReturnError, 0);
		return (kIOReturnSuccess);
	}

	/* Read vs. write */
	if (buffer->getDirection() == kIODirectionIn) {
		/* Zero the read buffer */
		bzero(zero, ZFS_PROXY_DEV_BSIZE);
		len = buffer->getLength();
		while (len > 0) {
			cur = (len > ZFS_PROXY_DEV_BSIZE ?
			    ZFS_PROXY_DEV_BSIZE : len);
			buffer->writeBytes(/* offset */ off,
			    /* buf */ zero, /* length */ cur);
			off += cur;
			len -= cur;
		}
		// dprintf("%s: read: %llu %llu",
		//    __func__, block, nblks);
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
ZFSDatasetProxy::doEjectMedia()
{
	DPRINTF_FUNC();
	/* XXX Called at shutdown, maybe return success? */
	return (kIOReturnError);
}

IOReturn
ZFSDatasetProxy::doFormatMedia(UInt64 byteCapacity)
{
	DPRINTF_FUNC();
	/* XXX shouldn't need it */
	return (kIOReturnError);
	// return (kIOReturnSuccess);
}

UInt32
ZFSDatasetProxy::doGetFormatCapacities(UInt64 *capacities,
    UInt32 capacitiesMaxCount) const
{
	DPRINTF_FUNC();
	if (capacities && capacitiesMaxCount > 0) {
		capacities[0] = (ZFS_PROXY_DEV_BSIZE * ZFS_PROXY_DEV_BCOUNT);
		dprintf("capacity %llu", capacities[0]);
	}

	/* Always inform caller of capacity count */
	return (1);
}

/* Returns full pool name from instance private var */
char *
ZFSDatasetProxy::getProductString()
{
	if (productString) dprintf("[%s]", productString);
	/* Return class private string */
	return ((char *)productString);
}

/* Returns readonly status from instance private var */
IOReturn
ZFSDatasetProxy::reportWriteProtection(bool *isWriteProtected)
{
	DPRINTF_FUNC();
	if (isWriteProtected) *isWriteProtected = isReadOnly;
	return (kIOReturnSuccess);
}

/* These return class static string for all instances */
char *
ZFSDatasetProxy::getVendorString()
{
	dprintf("[%s]", vendorString);
	/* Return class static string */
	return ((char *)vendorString);
}
char *
ZFSDatasetProxy::getRevisionString()
{
	dprintf("[%s]", revisionString);
	/* Return class static string */
	return ((char *)revisionString);
}
char *
ZFSDatasetProxy::getAdditionalDeviceInfoString()
{
	dprintf("[%s]", infoString);
	/* Return class static string */
	return ((char *)infoString);
}

/* Always return media present and unchanged */
IOReturn
ZFSDatasetProxy::reportMediaState(bool *mediaPresent,
    bool *changedState)
{
	DPRINTF_FUNC();
	if (mediaPresent) *mediaPresent = true;
	if (changedState) *changedState = false;
	return (kIOReturnSuccess);
}

/* Always report nonremovable and nonejectable */
IOReturn
ZFSDatasetProxy::reportRemovability(bool *isRemoveable)
{
	DPRINTF_FUNC();
	if (isRemoveable) *isRemoveable = false;
	return (kIOReturnSuccess);
}
IOReturn
ZFSDatasetProxy::reportEjectability(bool *isEjectable)
{
	DPRINTF_FUNC();
	if (isEjectable) *isEjectable = false;
	return (kIOReturnSuccess);
}

/* Always report 512b blocksize */
IOReturn
ZFSDatasetProxy::reportBlockSize(UInt64 *blockSize)
{
	DPRINTF_FUNC();
	if (!blockSize)
		return (kIOReturnError);

	*blockSize = ZFS_PROXY_DEV_BSIZE;
	return (kIOReturnSuccess);
}

/* XXX Calculate from dev_bcount, should get size from objset */
/* XXX Can issue message kIOMessageMediaParametersHaveChanged to update */
IOReturn
ZFSDatasetProxy::reportMaxValidBlock(UInt64 *maxBlock)
{
	DPRINTF_FUNC();
	if (!maxBlock)
		return (kIOReturnError);

	// *maxBlock = 0;
	// *maxBlock = ZFS_PROXY_DEV_BCOUNT - 1;
	*maxBlock = _pool_bcount - 1;
	dprintf("maxBlock %llu", *maxBlock);

	return (kIOReturnSuccess);
}

IOReturn
ZFSDatasetProxy::getWriteCacheState(bool *enabled)
{
	dprintf("getCacheState\n");
	if (enabled) *enabled = true;
	return (kIOReturnSuccess);
}

IOReturn
ZFSDatasetProxy::setWriteCacheState(bool enabled)
{
	dprintf("setWriteCache\n");
	return (kIOReturnSuccess);
}
