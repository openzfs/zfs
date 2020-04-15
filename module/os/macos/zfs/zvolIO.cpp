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
 * Copyright (c) 2013-2020, Jorgen Lundman.  All rights reserved.
 */

#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>

#include <sys/zfs_ioctl.h>
#include <sys/zfs_znode.h>
#include <sys/dataset_kstats.h>
#include <sys/zvol.h>
#include <sys/zvol_os.h>
#include <sys/zfs_boot.h>
#include <sys/spa_impl.h>

#include <sys/ZFSPool.h>
#include <sys/zvolIO.h>

/*
 * ZVOL Device
 */

// Define the superclass
#define	super IOBlockStorageDevice

#define	ZVOL_BSIZE	DEV_BSIZE

static const char *ZVOL_PRODUCT_NAME_PREFIX = "ZVOL ";

/* Wrapper for zvol_state pointer to IOKit device */
typedef struct zvol_iokit {
	org_openzfsonosx_zfs_zvol_device *dev;
} zvol_iokit_t;

OSDefineMetaClassAndStructors(org_openzfsonosx_zfs_zvol_device,
    IOBlockStorageDevice)

bool
org_openzfsonosx_zfs_zvol_device::init(zvol_state_t *c_zv,
    OSDictionary *properties)
{
	zvol_iokit_t *iokitdev = NULL;

	dprintf("zvolIO_device:init\n");

	if (!c_zv || c_zv->zv_zso->zvo_iokitdev != NULL) {
		dprintf("zvol %s invalid c_zv\n", __func__);
		return (false);
	}

	if ((iokitdev = (zvol_iokit_t *)kmem_alloc(sizeof (zvol_iokit_t),
	    KM_SLEEP)) == NULL) {
		printf("zvol %s wrapper alloc failed\n", __func__);
		return (false);
	}

	if (super::init(properties) == false) {
		printf("zvol %s super init failed\n", __func__);
		kmem_free(iokitdev, sizeof (zvol_iokit_t));
		return (false);
	}

	/* Store reference to zvol_state_t in the iokitdev */
	zv = c_zv;
	/* Store reference to iokitdev in zvol_state_t */
	iokitdev->dev = this;

	/* Assign to zv once completely initialized */
	zv->zv_zso->zvo_iokitdev = iokitdev;

	/* Apply the name from the full dataset path */
	if (strlen(zv->zv_name) != 0) {
		setName(zv->zv_name);
	}

	return (true);
}

bool
org_openzfsonosx_zfs_zvol_device::attach(IOService* provider)
{
	OSDictionary *protocolCharacteristics = 0;
	OSDictionary *deviceCharacteristics = 0;
	OSDictionary *storageFeatures = 0;
	OSBoolean *unmapFeature = 0;
	const OSSymbol *propSymbol = 0;
	OSString *dataString = 0;
	OSNumber *dataNumber = 0;

	char product_name[strlen(ZVOL_PRODUCT_NAME_PREFIX) + MAXPATHLEN + 1];

	if (!provider) {
		dprintf("ZVOL attach missing provider\n");
		return (false);
	}

	if (super::attach(provider) == false)
		return (false);

	/*
	 * We want to set some additional properties for ZVOLs, in
	 * particular, "Virtual Device", and type "File"
	 * (or is Internal better?)
	 *
	 * Finally "Generic" type.
	 *
	 * These properties are defined in *protocol* characteristics
	 */

	protocolCharacteristics = OSDictionary::withCapacity(3);

	if (!protocolCharacteristics) {
		IOLog("failed to create dict for protocolCharacteristics.\n");
		return (true);
	}

	propSymbol = OSSymbol::withCString(
	    kIOPropertyPhysicalInterconnectTypeVirtual);

	if (!propSymbol) {
		IOLog("could not create interconnect type string\n");
		return (true);
	}
	protocolCharacteristics->setObject(
	    kIOPropertyPhysicalInterconnectTypeKey, propSymbol);

	propSymbol->release();
	propSymbol = 0;

	propSymbol = OSSymbol::withCString(kIOPropertyInterconnectFileKey);
	if (!propSymbol) {
		IOLog("could not create interconnect location string\n");
		return (true);
	}
	protocolCharacteristics->setObject(
	    kIOPropertyPhysicalInterconnectLocationKey, propSymbol);

	propSymbol->release();
	propSymbol = 0;

	setProperty(kIOPropertyProtocolCharacteristicsKey,
	    protocolCharacteristics);

	protocolCharacteristics->release();
	protocolCharacteristics = 0;

	/*
	 * We want to set some additional properties for ZVOLs, in
	 * particular, physical block size (volblocksize) of the
	 * underlying ZVOL, and 'logical' block size presented by
	 * the virtual disk. Also set physical bytes per sector.
	 *
	 * These properties are defined in *device* characteristics
	 */

	deviceCharacteristics = OSDictionary::withCapacity(3);

	if (!deviceCharacteristics) {
		IOLog("failed to create dict for deviceCharacteristics.\n");
		return (true);
	}

	/* Set this device to be an SSD, for priority and VM paging */
	propSymbol = OSSymbol::withCString(
	    kIOPropertyMediumTypeSolidStateKey);
	if (!propSymbol) {
		IOLog("could not create medium type string\n");
		return (true);
	}
	deviceCharacteristics->setObject(kIOPropertyMediumTypeKey,
	    propSymbol);

	propSymbol->release();
	propSymbol = 0;

	/* Set logical block size to ZVOL_BSIZE (512b) */
	dataNumber =	OSNumber::withNumber(ZVOL_BSIZE,
	    8 * sizeof (ZVOL_BSIZE));

	deviceCharacteristics->setObject(kIOPropertyLogicalBlockSizeKey,
	    dataNumber);

	dprintf("logicalBlockSize %llu\n",
	    dataNumber->unsigned64BitValue());

	dataNumber->release();
	dataNumber	= 0;

	/* Set physical block size to match volblocksize property */
	dataNumber =	OSNumber::withNumber(zv->zv_volblocksize,
	    8 * sizeof (zv->zv_volblocksize));

	deviceCharacteristics->setObject(kIOPropertyPhysicalBlockSizeKey,
	    dataNumber);

	dprintf("physicalBlockSize %llu\n",
	    dataNumber->unsigned64BitValue());

	dataNumber->release();
	dataNumber	= 0;

	/* Set physical bytes per sector to match volblocksize property */
	dataNumber =	OSNumber::withNumber((uint64_t)(zv->zv_volblocksize),
	    8 * sizeof (uint64_t));

	deviceCharacteristics->setObject(kIOPropertyBytesPerPhysicalSectorKey,
	    dataNumber);

	dprintf("physicalBytesPerSector %llu\n",
	    dataNumber->unsigned64BitValue());

	dataNumber->release();
	dataNumber	= 0;

	/* Publish the Device / Media name */
	(void) snprintf(product_name, sizeof (product_name), "%s%s",
	    ZVOL_PRODUCT_NAME_PREFIX, zv->zv_name);
	dataString = OSString::withCString(product_name);
	deviceCharacteristics->setObject(kIOPropertyProductNameKey, dataString);
	dataString->release();
	dataString = 0;

	/* Apply these characteristics */
	setProperty(kIOPropertyDeviceCharacteristicsKey,
	    deviceCharacteristics);

	deviceCharacteristics->release();
	deviceCharacteristics	= 0;

	/*
	 * ZVOL unmap support
	 *
	 * These properties are defined in IOStorageFeatures
	 */

	storageFeatures =	OSDictionary::withCapacity(1);
	if (!storageFeatures) {
		IOLog("failed to create dictionary for storageFeatures.\n");
		return (true);
	}

	/* Set unmap feature */
	unmapFeature =	OSBoolean::withBoolean(true);
	storageFeatures->setObject(kIOStorageFeatureUnmap, unmapFeature);
	unmapFeature->release();
	unmapFeature	= 0;

	/* Apply these storage features */
	setProperty(kIOStorageFeaturesKey, storageFeatures);
	storageFeatures->release();
	storageFeatures	= 0;


	/*
	 * Set transfer limits:
	 *
	 *  Maximum transfer size (bytes)
	 *  Maximum transfer block count
	 *  Maximum transfer block size (bytes)
	 *  Maximum transfer segment count
	 *  Maximum transfer segment size (bytes)
	 *  Minimum transfer segment size (bytes)
	 *
	 *  We will need to establish safe defaults for all / per volblocksize
	 *
	 *  Example: setProperty(kIOMinimumSegmentAlignmentByteCountKey, 1, 1);
	 */

	/*
	 * Finally "Generic" type, set as a device property. Tried setting this
	 * to the string "ZVOL" however the OS does not recognize it as a block
	 * storage device. This would probably be possible by extending the
	 * IOBlockStorage Device / Driver relationship.
	 */

	setProperty(kIOBlockStorageDeviceTypeKey,
	    kIOBlockStorageDeviceTypeGeneric);

	return (true);
}

int
org_openzfsonosx_zfs_zvol_device::renameDevice(void)
{
	OSDictionary *deviceDict;
	OSString *nameStr;
	char *newstr;
	int len;

	/* Length of string and null terminating character */
	len = strlen(ZVOL_PRODUCT_NAME_PREFIX) + strlen(zv->zv_name) + 1;
	newstr = (char *)kmem_alloc(len, KM_SLEEP);
	if (!newstr) {
		dprintf("%s string alloc failed\n", __func__);
		return (ENOMEM);
	}

	/* Append prefix and dsl name */
	snprintf(newstr, len, "%s%s", ZVOL_PRODUCT_NAME_PREFIX, zv->zv_name);
	nameStr = OSString::withCString(newstr);
	kmem_free(newstr, len);

	if (!nameStr) {
		dprintf("%s couldn't allocate name string\n", __func__);
		return (ENOMEM);
	}

	/* Fetch current device characteristics dictionary */
	deviceDict = OSDynamicCast(OSDictionary,
	    getProperty(kIOPropertyDeviceCharacteristicsKey));
	if (!deviceDict || (deviceDict =
	    OSDictionary::withDictionary(deviceDict)) == NULL) {
		dprintf("couldn't clone device characteristics\n");
		/* Allocate new dict */
		if (!deviceDict &&
		    (deviceDict = OSDictionary::withCapacity(1)) == NULL) {
			dprintf("%s OSDictionary alloc failed\n", __func__);
			nameStr->release();
			return (ENOMEM);
		}

	}

	/* Add or replace the product name */
	if (deviceDict->setObject(kIOPropertyProductNameKey,
	    nameStr) == false) {
		dprintf("%s couldn't set product name\n", __func__);
		nameStr->release();
		deviceDict->release();
		return (ENXIO);
	}
	nameStr->release();
	nameStr = 0;

	/* Set IORegistry property */
	if (setProperty(kIOPropertyDeviceCharacteristicsKey,
	    deviceDict) == false) {
		dprintf("%s couldn't set IORegistry property\n", __func__);
		deviceDict->release();
		return (ENXIO);
	}
	deviceDict->release();
	deviceDict = 0;

	/* Apply the name from the full dataset path */
	setName(zv->zv_name);

	return (0);
}

int
org_openzfsonosx_zfs_zvol_device::offlineDevice(void)
{
	IOService *client;

	if ((client = this->getClient()) == NULL) {
		return (ENOENT);
	}

	/* Ask IOBlockStorageDevice to offline media */
	if (client->message(kIOMessageMediaStateHasChanged,
	    this, (void *)kIOMediaStateOffline) != kIOReturnSuccess) {
		dprintf("%s failed\n", __func__);
		return (ENXIO);
	}

	return (0);
}

int
org_openzfsonosx_zfs_zvol_device::onlineDevice(void)
{
	IOService *client;

	if ((client = this->getClient()) == NULL) {
		return (ENOENT);
	}

	/* Ask IOBlockStorageDevice to online media */
	if (client->message(kIOMessageMediaStateHasChanged,
	    this, (void *)kIOMediaStateOnline) != kIOReturnSuccess) {
		dprintf("%s failed\n", __func__);
		return (ENXIO);
	}

	return (0);
}

int
org_openzfsonosx_zfs_zvol_device::refreshDevice(void)
{
	IOService *client;

	if ((client = this->getClient()) == NULL) {
		return (ENOENT);
	}

	/* Ask IOBlockStorageDevice to reset the media params */
	if (client->message(kIOMessageMediaParametersHaveChanged,
	    this) != kIOReturnSuccess) {
		dprintf("%s failed\n", __func__);
		return (ENXIO);
	}

	return (0);
}

int
org_openzfsonosx_zfs_zvol_device::getBSDName(void)
{
	IORegistryEntry *ioregdevice = 0;
	OSObject *bsdnameosobj = 0;
	OSString* bsdnameosstr = 0;

	ioregdevice = OSDynamicCast(IORegistryEntry, this);

	if (!ioregdevice)
		return (-1);

	bsdnameosobj = ioregdevice->getProperty(kIOBSDNameKey,
	    gIOServicePlane, kIORegistryIterateRecursively);

	if (!bsdnameosobj)
		return (-1);

	bsdnameosstr = OSDynamicCast(OSString, bsdnameosobj);

	IOLog("zvol: bsd name is '%s'\n",
	    bsdnameosstr->getCStringNoCopy());

	if (!zv)
		return (-1);

	zv->zv_zso->zvo_bsdname[0] = 'r'; // for 'rdiskX'.
	strlcpy(&zv->zv_zso->zvo_bsdname[1],
	    bsdnameosstr->getCStringNoCopy(),
	    sizeof (zv->zv_zso->zvo_bsdname)-1);
	/*
	 * IOLog("name assigned '%s'\n", zv->zv_zso->zvo_bsdname);
	 */

	return (0);
}

void
org_openzfsonosx_zfs_zvol_device::detach(IOService *provider)
{
	super::detach(provider);
}

void
org_openzfsonosx_zfs_zvol_device::clearState(void)
{
	zv = NULL;
}

bool
org_openzfsonosx_zfs_zvol_device::handleOpen(IOService *client,
    IOOptionBits options, void *argument)
{
	IOStorageAccess access = (uintptr_t)argument;
	bool ret = false;
	int openflags = 0;

	if (super::handleOpen(client, options, argument) == false)
		return (false);

	/* Device terminating? */
	if (zv == NULL ||
	    zv->zv_zso == NULL ||
	    zv->zv_zso->zvo_iokitdev == NULL)
		return (false);

	if (access & kIOStorageAccessReaderWriter) {
		openflags = FWRITE | ZVOL_EXCL;
	} else {
		openflags = FREAD;
	}

	/*
	 * Don't use 'zv' until it has been verified by zvol_os_open_zv()
	 * and returned as opened, then it holds an open count and can be
	 * used.
	 */

	if (zvol_os_open_zv(zv, zv->zv_zso->zvo_openflags, 0, NULL) == 0) {
		ret = true;
	}

	if (ret)
		zv->zv_zso->zvo_openflags = openflags;


	dprintf("Open %s (openflags %llx)\n", (ret ? "done" : "failed"),
	    ret ? zv->zv_zso->zvo_openflags : 0);

	if (ret == false)
		super::handleClose(client, options);

	return (ret);
}

void
org_openzfsonosx_zfs_zvol_device::handleClose(IOService *client,
    IOOptionBits options)
{
	super::handleClose(client, options);

	/* Terminating ? */
	if (zv == NULL ||
	    zv->zv_zso == NULL ||
	    zv->zv_zso->zvo_iokitdev == NULL)
		return;

	zvol_os_close_zv(zv, zv->zv_zso->zvo_openflags, 0, NULL);

}

IOReturn
org_openzfsonosx_zfs_zvol_device::doAsyncReadWrite(
    IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
    IOStorageAttributes *attributes, IOStorageCompletion *completion)
{
	IODirection direction;
	IOByteCount actualByteCount;

	// Return errors for incoming I/O if we have been terminated.
	if (isInactive() == true) {
		dprintf("asyncReadWrite notActive fail\n");
		return (kIOReturnNotAttached);
	}

	// These variables are set in zvol_first_open(), which should have been
	// called already.
	if (!zv->zv_dn) {
		dprintf("asyncReadWrite no zvol dnode\n");
		return (kIOReturnNotAttached);
	}

	// Ensure the start block is within the disk capacity.
	if ((block)*(ZVOL_BSIZE) >= zv->zv_volsize) {
		dprintf("asyncReadWrite start block outside volume\n");
		return (kIOReturnBadArgument);
	}

	// Shorten the read, if beyond the end
	if (((block + nblks)*(ZVOL_BSIZE)) > zv->zv_volsize) {
		dprintf("asyncReadWrite block shortening needed\n");
		return (kIOReturnBadArgument);
	}

	// Get the buffer direction, whether this is a read or a write.
	direction = buffer->getDirection();
	if ((direction != kIODirectionIn) && (direction != kIODirectionOut)) {
		dprintf("asyncReadWrite kooky direction\n");
		return (kIOReturnBadArgument);
	}

	// dprintf("%s offset @block %llu numblocks %llu: blksz %u\n",
	//   direction == kIODirectionIn ? "Read" : "Write",
	//  block, nblks, (ZVOL_BSIZE));

	/* Perform the read or write operation through the transport driver. */
	actualByteCount = (nblks*(ZVOL_BSIZE));

	/* Make sure we don't go away while the command is being executed */
	/* Open should be holding a retain */

	struct iovec iov;
	iov.iov_base = (void *)buffer;
	iov.iov_len = actualByteCount;
	zfs_uio_t uio;
	zfs_uio_iovec_func_init(&uio, &iov, 1, block*(ZVOL_BSIZE),
	    (zfs_uio_seg_t)UIO_FUNCSPACE, actualByteCount, 0, zvolIO_strategy);

	if (direction == kIODirectionIn) {
	    zvol_os_read_zv(zv, &uio);
	} else {
		zvol_os_write_zv(zv, &uio);
	}

	if (zfs_uio_resid(&uio) != 0)
		printf("Read/Write operation failed\n");

	// Call the completion function.
	(completion->action)(completion->target, completion->parameter,
	    kIOReturnSuccess, actualByteCount);

	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::doDiscard(UInt64 block, UInt64 nblks)
{
	dprintf("doDiscard called with block, nblks (%llu, %llu)\n",
	    block, nblks);
	uint64_t bytes		= 0;
	uint64_t off		= 0;

	/* Convert block/nblks to offset/bytes */
	off =	block * ZVOL_BSIZE;
	bytes =	nblks * ZVOL_BSIZE;
	dprintf("calling zvol_unmap with offset, bytes (%llu, %llu)\n",
	    off, bytes);

	if (zvol_os_unmap(zv, off, bytes) == 0)
		return (kIOReturnSuccess);
	else
		return (kIOReturnError);
}


IOReturn
org_openzfsonosx_zfs_zvol_device::doUnmap(IOBlockStorageDeviceExtent *extents,
    UInt32 extentsCount, UInt32 options = 0)
{
	UInt32 i = 0;
	IOReturn result;

	dprintf("doUnmap called with (%u) extents and options (%u)\n",
	    (uint32_t)extentsCount, (uint32_t)options);

	if (options > 0 || !extents) {
		return (kIOReturnUnsupported);
	}

	for (i = 0; i < extentsCount; i++) {

		result = doDiscard(extents[i].blockStart,
		    extents[i].blockCount);

		if (result != kIOReturnSuccess) {
			return (result);
		}
	}

	return (kIOReturnSuccess);
}

UInt32
org_openzfsonosx_zfs_zvol_device::doGetFormatCapacities(UInt64* capacities,
    UInt32 capacitiesMaxCount) const
{
	dprintf("formatCap\n");

	/*
	 * Ensure that the array is sufficient to hold all our formats
	 * (we require one element).
	 */
	if ((capacities != NULL) && (capacitiesMaxCount < 1))
		return (0);
		/* Error, return an array size of 0. */

	/*
	 * The caller may provide a NULL array if it wishes to query the number
	 * of formats that we support.
	 */
	if (capacities != NULL)
		capacities[0] = zv->zv_volsize;

	dprintf("returning capacity[0] size %llu\n", zv->zv_volsize);

	return (1);
}

char *
org_openzfsonosx_zfs_zvol_device::getProductString(void)
{
	dprintf("getProduct %p\n", zv);

	if (zv)
		return (zv->zv_name);

	return ((char *)"ZVolume");
}

IOReturn
org_openzfsonosx_zfs_zvol_device::reportBlockSize(UInt64 *blockSize)
{
	dprintf("reportBlockSize %llu\n", *blockSize);

	if (blockSize) *blockSize = (ZVOL_BSIZE);

	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::reportMaxValidBlock(UInt64 *maxBlock)
{
	dprintf("reportMaxValidBlock %llu\n", *maxBlock);

	if (maxBlock) *maxBlock = ((zv->zv_volsize / (ZVOL_BSIZE)) - 1);

	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::reportMediaState(bool *mediaPresent,
    bool *changedState)
{
	dprintf("reportMediaState\n");
	if (mediaPresent) *mediaPresent = true;
	if (changedState) *changedState = false;
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::reportPollRequirements(bool *pollRequired,
    bool *pollIsExpensive)
{
	dprintf("reportPollReq\n");
	if (pollRequired) *pollRequired = false;
	if (pollIsExpensive) *pollIsExpensive = false;
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::reportRemovability(bool *isRemovable)
{
	dprintf("reportRemova\n");
	if (isRemovable) *isRemovable = false;
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::doEjectMedia(void)
{
	dprintf("ejectMedia\n");
/* XXX */
	// Only 10.6 needs special work to eject
	// if ((version_major == 10) && (version_minor == 8))
	//	destroyBlockStorageDevice(zvol);
	// }

	return (kIOReturnError);
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::doFormatMedia(UInt64 byteCapacity)
{
	dprintf("doFormat\n");
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::doLockUnlockMedia(bool doLock)
{
	dprintf("doLockUnlock\n");
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::doSynchronizeCache(void)
{
	dprintf("doSync\n");
	if (zv && zv->zv_zilog) {
		zil_commit(zv->zv_zilog, ZVOL_OBJ);
	}
	return (kIOReturnSuccess);
}

char *
org_openzfsonosx_zfs_zvol_device::getVendorString(void)
{
	dprintf("getVendor\n");
	return ((char *)"ZVOL");
}

char *
org_openzfsonosx_zfs_zvol_device::getRevisionString(void)
{
	dprintf("getRevision\n");
	return ((char *)ZFS_META_VERSION);
}

char *
org_openzfsonosx_zfs_zvol_device::getAdditionalDeviceInfoString(void)
{
	dprintf("getAdditional\n");
	return ((char *)"ZFS Volume");
}

IOReturn
org_openzfsonosx_zfs_zvol_device::reportEjectability(bool *isEjectable)
{
	dprintf("reportEjecta\n");
	/*
	 * Which do we prefer? If you eject it, you can't get volume back until
	 * you import it again.
	 */

	if (isEjectable) *isEjectable = false;
	return (kIOReturnSuccess);
}

/* XXX deprecated function */
IOReturn
org_openzfsonosx_zfs_zvol_device::reportLockability(bool *isLockable)
{
	dprintf("reportLocka\n");
	if (isLockable) *isLockable = true;
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::reportWriteProtection(bool *isWriteProtected)
{
	dprintf("reportWritePro: %d\n", *isWriteProtected);

	if (!isWriteProtected)
		return (kIOReturnSuccess);

	if (zv && (zv->zv_flags & ZVOL_RDONLY))
		*isWriteProtected = true;
	else
		*isWriteProtected = false;

	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::getWriteCacheState(bool *enabled)
{
	dprintf("getCacheState\n");
	if (enabled) *enabled = true;
	return (kIOReturnSuccess);
}

IOReturn
org_openzfsonosx_zfs_zvol_device::setWriteCacheState(bool enabled)
{
	dprintf("setWriteCache\n");
	return (kIOReturnSuccess);
}

extern "C" {

/* C interfaces */
int
zvolCreateNewDevice(zvol_state_t *zv)
{
	org_openzfsonosx_zfs_zvol_device *zvol;
	ZFSPool *pool_proxy;
	spa_t *spa;
	dprintf("%s\n", __func__);

	/* We must have a valid zvol_state_t */
	if (!zv || !zv->zv_objset) {
		dprintf("%s missing zv or objset\n", __func__);
		return (EINVAL);
	}

	/* We need the spa to get the pool proxy */
	if ((spa = dmu_objset_spa(zv->zv_objset)) == NULL) {
		dprintf("%s couldn't get spa\n", __func__);
		return (EINVAL);
	}
	if (spa->spa_iokit_proxy == NULL ||
	    (pool_proxy = spa->spa_iokit_proxy->proxy) == NULL) {
		dprintf("%s missing IOKit pool proxy\n", __func__);
		return (EINVAL);
	}

	zvol = new org_openzfsonosx_zfs_zvol_device;

	/* Validate creation, initialize and attach */
	if (!zvol || zvol->init(zv) == false ||
	    zvol->attach(pool_proxy) == false) {
		dprintf("%s device creation failed\n", __func__);
		if (zvol) zvol->release();
		return (ENOMEM);
	}
	/* Start the service */
	if (zvol->start(pool_proxy) == false) {
		dprintf("%s device start failed\n", __func__);
		zvol->detach(pool_proxy);
		zvol->release();
		return (ENXIO);
	}

	/* Open pool_proxy provider */
	if (pool_proxy->open(zvol) == false) {
		dprintf("%s open provider failed\n", __func__);
		zvol->stop(pool_proxy);
		zvol->detach(pool_proxy);
		zvol->release();
		return (ENXIO);
	}
	/* Is retained by provider */
	zvol->release();
	zvol = 0;

	return (0);
}


/*
 * Sometimes we need to wait for zvol name to show up.
 * 0 means success - if ret_service is given, service is returned.
 * Caller should release()
 * > 0 means error of some kind
 * -1 means timeout.
 */
static int
zvolWaitForName(char *name, char *vendor, uint64_t timeout,
    IOService **ret_service)
{
    OSDictionary *matching;
    IOService *service = 0;
	OSString *nameStr = 0;
	char str[MAXNAMELEN];

	snprintf(str, MAXNAMELEN, "%s %s Media",
	    vendor, name);
	if ((nameStr = OSString::withCString(str)) == NULL) {
		dprintf("%s problem with name string\n", __func__);
		return (ENOMEM);
	}

	matching = IOService::serviceMatching("IOMedia");
	if (!matching || !matching->setObject(gIONameMatchKey, nameStr)) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		nameStr->release();
		return (ENOMEM);
	}

	/* Wait for upper layer BSD client */
	printf("%s waiting for IOMedia\n", __func__);

	/* Wait for up to `timeout` */
	service = IOService::waitForMatchingService(matching, timeout);
	dprintf("%s %s service\n", __func__, (service ? "got" : "no"));

	nameStr->release();
	matching->release();

	if (!service)
		return (SET_ERROR(-1));

	if (ret_service != 0)
		*ret_service = service;
	else
		service->release();

	return (0);
}

int
zvolRegisterDevice(zvol_state_t *zv)
{
	org_openzfsonosx_zfs_zvol_device *zvol;
	IOService *service = 0;
	IOMedia *media = 0;
	OSString *bsdName = 0;
	int ret = ENOENT;

	if (!zv || !zv->zv_zso->zvo_iokitdev || zv->zv_name[0] == 0) {
		dprintf("%s missing zv, iokitdev, or name\n", __func__);
		return (SET_ERROR(EINVAL));
	}

	if ((zvol = zv->zv_zso->zvo_iokitdev->dev) == NULL) {
		dprintf("%s couldn't get zvol device\n", __func__);
		return (SET_ERROR(EINVAL));
	}

	if (!zvol->getVendorString()) {
		return (SET_ERROR(EINVAL));
	}

	/* Register device for service matching */
	zvol->registerService(kIOServiceAsynchronous);

	if (zvolWaitForName(zv->zv_name, zvol->getVendorString(),
	    (5ULL * kSecondScale), &service) != 0) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		return (SET_ERROR(ENOMEM));
	}

	if (!service) {
		dprintf("%s couldn't get matching service\n", __func__);
		return (SET_ERROR(ENOENT));
	}

	dprintf("%s casting to IOMedia\n", __func__);
	media = OSDynamicCast(IOMedia, service);

	if (!media) {
		dprintf("%s no IOMedia\n", __func__);
		service->release();
		return (SET_ERROR(ENOENT));
	}

	dprintf("%s getting IOBSDNameKey\n", __func__);
	bsdName = OSDynamicCast(OSString,
	    media->getProperty(kIOBSDNameKey));

	if (bsdName) {
		const char *str = bsdName->getCStringNoCopy();
		dprintf("%s Got bsd name [%s]\n",
		    __func__, str);
		zv->zv_zso->zvo_bsdname[0] = 'r';
		snprintf(zv->zv_zso->zvo_bsdname+1,
		    sizeof (zv->zv_zso->zvo_bsdname)-1,
		    "%s", str);
		dprintf("%s zvol bsdname set to %s\n", __func__,
		    zv->zv_zso->zvo_bsdname);
		zvol_add_symlink(zv, zv->zv_zso->zvo_bsdname+1,
		    zv->zv_zso->zvo_bsdname);
		ret = 0;
	} else {
		dprintf("%s couldn't get BSD Name\n", __func__);
	}

	/* Release retain held by waitForMatchingService */
	service->release();

	dprintf("%s complete\n", __func__);
	return (ret);
}

/* Struct passed in will be freed before returning */
void *
zvolRemoveDevice(zvol_state_t *zv)
{
	zvol_iokit_t *iokitdev = zv->zv_zso->zvo_iokitdev;
	org_openzfsonosx_zfs_zvol_device *zvol;
	dprintf("%s\n", __func__);

	if (!iokitdev) {
		dprintf("%s missing argument\n", __func__);
		return (NULL);
	}

	zvol = iokitdev->dev;

	/* Free the wrapper struct */
	kmem_free(iokitdev, sizeof (zvol_iokit_t));

	if (zvol == NULL) {
		dprintf("%s couldn't get IOKit handle\n", __func__);
		return (NULL);
	}

	/* Mark us as terminating */
	zvol->clearState();

	return (zvol);
}

/*
 * zvolRemoveDevice continued..
 * terminate() will block and we can deadlock, so it is issued as a
 * separate thread. Done from zvol_os.c as it is easier in C.
 */
int
zvolRemoveDeviceTerminate(void *arg)
{
	org_openzfsonosx_zfs_zvol_device *zvol =
	    (org_openzfsonosx_zfs_zvol_device *)arg;

	IOLog("zvolRemoveDeviceTerminate\n");

	/* Terminate */
	if (zvol->terminate(kIOServiceTerminate|kIOServiceSynchronous|
	    kIOServiceRequired) == false) {
		IOLog("%s terminate failed\n", __func__);
	}

	return (0);
}

/* Called with zv->zv_name already updated */
int
zvolRenameDevice(zvol_state_t *zv)
{
	org_openzfsonosx_zfs_zvol_device *zvol = NULL;
	int error;

	if (!zv || strnlen(zv->zv_name, 1) == 0) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	if ((zvol = zv->zv_zso->zvo_iokitdev->dev) == NULL) {
		dprintf("%s couldn't get zvol device\n", __func__);
		return (EINVAL);
	}

	/* Set IORegistry name and property */
	if ((error = zvol->renameDevice()) != 0) {
		dprintf("%s renameDevice error %d\n", __func__, error);
		return (error);
	}

	/*
	 * XXX This works, but if there is a volume mounted on
	 * the zvol at the time it is uncleanly ejected.
	 * We just need to add diskutil unmount to `zfs rename`,
	 * like zpool export.
	 */
	/* Inform clients of this device that name has changed */
	if (zvolWaitForName(zv->zv_name, zvol->getVendorString(),
	    (2ULL * kSecondScale), NULL) != 0)
		dprintf("wait for rename failed.\n");


	if (zvol->offlineDevice() != 0 ||
	    zvol->onlineDevice() != 0) {
		dprintf("%s media reset failed\n", __func__);
		return (ENXIO);
	}

	return (0);
}

/* Called with zvol volsize already updated */
int
zvolSetVolsize(zvol_state_t *zv)
{
	org_openzfsonosx_zfs_zvol_device *zvol;
	int error;

	dprintf("%s\n", __func__);

	if (!zv || !zv->zv_zso->zvo_iokitdev) {
		dprintf("%s invalid zvol\n", __func__);
		return (EINVAL);
	}

	/* Cast to correct type */
	if ((zvol = zv->zv_zso->zvo_iokitdev->dev) == NULL) {
		dprintf("%s couldn't cast IOKit handle\n", __func__);
		return (ENXIO);
	}
	/*
	 * XXX This works fine, even if volume is mounted,
	 * but only tested expanding the zvol and only with
	 * GPT/APM/MBR partition map (not volume on whole-zvol).
	 */
	/* Inform clients of this device that size has changed */
	if ((error = zvol->refreshDevice()) != 0) {
		dprintf("%s refreshDevice error %d\n", __func__, error);
		return (error);
	}

	return (0);
}


size_t zvolIO_strategy(char *addr, uint64_t offset,
    size_t len, zfs_uio_rw_t rw, const void *privptr)
{
	IOMemoryDescriptor *iomem = (IOMemoryDescriptor *)privptr;

	if (rw == UIO_READ)
		return (iomem->writeBytes(offset, (void *)addr, len));
	else
		return (iomem->readBytes(offset, (void *)addr, len));
}

boolean_t
zvol_os_is_zvol_impl(const char *path)
{
	OSDictionary *matchDict = 0;
	OSString *bsdName = NULL;
	OSString *uuid = NULL;
	const char *substr = 0;
	bool ret = B_FALSE;

	dprintf("%s: processing '%s'\n", __func__, path);

	/* Validate path */
	if (path == 0 || strlen(path) <= 1) {
		dprintf("%s no path provided\n", __func__);
		return (SET_ERROR(ret));
	}
	/* Translate /dev/diskN and InvariantDisks paths */
	if (strncmp(path, "/dev/", 5) != 0 &&
	    strncmp(path, "/var/run/disk/by-id/", 20) != 0 &&
	    strncmp(path, "/private/var/run/disk/by-id/", 28) != 0) {
		dprintf("%s Unrecognized path %s\n", __func__, path);
		return (SET_ERROR(ret));
	}

	/* Validate path and alloc bsdName */
	if (strncmp(path, "/dev/", 5) == 0) {
		char disk[MAXPATHLEN];

	    /* substr starts after '/dev/' */
		substr = path + 5;

		strlcpy(disk, substr, MAXPATHLEN);

		/* For zvol_is_zvol, we want root disk, not slice. */
		if (tolower(disk[0]) == 'd' &&
		    tolower(disk[1]) == 'i' &&
		    tolower(disk[2]) == 's' &&
		    tolower(disk[3]) == 'k') {
		char *r = &disk[4];
		while (isdigit(*r)) r++;
		if (tolower(*r) == 's')
			*r = 0;
		}

		/* Get diskN from /dev/diskN or /dev/rdiskN */
		if (strncmp(disk, "disk", 4) == 0) {
			bsdName = OSString::withCString(disk);
		} else if (strncmp(disk, "rdisk", 5) == 0) {
			bsdName = OSString::withCString(disk + 1);
		}
	} else if (strncmp(path, "/var/run/disk/by-id/", 20) == 0 ||
	    strncmp(path, "/private/var/run/disk/by-id/", 28) == 0) {
		/* InvariantDisks paths */

		/* substr starts after '/by-id/' */
		substr = path + 20;
		if (strncmp(path, "/private", 8) == 0) substr += 8;

		/* Handle media UUID, skip volume UUID or device GUID */
		if (strncmp(substr, "media-", 6) == 0) {
			/* Lookup IOMedia with UUID */
			uuid = OSString::withCString(substr+strlen("media-"));
		} else if (strncmp(substr, "volume-", 7) == 0) {
			/*
			 * volume-UUID is specified by DiskArbitration
			 * when a Filesystem bundle is able to probe
			 * the media and retrieve/generate a UUID for
			 * it's contents.
			 * So while we could use this and have zfs.util
			 * probe for vdev GUID (and pool GUID) and
			 * generate a UUID, we would need to do the same
			 * here to find the disk, possibly probing
			 * devices to get the vdev GUID in the process.
			 */
			dprintf("%s Unsupported volume-UUID path %s\n",
			    __func__, path);
		} else if (strncmp(substr, "device-", 7) == 0) {
			/* Lookup IOMedia with device GUID */
			/*
			 * XXX Not sure when this is used, no devices
			 * seem to be presented this way.
			 */
			dprintf("%s Unsupported device-GUID path %s\n",
			    __func__, path);
		} else {
			dprintf("%s unrecognized path %s\n", __func__, path);
		}
		/* by-path and by-serial are handled separately */
	}

	if (!bsdName && !uuid) {
		dprintf("%s Invalid path %s\n", __func__, path);
		return (SET_ERROR(ret));
	}

	/* Match on IOMedia by BSD disk name */
	matchDict = IOService::serviceMatching("IOMedia");
	if (!matchDict) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		if (bsdName) bsdName->release();
		if (uuid) uuid->release();
		return (SET_ERROR(ret));
	}
	if (bsdName) {

		matchDict->setObject(kIOBSDNameKey, bsdName);

	} else if (uuid) {
		if (matchDict->setObject(kIOMediaUUIDKey, uuid) == false) {
			dprintf("%s couldn't setup UUID matching"
			    " dictionary\n", __func__);
			matchDict->release();
			matchDict = 0;
		}
	} else {
		dprintf("%s missing matching property\n", __func__);
		matchDict->release();
		matchDict = 0;
	}

	if (bsdName) bsdName->release();
	if (uuid) uuid->release();

	if (matchDict == 0)
		return (SET_ERROR(ret));

	OSIterator *iter = 0;
	OSObject *obj = 0;
	IOMedia *media = 0;

	iter = IOService::getMatchingServices(matchDict);

	matchDict->release();

	if (!iter) {
		dprintf("%s No iterator from getMatchingServices\n",
		    __func__);
		return (SET_ERROR(ret));
	}

	/* Get first object from iterator */
	while ((obj = iter->getNextObject()) != NULL) {
		if ((media = OSDynamicCast(IOMedia, obj)) == NULL) {
			obj = 0;
			continue;
		}
		if (media->isFormatted() == false) {
			obj = 0;
			media = 0;
			continue;
		}

		media->retain();
		break;
	}

	if (!media) {
		dprintf("%s no match found\n", __func__);
		iter->release();
		return (SET_ERROR(ret));
	}

	iter->release();
	iter = 0;

	// IOMedia from here on out.

	const char *name;

	name = media->getName();

	if (strncmp(name, ZVOL_PRODUCT_NAME_PREFIX,
	    strlen(ZVOL_PRODUCT_NAME_PREFIX)) == 0)
		ret = B_TRUE;

	media->release();

	return (SET_ERROR(ret));
}


} /* extern "C" */
