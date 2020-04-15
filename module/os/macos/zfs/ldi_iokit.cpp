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
 * Copyright (c) 1994, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */
/*
 * Copyright (c) 2015, Evan Susarret.  All rights reserved.
 */
/*
 * Portions of this document are copyright Oracle and Joyent.
 * OS X implementation of ldi_ named functions for ZFS written by
 * Evan Susarret in 2015.
 */

/* Quiet some noisy build warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winconsistent-missing-override"
#pragma GCC diagnostic ignored "-Wdeprecated-register"

/*
 * Apple IOKit (c++)
 */
#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>

/*
 * ZFS internal
 */
#include <sys/zfs_context.h>

/*
 * LDI Includes
 */
#include <sys/ldi_impl_osx.h>


/* Debug prints */

/* Attach created IOService objects to the IORegistry under ZFS. */
// #define	LDI_IOREGISTRY_ATTACH

/*
 * Globals
 */
static IOService		*ldi_zfs_handle;

/* Exposed to c callers */
extern "C" {

struct _handle_iokit {
	IOMedia			*media;
	IOService		*client;
};	/* 16b */

struct _handle_notifier {
	IONotifier		*obj;
};	/* 8b */

#define	LH_MEDIA(lhp)		lhp->lh_tsd.iokit_tsd->media
#define	LH_CLIENT(lhp)		lhp->lh_tsd.iokit_tsd->client
#define	LH_NOTIFIER(lhp)	lhp->lh_notifier->obj

void
handle_free_iokit(struct ldi_handle *lhp) {
	if (!lhp) {
		dprintf("%s missing lhp\n", __func__);
		return;
	}

	if (!lhp->lh_tsd.iokit_tsd) {
		dprintf("%s missing iokit_tsd\n", __func__);
		return;
	}

	/* Free IOService client */
	if (handle_free_ioservice(lhp) != 0) {
		dprintf("%s lhp %p client %s\n",
		    __func__, lhp, "couldn't be removed");
	}

	kmem_free(lhp->lh_tsd.iokit_tsd, sizeof (struct _handle_iokit));
	lhp->lh_tsd.iokit_tsd = 0;
}

/* Returns handle with lock still held */
struct ldi_handle *
handle_alloc_iokit(dev_t device, int fmode)
{
	struct ldi_handle *lhp, *retlhp;

	/* Search for existing handle */
	if ((retlhp = handle_find(device, fmode, B_TRUE)) != NULL) {
		dprintf("%s found handle before alloc\n", __func__);
		return (retlhp);
	}

	/* Allocate an LDI IOKit handle */
	if ((lhp = handle_alloc_common(LDI_TYPE_IOKIT, device,
	    fmode)) == NULL) {
		dprintf("%s couldn't allocate handle\n", __func__);
		return (NULL);
	}

	/* Allocate and clear type-specific device data */
	lhp->lh_tsd.iokit_tsd = (struct _handle_iokit *)kmem_alloc(
	    sizeof (struct _handle_iokit), KM_SLEEP);
	LH_MEDIA(lhp) = 0;
	LH_CLIENT(lhp) = 0;

	/* Allocate an IOService client for open/close */
	if (handle_alloc_ioservice(lhp) != 0) {
		dprintf("%s couldn't allocate IOService client\n", __func__);
		handle_release(lhp);
		return (NULL);
	}

	/* Add the handle to the list, or return match */
	if ((retlhp = handle_add(lhp)) == NULL) {
		dprintf("%s handle_add failed\n", __func__);
		handle_release(lhp);
		return (NULL);
	}

	/* Check if new or found handle was returned */
	if (retlhp != lhp) {
		dprintf("%s found handle after alloc\n", __func__);
		handle_release(lhp);
		lhp = 0;
	}

	return (retlhp);
}

int
handle_free_ioservice(struct ldi_handle *lhp)
{
	/* Validate handle pointer */
	ASSERT3U(lhp, !=, NULL);
#ifdef DEBUG
	if (!lhp) {
		dprintf("%s missing handle\n", __func__);
		return (EINVAL);
	}
	if (!LH_CLIENT(lhp)) {
		dprintf("%s missing client\n", __func__);
		return (ENODEV);
	}
#endif

#ifdef LDI_IOREGISTRY_ATTACH
	/* Detach client from ZFS in IORegistry */
	LH_CLIENT(lhp)->detach(ldi_zfs_handle);
#endif

	LH_CLIENT(lhp)->stop(ldi_zfs_handle);
	LH_CLIENT(lhp)->release();
	LH_CLIENT(lhp) = 0;

	return (0);
}

int
handle_alloc_ioservice(struct ldi_handle *lhp)
{
	IOService *client;

	/* Validate handle pointer */
	ASSERT3U(lhp, !=, NULL);
	if (lhp == NULL) {
		dprintf("%s missing handle\n", __func__);
		return (EINVAL);
	}

	/* Allocate and init an IOService client for open/close */
	if ((client = new IOService) == NULL) {
		dprintf("%s couldn't allocate new IOService\n", __func__);
		return (ENOMEM);
	}
	if (client->init(0) != true) {
		dprintf("%s IOService init failed\n", __func__);
		client->release();
		return (ENOMEM);
	}

#ifdef LDI_IOREGISTRY_ATTACH
	/* Attach client to ZFS in IORegistry */
	if (client->attach(ldi_zfs_handle) != true) {
		dprintf("%s IOService attach failed\n", __func__);
		client->release();
		return (ENOMEM);
	}
#endif

	/* Start service */
	if (client->start(ldi_zfs_handle) != true) {
		dprintf("%s IOService attach failed\n", __func__);
		/* Detach client from ZFS in IORegistry */
#ifdef LDI_IOREGISTRY_ATTACH
		client->detach(ldi_zfs_handle);
#endif
		client->release();
		return (ENOMEM);
	}

	LH_CLIENT(lhp) = client;
	return (0);
}

/* Set status to Offline and post event */
static bool
handle_media_terminate_cb(void* target, void* refCon,
    IOService* newService, IONotifier* notifier)
{
	struct ldi_handle *lhp = (struct ldi_handle *)refCon;

#ifdef DEBUG
	if (!lhp) {
		dprintf("%s missing refCon ldi_handle\n", __func__);
		return (false);
	}
#endif

	/* Take hold on handle to prevent removal */
	handle_hold(lhp);

	dprintf("%s setting lhp %p to Offline status\n", __func__, lhp);
	if (handle_status_change(lhp, LDI_STATUS_OFFLINE) != 0) {
		dprintf("%s handle_status_change failed\n", __func__);
		handle_release(lhp);
		return (false);
	}

	handle_release(lhp);
	return (true);
}

int
handle_close_iokit(struct ldi_handle *lhp)
{
#ifdef DEBUG
	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_type, ==, LDI_TYPE_IOKIT);
	ASSERT3U(lhp->lh_status, ==, LDI_STATUS_CLOSING);

	/* Validate IOMedia and IOService client */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp)) ||
	    !OSDynamicCast(IOService, LH_CLIENT(lhp))) {
		dprintf("%s invalid IOMedia or client\n", __func__);
		return (ENODEV);
	}
#endif /* DEBUG */

	LH_MEDIA(lhp)->close(LH_CLIENT(lhp));
	LH_MEDIA(lhp) = 0;
	return (0);
}

static int
handle_open_iokit(struct ldi_handle *lhp, IOMedia *media)
{
#ifdef DEBUG
	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(media, !=, NULL);
	ASSERT3U(lhp->lh_type, ==, LDI_TYPE_IOKIT);
	ASSERT3U(lhp->lh_status, ==, LDI_STATUS_OPENING);

	/* Validate IOMedia and IOService client */
	if (!OSDynamicCast(IOMedia, media) ||
	    !OSDynamicCast(IOService, LH_CLIENT(lhp))) {
		dprintf("%s invalid IOMedia or client\n", __func__);
		return (ENODEV);
	}
#endif /* DEBUG */
	/* Retain until open or error */
	media->retain();

	/*
	 * If read/write mode is requested, check that the
	 * device is actually writeable.
	 */
	if (lhp->lh_fmode & FWRITE && media->isWritable() == false) {
		dprintf("%s read-write requested on %s\n",
		    __func__, "read-only IOMedia");
		media->release();
		return (EPERM);
	}

	/* Call open with the IOService client handle */
	if (media->IOMedia::open(LH_CLIENT(lhp), 0,
	    (lhp->lh_fmode & FWRITE ?  kIOStorageAccessReaderWriter :
	    kIOStorageAccessReader)) == false) {
		dprintf("%s IOMedia->open failed\n", __func__);
		media->release();
		return (EIO);
	}
	media->release();

	/* Assign IOMedia device */
	LH_MEDIA(lhp) = media;
	return (0);
}

int
handle_get_size_iokit(struct ldi_handle *lhp, uint64_t *dev_size)
{
	if (!lhp || !dev_size) {
		dprintf("%s missing lhp or dev_size\n", __func__);
		return (EINVAL);
	}

#ifdef DEBUG
	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		dprintf("%s no IOMedia\n", __func__);
		return (ENODEV);
	}
#endif

	*dev_size = LH_MEDIA(lhp)->getSize();
	if (*dev_size == 0) {
		dprintf("%s %s\n", __func__,
		    "IOMedia getSize returned 0");
		return (EINVAL);
	}

	return (0);
}

int
handle_get_dev_path_iokit(struct ldi_handle *lhp,
    char *path, int len)
{
	int retlen = len;

	if (!lhp || !path || len == 0) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

#ifdef DEBUG
	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		dprintf("%s no IOMedia\n", __func__);
		return (ENODEV);
	}
#endif

	if (LH_MEDIA(lhp)->getPath(path, &retlen, gIODTPlane) == false) {
		dprintf("%s getPath failed\n", __func__);
		return (EIO);
	}

dprintf("%s got path [%s]\n", __func__, path);
	return (0);
}

int handle_get_bootinfo_iokit(struct ldi_handle *lhp,
    struct io_bootinfo *bootinfo)
{
	int error = 0;

	if (!lhp || !bootinfo) {
		dprintf("%s missing argument\n", __func__);
printf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	if ((error = handle_get_size_iokit(lhp,
	    &bootinfo->dev_size)) != 0 ||
	    (error = handle_get_dev_path_iokit(lhp, bootinfo->dev_path,
	    sizeof (bootinfo->dev_path))) != 0) {
		dprintf("%s get size or dev_path error %d\n",
		    __func__, error);
	}

	return (error);
}

int
handle_sync_iokit(struct ldi_handle *lhp)
{
#ifdef DEBUG
	/* Validate IOMedia and client */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp)) ||
	    !OSDynamicCast(IOService, LH_CLIENT(lhp))) {
		dprintf("%s invalid IOMedia or client\n", __func__);
		return (ENODEV);
	}
#endif

#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	/* from module/os/macos/zfs/zfs_vfsops.c */
	extern uint64_t zfs_iokit_sync_paranoia;
	/*
	 * Issue device sync
	 *
	 * We can try to issue a Barrier synch here, which is likely to be
	 * faster, but also is not supported by all devices.
	 *
	 */
	IOStorageSynchronizeOptions synctype = (zfs_iokit_sync_paranoia != 0)
	    ? kIOStorageSynchronizeOptionNone
	    : kIOStorageSynchronizeOptionBarrier;
	IOReturn ret = LH_MEDIA(lhp)->synchronize(LH_CLIENT(lhp),
	    0, 0, synctype);
	if (ret !=  kIOReturnSuccess) {
		printf("%s %s %d %s\n", __func__,
		    "IOMedia synchronizeCache (with write barrier) failed",
		    ret, "(see IOReturn.h)\n");
		return (ENOTSUP);
	}
#else
	/* Issue device sync */
	if (LH_MEDIA(lhp)->synchronizeCache(LH_CLIENT(lhp)) !=
	    kIOReturnSuccess) {
		printf("%s %s\n", __func__,
		    "IOMedia synchronizeCache failed");
		return (ENOTSUP);
	}
#endif

	/* Success */
	return (0);
}

static dev_t
dev_from_media(IOMedia *media)
{
	OSObject *property;
	OSNumber *number;
	uint32_t major, minor;
	dev_t device = 0;

	/* Validate media */
	if (!media || !OSDynamicCast(IOMedia, media)) {
		dprintf("%s no device\n", __func__);
		return (0);
	}
	media->retain();

	/* Get device major */
	if (NULL == (property = media->getProperty(kIOBSDMajorKey,
	    gIOServicePlane, kIORegistryIterateRecursively)) ||
	    NULL == (number = OSDynamicCast(OSNumber, property))) {
		dprintf("%s couldn't get BSD major\n", __func__);
		media->release();
		return (0);
	}
	major = number->unsigned32BitValue();
	number = NULL;
	property = NULL;

	/* Get device minor */
	if (NULL == (property = media->getProperty(kIOBSDMinorKey,
	    gIOServicePlane, kIORegistryIterateRecursively)) ||
	    NULL == (number = OSDynamicCast(OSNumber, property))) {
		dprintf("%s couldn't get BSD major\n", __func__);
		media->release();
		return (0);
	}
	minor = number->unsigned32BitValue();
	number = NULL;
	property = NULL;

	/* Cleanup */
	media->release();
	media = NULL;

	device = makedev(major, minor);

	/* Return 0 or valid dev_t */
	return (device);
}

/* Returns NULL or dictionary with a retain count */
static OSDictionary *
media_matchdict_from_dev(dev_t device)
{
	OSDictionary *matchDict;
	OSNumber *majorNum, *minorNum;

	/* Validate dev_t */
	if (device == 0) {
		dprintf("%s no dev_t provided\n", __func__);
		return (NULL);
	}

	/* Allocate OSNumbers for BSD major and minor (32-bit) */
	if (NULL == (majorNum = OSNumber::withNumber(major(device), 32)) ||
	    NULL == (minorNum = OSNumber::withNumber(minor(device), 32))) {
		dprintf("%s couldn't alloc major/minor as OSNumber\n",
		    __func__);
		if (majorNum) {
			majorNum->release();
		}
		return (NULL);
	}

	/* Match on IOMedia */
	if (NULL == (matchDict = IOService::serviceMatching("IOMedia")) ||
	    !(matchDict->setObject(kIOBSDMajorKey, majorNum)) ||
	    !(matchDict->setObject(kIOBSDMinorKey, minorNum))) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		if (matchDict) {
			matchDict->release();
		}
		majorNum->release();
		minorNum->release();
		return (NULL);
	}
	majorNum->release();
	minorNum->release();

	/* Return NULL or valid OSDictionary with retain count */
	return (matchDict);
}

/* Returns NULL or dictionary with a retain count */
/*
 * media_matchdict_from_path
 * translate from paths of the form /dev/diskNsN
 * or /private/var/run/disk/by-id/media-<UUID> to a matching
 * dictionary.
 */
static OSDictionary *
media_matchdict_from_path(const char *path)
{
	OSDictionary *matchDict = 0;
	OSString *bsdName = NULL;
	OSString *uuid = NULL;
	const char *substr = 0;
	bool ret;

	/* Validate path */
	if (path == 0 || strlen(path) <= 1) {
		dprintf("%s no path provided\n", __func__);
		return (NULL);
	}
	/* Translate /dev/diskN and InvariantDisks paths */
	if (strncmp(path, "/dev/", 5) != 0 &&
	    strncmp(path, "/var/run/disk/by-id/", 20) != 0 &&
	    strncmp(path, "/private/var/run/disk/by-id/", 28) != 0) {
		dprintf("%s Unrecognized path %s\n", __func__, path);
		return (NULL);
	}

	/* Validate path and alloc bsdName */
	if (strncmp(path, "/dev/", 5) == 0) {

		/* substr starts after '/dev/' */
		substr = path + 5;
		/* Get diskN from /dev/diskN or /dev/rdiskN */
		if (strncmp(substr, "disk", 4) == 0) {
			bsdName = OSString::withCString(substr);
		} else if (strncmp(substr, "rdisk", 5) == 0) {
			bsdName = OSString::withCString(substr + 1);
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
		return (NULL);
	}

	/* Match on IOMedia by BSD disk name */
	matchDict = IOService::serviceMatching("IOMedia");
	if (!matchDict) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		if (bsdName) bsdName->release();
		if (uuid) uuid->release();
		return (NULL);
	}
	if (bsdName) {
		ret = matchDict->setObject(kIOBSDNameKey, bsdName);

		if (!ret) {
			dprintf("%s couldn't setup bsd name matching"
			    " dictionary\n", __func__);
			matchDict->release();
			matchDict = 0;
		}
		if (uuid) uuid->release();
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

	/* Return NULL or valid OSDictionary with retain count */
	return (matchDict);
}

/* Returns NULL or matched IOMedia with a retain count */
static IOMedia *
media_from_matchdict(OSDictionary *matchDict)
{
	OSIterator *iter = 0;
	OSObject *obj = 0;
	IOMedia *media = 0;

	if (!matchDict) {
		dprintf("%s missing matching dictionary\n", __func__);
		return (NULL);
	}

	/*
	 * We could instead use copyMatchingService, since
	 * there should only be one match.
	 */
	iter = IOService::getMatchingServices(matchDict);
	if (!iter) {
		dprintf("%s No iterator from getMatchingServices\n",
		    __func__);
		return (NULL);
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
		return (NULL);
	}

#ifdef DEBUG
	/* Report if there were additional matches */
	if (iter->getNextObject() != NULL) {
		dprintf("%s Had more potential matches\n", __func__);
	}
#endif
	iter->release();
	iter = 0;

	/* Return valid IOMedia with retain count */
	return (media);
}

/*
 * media_from_dev is intended to be called by ldi_open_by_name
 * and ldi_open_by_dev with a dev_t, and returns NULL or an IOMedia
 * device with a retain count that should be released on open.
 */
static IOMedia *
media_from_dev(dev_t device = 0)
{
	IOMedia *media;
	OSDictionary *matchDict;

	/* Get matchDict, will need to be released */
	matchDict = media_matchdict_from_dev(device);
	if (!matchDict) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		return (NULL);
	}

	/* Get first matching IOMedia */
	media = media_from_matchdict(matchDict);
	matchDict->release();
	matchDict = 0;

	if (!media) {
		dprintf("%s no IOMedia found for dev_t %d\n", __func__,
		    device);
	}

	/* Return NULL or valid media with retain count */
	return (media);
}

/*
 * media_from_device_path
 *
 * translate /private/var/run/disk/by-path/<path> to an IOMedia
 * handle. The remainder of the path should be a valid
 * path in the IORegistry IODTPlane device tree.
 */
static IOMedia *
media_from_device_path(const char *path = 0)
{
	IORegistryEntry *entry = 0;
	IOMedia *media = 0;
	OSString *osstr;
	const char *string, *dash;

	/* Must be /var/run/disk/by-path/, but may have /private prefix */
	if (!path || path[0] == 0 ||
	    (strncmp(path, "/var/run/disk/by-path/", 22) != 0 &&
	    strncmp(path, "/private/var/run/disk/by-path/", 30) != 0)) {
		dprintf("%s invalid path [%s]\n", __func__,
		    (path && path[0] != '\0' ? path : ""));
		return (NULL);
	}

	/* We need the leading slash in the string, so trim 21 or 29 */
	if (strncmp(path, "/private", 8) == 0) {
		osstr = OSString::withCString(path+29);
	} else {
		osstr = OSString::withCString(path+21);
	}
	if (!osstr) {
		dprintf("%s couldn't get string from path\n", __func__);
		return (NULL);
	}

	string = osstr->getCStringNoCopy();
	ASSERT(string);

	/* Convert dashes to slashes */
	while ((dash = strchr(string, '-')) != NULL) {
		osstr->setChar('/', dash - string);
	}
	dprintf("%s string [%s]\n", __func__, string);

	entry = IORegistryEntry::fromPath(string, gIODTPlane);
	string = 0;
	osstr->release();
	osstr = 0;

	if (!entry) {
		dprintf("%s IORegistryEntry::fromPath failed\n", __func__);
		return (NULL);
	}

	if ((media = OSDynamicCast(IOMedia, entry)) == NULL) {
		entry->release();
		return (0);
	}

	/* Leave a retain count on the media */
	return (media);
}

/*
 * media_from_serial
 *
 * translate /private/var/run/disk/by-serial/model-serial[:location]
 * to an IOMedia handle. The path format is determined by
 * InvariantDisks logic in IDSerialLinker.cpp.
 */
static IOMedia *
media_from_serial(const char *path = 0)
{
	IORegistryEntry *entry = 0;
	IOMedia *media = 0;
	OSDictionary *matching = 0;
	OSDictionary *deviceCharacteristics = 0;
	OSIterator *iter = 0;
	OSString *osstr = 0;
	OSString *model = 0;
	OSString *serial = 0;
	OSNumber *bsdUnit = 0;
	OSObject *property = 0;
	OSObject *propDict = 0;
	OSObject *obj = 0;
	const char *substr = 0;
	const char *sep1 = 0, *sep2 = 0;
	const char *string = 0, *space = 0;
	const char *location = 0, *entryLocation = 0;
	int newlen = 0, soff = 0;
	bool matched = false;

	/* Must be /var/run/disk/by-serial/, but may have /private prefix */
	if (!path || path[0] == 0 ||
	    (strncmp(path, "/var/run/disk/by-serial/", 24) != 0 &&
	    strncmp(path, "/private/var/run/disk/by-serial/", 32) != 0)) {
		dprintf("%s invalid path [%s]\n", __func__,
		    (path && path[0] != '\0' ? path : ""));
		return (NULL);
	}

	/* substr starts after '/by-serial/' */
	substr = path + 24;
	if (strncmp(path, "/private", 8) == 0) substr += 8;

	/*
	 * For each whole-disk IOMedia:
	 * Search parents for deviceCharacteristics, or skip.
	 * Check for Model and Serial Number properties, or skip.
	 * Trim trailing space and swap underscores within string.
	 * If "model-serial" matches path so far:
	 *  Match whole-disk IOMedia if no slice specified.
	 *  Or get child IOMedia with matching Location property.
	 */

	sep1 = strchr(substr, '-');
	sep2 = strrchr(substr, ':');
	if (sep1 == 0) {
		dprintf("%s invalid by-serial path [%s]\n", __func__, substr);
		return (NULL);
	}
	if (sep2 == 0) {
		dprintf("%s no slice, whole disk [%s]\n", __func__, substr);
		sep2 = substr + (strlen(substr));
	}

	if ((matching = IOService::serviceMatching("IOMedia")) == NULL) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		return (NULL);
	}

	if ((matching->setObject(kIOMediaWholeKey, kOSBooleanTrue) == false) ||
	    (iter = IOService::getMatchingServices(matching)) == NULL) {
		dprintf("%s couldn't get IOMedia iterator\n", __func__);
		matching->release();
		return (NULL);
	}
	matching->release();
	matching = 0;

	while ((obj = iter->getNextObject()) != NULL) {
		if ((entry = OSDynamicCast(IORegistryEntry, obj)) == NULL ||
		    (media = OSDynamicCast(IOMedia, entry)) == NULL ||
		    media->isFormatted() == false) {
		    // media->isWhole() == false) {
			continue;
		}

		propDict = media->getProperty(
		    kIOPropertyDeviceCharacteristicsKey, gIOServicePlane,
		    (kIORegistryIterateRecursively |
		    kIORegistryIterateParents));
		if ((deviceCharacteristics = OSDynamicCast(OSDictionary,
		    propDict)) == NULL) {
			dprintf("%s no device characteristics, skipping\n",
			    __func__);
			continue;
		}

		/*
		 * Get each property, cast as OSString, then copy
		 * to a new OSString.
		 */
		if ((property = deviceCharacteristics->getObject(
		    kIOPropertyProductNameKey)) == NULL ||
		    (osstr = OSDynamicCast(OSString, property)) == NULL ||
		    (model = OSString::withString(osstr)) == NULL) {
			dprintf("%s no product name, skipping\n", __func__);
			continue;
		}
		if ((property = deviceCharacteristics->getObject(
		    kIOPropertyProductSerialNumberKey)) == NULL ||
		    (osstr = OSDynamicCast(OSString, property)) == NULL ||
		    (serial = OSString::withString(osstr)) == NULL) {
			dprintf("%s no serial number, skipping\n", __func__);
			model->release();
			model = 0;
			continue;
		}

		string = model->getCStringNoCopy();
		if (!string) {
			model->release();
			model = 0;
			serial->release();
			serial = 0;
			continue;
		}
		/* Trim trailing whitespace */
		for (newlen = strlen(string); newlen > 0; newlen--) {
			if (string[newlen-1] != ' ') {
				model->setChar('\0', newlen);
				break;
			}
		}

		/*
		 * sep1 is the location of the first '-' in the path.
		 * even if there is a '-' in the model name, we can skip
		 * media with model names shorter than that.
		 */
		if (newlen == 0 ||
		    (newlen < (sep1 - substr)) ||
		    (substr[newlen] != '-')) {
			model->release();
			model = 0;
			serial->release();
			serial = 0;
			continue;
		}

		/* Convert spaces to underscores */
		while ((space = strchr(string, ' ')) != NULL) {
			model->setChar('_', space - string);
		}

		/* Compare the model string with the path */
		if (strncmp(substr, string, newlen) != 0) {
			model->release();
			model = 0;
			serial->release();
			serial = 0;
			continue;
		}
		dprintf("%s model string matched [%s]\n",
		    __func__, model->getCStringNoCopy());
		model->release();
		model = 0;

		soff = newlen + 1;

		string = serial->getCStringNoCopy();
		if (!string) {
			serial->release();
			serial = 0;
			continue;
		}
		/* Trim trailing whitespace */
		for (newlen = strlen(string); newlen > 0; newlen--) {
			if (string[newlen-1] != ' ') {
				serial->setChar('\0', newlen);
				break;
			}
		}
		/*
		 * sep2 is the location of the last ':' in the path, or
		 * the end of the string if there is none.
		 * even if there is a ':' in the serial number, we can skip
		 * media with serial number strings shorter than that.
		 */
		if (newlen == 0 ||
		    (newlen < (sep2 - sep1 - 1)) ||
		    (substr[soff+newlen] != '\0' &&
		    substr[soff+newlen] != ':')) {
			serial->release();
			serial = 0;
			continue;
		}

		/* Convert spaces to underscores */
		while ((space = strchr(string, ' ')) != NULL) {
			serial->setChar('_', space - string);
		}

		/* Compare the serial string with the path */
		if (strncmp(substr+soff, string, newlen) != 0) {
			serial->release();
			serial = 0;
			continue;
		}
		dprintf("%s serial string matched [%s]\n",
		    __func__, serial->getCStringNoCopy());
		serial->release();
		serial = 0;

		/*
		 * Still need to get the slice - the component
		 * after an optional ':' at the end of the
		 * string, by searching for IOMedia with that
		 * location string below the whole-disk IOMedia.
		 */
		/* Set new location of ':' */
		sep2 = substr + (soff + newlen);
		/* Found match */
		matched = true;
		media->retain();
		break;
	}
	iter->release();
	iter = 0;

	if (!matched || !media) {
		dprintf("%s no matching devices found\n", __func__);
		return (NULL);
	}

	/* Whole disk path will not end with ':<location>' */
	if (sep2[0] != ':') {
		dprintf("%s Found whole disk [%s]\n", __func__, path);
		/* Leave a retain count on the media */
		return (media);
	}

	/* Remainder of string is location */
	location = sep2 + 1;
	dprintf("%s location string [%s]\n", __func__, location);

	if ((bsdUnit = OSDynamicCast(OSNumber,
	    media->getProperty(kIOBSDUnitKey))) == NULL) {
		dprintf("%s couldn't get BSD unit number\n", __func__);
		media->release();
		return (NULL);
	}
	if ((matching = IOService::serviceMatching("IOMedia")) == NULL ||
	    (matching->setObject(kIOMediaWholeKey, kOSBooleanFalse)) == false ||
	    (matching->setObject(kIOBSDUnitKey, bsdUnit)) == false ||
	    (iter = IOService::getMatchingServices(matching)) == NULL) {
		dprintf("%s iterator for location failed\n",
		    __func__);

		if (matching) matching->release();
		/* We had a candidate, but couldn't get the location */
		media->release();
		return (NULL);
	}
	matching->release();
	matching = 0;

	/* Iterate over children checking for matching location */
	matched = false;
	entry = 0;
	while ((obj = iter->getNextObject()) != NULL) {
		if ((entry = OSDynamicCast(IORegistryEntry, obj)) == NULL ||
		    (OSDynamicCast(IOMedia, entry)) == NULL) {
			entry = 0;
			continue;
		}

		if ((entryLocation = entry->getLocation()) == NULL ||
		    (strlen(entryLocation) != strlen(location)) ||
		    strcmp(entryLocation, location) != 0) {
			entry = 0;
			continue;
		}

		dprintf("%s found match\n", __func__);
		matched = true;
		entry->retain();
		break;
	}
	iter->release();
	iter = 0;

	/* Drop the whole-disk media */
	media->release();
	media = 0;

	/* Cast the new entry, if there is one */
	if (!entry || (media = OSDynamicCast(IOMedia, entry)) == NULL) {
if (entry) dprintf("%s had entry but couldn't cast\n", __func__);
		dprintf("%s no media found for path %s\n",
		    __func__, path);
		if (entry) entry->release();
		return (NULL);
	}

	dprintf("%s media from serial number succeeded\n", __func__);

	/* Leave a retain count on the media */
	return (matched ? media : NULL);
}

/*
 * media_from_path is intended to be called by ldi_open_by_name
 * with a char* path, and returns NULL or an IOMedia device with a
 * retain count that should be released on open.
 */
static IOMedia *
media_from_path(const char *path = 0)
{
	IOMedia *media;
	OSDictionary *matchDict;

	/* Validate path */
	if (path == 0 || strlen(path) <= 1) {
		dprintf("%s no path provided\n", __func__);
		return (NULL);
	}

	if (strncmp(path, "/var/run/disk/by-path/", 22) == 0 ||
	    strncmp(path, "/private/var/run/disk/by-path/", 30) == 0) {
		media = media_from_device_path(path);
		dprintf("%s media_from_device_path %s\n", __func__,
		    (media ? "succeeded" : "failed"));
		return (media);
	}

	if (strncmp(path, "/var/run/disk/by-serial/", 24) == 0 ||
	    strncmp(path, "/private/var/run/disk/by-serial/", 32) == 0) {
		media = media_from_serial(path);
		dprintf("%s media_from_serial %s\n", __func__,
		    (media ? "succeeded" : "failed"));
		return (media);
	}

	/* Try to get /dev/disk or /private/var/run/disk/by-id path */
	matchDict = media_matchdict_from_path(path);
	if (!matchDict) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		return (NULL);
	}

	media = media_from_matchdict(matchDict);
	matchDict->release();
	matchDict = 0;

	if (!media) {
		dprintf("%s no IOMedia found for path %s\n", __func__, path);
	}

	/* Return NULL or valid media with retain count */
	return (media);
}

/* Completion handler for IOKit strategy */
static void
ldi_iokit_io_intr(void *target, void *parameter,
    IOReturn status, UInt64 actualByteCount)
{
	IOMemoryDescriptor *iomem = (IOMemoryDescriptor *)target;

	ldi_buf_t *lbp = (ldi_buf_t *)parameter;

#ifdef DEBUG
	/* In debug builds, verify buffer pointers */
	ASSERT3U(lbp, !=, 0);

	if (!lbp) {
		printf("%s missing a buffer\n", __func__);
		return;
	}

	if (!iomem) {
		printf("%s missing iomem\n", __func__);
		return;
	}

	// this is very very very noisy in --enable-boot
	// ASSERT3U(ldi_zfs_handle, !=, 0);

	if (actualByteCount == 0 ||
	    actualByteCount != lbp->b_bcount ||
	    status != kIOReturnSuccess) {
		printf("%s %s %llx / %llx\n", __func__,
		    "actualByteCount != lbp->b_bcount",
		    actualByteCount, lbp->b_bcount);
		if (ldi_zfs_handle)
			printf("%s status %d %d %s\n", __func__, status,
			    ldi_zfs_handle->errnoFromReturn(status),
			    ldi_zfs_handle->stringFromReturn(status));
		else
			printf("%s status %d ldi_zfs_handle is NULL\n",
			    __func__, status);
	}
#endif

	/* Complete and release IOMemoryDescriptor */
	iomem->complete();
	iomem->release();
	iomem = 0;

	/* Compute resid */
	ASSERT3U(lbp->b_bcount, >=, actualByteCount);
	lbp->b_resid = (lbp->b_bcount - actualByteCount);

	/* Set error status */
	if (status == kIOReturnSuccess &&
	    actualByteCount != 0 && lbp->b_resid == 0) {
		lbp->b_error = 0;
	} else {
		lbp->b_error = EIO;
	}

	/* Call original completion function */
	if (lbp->b_iodone) {
		(void) lbp->b_iodone(lbp);
	}
}

/*
 * Uses IOMedia::read asynchronously or IOStorage::read synchronously.
 * virtual void read(IOService *	client,
 *     UInt64				byteStart,
 *     IOMemoryDescriptor *		buffer,
 *     IOStorageAttributes *		attributes,
 *     IOStorageCompletion *		completion);
 * virtual IOReturn read(IOService *	client,
 *     UInt64				byteStart,
 *     IOMemoryDescriptor *		buffer,
 *     IOStorageAttributes *		attributes = 0,
 *     UInt64 *				actualByteCount = 0);
 */
int
buf_strategy_iokit(ldi_buf_t *lbp, struct ldi_handle *lhp)
{

	ASSERT3U(lbp, !=, NULL);
	ASSERT3U(lhp, !=, NULL);

#ifdef DEBUG
	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp)) ||
	    !OSDynamicCast(IOService, LH_CLIENT(lhp))) {
		dprintf("%s invalid IOMedia or client\n", __func__);
		return (ENODEV);
	}
#endif /* DEBUG */

	/* Allocate a memory descriptor pointing to the data address */
	IOMemoryDescriptor	*iomem;
	iomem = IOMemoryDescriptor::withAddress(
	    lbp->b_un.b_addr, lbp->b_bcount,
	    (lbp->b_flags & B_READ ? kIODirectionIn : kIODirectionOut));

	/* Verify the buffer */
	if (!iomem || iomem->getLength() != lbp->b_bcount ||
	    iomem->prepare() != kIOReturnSuccess) {
		dprintf("%s couldn't allocate IO buffer\n",
		    __func__);
		if (iomem) {
			iomem->release();
		}
		return (ENOMEM);
	}

	/* Recheck instantaneous value of handle status */
	if (lhp->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s device not online\n", __func__);
		iomem->complete();
		iomem->release();
		return (ENODEV);
	}

	IOStorageAttributes	ioattr = { 0 };

	/* Synchronous or async */
	if (lbp->b_iodone == NULL) {
		UInt64 actualByteCount = 0;
		IOReturn result;

		/* Read or write */
		if (lbp->b_flags & B_READ) {
			result = LH_MEDIA(lhp)->IOStorage::read(LH_CLIENT(lhp),
			    dbtolb(lbp->b_lblkno), iomem,
			    &ioattr, &actualByteCount);
		} else {
			result = LH_MEDIA(lhp)->IOStorage::write(LH_CLIENT(lhp),
				dbtolb(lbp->b_lblkno), iomem,
				&ioattr, &actualByteCount);
		}

		/* Call completion */
		ldi_iokit_io_intr((void *)iomem, (void *)lbp,
		    result, actualByteCount);

		/* Return success based on result */
		return (result == kIOReturnSuccess ? 0 : EIO);
	}

#if !defined(MAC_OS_X_VERSION_10_9) || \
  (MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_9)
	/* Priority of I/O */
	if (lbp->b_flags & B_THROTTLED_IO) {
		lbp->b_flags &= ~B_THROTTLED_IO;
		ioattr.priority = kIOStoragePriorityBackground;
		if (lbp->b_flags & B_WRITE)
			ioattr.priority--;
	} else if ((lbp->b_flags & B_ASYNC) == 0 || (lbp->b_flags & B_WRITE))
		ioattr.priority = kIOStoragePriorityDefault - 1;
	else
		ioattr.priority = kIOStoragePriorityDefault;
#endif

	/*
	 * Make sure there is enough space to hold IOCompletion.
	 * If this trips, increase the space in ldi_buf.h's
	 * struct opaque_iocompletion.
	 */
	CTASSERT(sizeof (struct opaque_iocompletion) >=
	    sizeof (struct IOStorageCompletion));

	IOStorageCompletion	*iocompletion;
	iocompletion = (IOStorageCompletion *)&lbp->b_completion;
	iocompletion->target = (void *)iomem;
	iocompletion->parameter = lbp;
	iocompletion->action = &ldi_iokit_io_intr;

	/* Read or write */
	if (lbp->b_flags & B_READ) {
		LH_MEDIA(lhp)->IOMedia::read(LH_CLIENT(lhp),
		    dbtolb(lbp->b_lblkno), iomem,
		    &ioattr, iocompletion);
	} else {
		LH_MEDIA(lhp)->IOMedia::write(LH_CLIENT(lhp),
		    dbtolb(lbp->b_lblkno), iomem,
		    &ioattr, iocompletion);
	}

	/* Return success, will call io_intr when done */
	return (0);
}

/* Client interface, alloc and open IOKit handle */
int
ldi_open_by_media(IOMedia *media = 0, dev_t device = 0,
    int fmode = 0, ldi_handle_t *lhp = 0)
{
	struct ldi_handle *retlhp;
	ldi_status_t status;
	int error;

	/* Validate IOMedia */
	if (!media || !lhp) {
		dprintf("%s invalid argument %p or %p\n",
		    __func__, media, lhp);
		return (EINVAL);
	}

	/* Retain for duration of open */
	media->retain();

	/* Get dev_t if not supplied */
	if (device == 0 && (device = dev_from_media(media)) == 0) {
		dprintf("%s dev_from_media failed: %p %d\n", __func__,
		    media, device);
		media->release();
		return (ENODEV);
	}

	/* In debug build, be loud if we potentially leak a handle */
	ASSERT3U(*(struct ldi_handle **)lhp, ==, NULL);

	/* Allocate IOKit handle */
	retlhp = handle_alloc_iokit(device, fmode);
	if (retlhp == NULL) {
		dprintf("%s couldn't allocate IOKit handle\n", __func__);
		media->release();
		return (ENOMEM);
	}

	/* Try to open device with IOMedia */
	status = handle_open_start(retlhp);
	if (status == LDI_STATUS_ONLINE) {
		dprintf("%s already online, refs %d, openrefs %d\n", __func__,
		    retlhp->lh_ref, retlhp->lh_openref);
		/* Cast retlhp and assign to lhp (may be 0) */
		*lhp = (ldi_handle_t)retlhp;
		media->release();
		/* Successfully incremented open ref */
		return (0);
	}
	if (status != LDI_STATUS_OPENING) {
		dprintf("%s invalid status %d\n", __func__, status);
		handle_release(retlhp);
		retlhp = 0;
		media->release();
		return (ENODEV);
	}

	error = handle_open_iokit(retlhp, media);
	media->release();

	if (error) {
		dprintf("%s Couldn't open handle\n", __func__);
		handle_open_done(retlhp, LDI_STATUS_CLOSED);
		handle_release(retlhp);
		retlhp = 0;
		return (EIO);
	}
	handle_open_done(retlhp, LDI_STATUS_ONLINE);

	/* Register for disk notifications */
	handle_register_notifier(retlhp);

	/* Cast retlhp and assign to lhp (may be 0) */
	*lhp = (ldi_handle_t)retlhp;
	/* Pass error from open */
	return (error);
}

/* Client interface, find IOMedia from dev_t, alloc and open handle */
int
ldi_open_media_by_dev(dev_t device = 0, int fmode = 0,
    ldi_handle_t *lhp = 0)
{
	IOMedia *media = 0;
	int error = EINVAL;

	/* Validate arguments */
	if (!lhp || device == 0) {
		dprintf("%s missing argument %p %d\n",
		    __func__, lhp, device);
		return (EINVAL);
	}
	/* In debug build, be loud if we potentially leak a handle */
	ASSERT3U(*((struct ldi_handle **)lhp), ==, NULL);

	/* Get IOMedia from major/minor */
	if ((media = media_from_dev(device)) == NULL) {
		dprintf("%s media_from_dev error %d\n",
		    __func__, error);
		return (ENODEV);
	}

	/* Try to open by media */
	error = ldi_open_by_media(media, device, fmode, lhp);

	/* Release IOMedia and clear */
	media->release();
	media = 0;

	/* Pass error from open */
	return (error);
}

/* Client interface, find dev_t and IOMedia/vnode, alloc and open handle */
int
ldi_open_media_by_path(char *path = 0, int fmode = 0,
    ldi_handle_t *lhp = 0)
{
	IOMedia *media = 0;
	dev_t device = 0;
	int error = EINVAL;

	/* Validate arguments */
	if (!lhp || !path) {
		dprintf("%s %s %p %s %d\n", __func__,
		    "missing lhp or path", lhp, path, fmode);
		return (EINVAL);
	}
	/* In debug build, be loud if we potentially leak a handle */
	ASSERT3U(*((struct ldi_handle **)lhp), ==, NULL);

	/* For /dev/disk*, and InvariantDisk paths */
	if ((media = media_from_path(path)) == NULL) {
		dprintf("%s media_from_path failed\n", __func__);
		return (ENODEV);
	}

	error = ldi_open_by_media(media, device, fmode, lhp);

	/* Release IOMedia and clear */
	media->release();
	media = 0;

	/* Error check open */
	if (error) {
		dprintf("%s ldi_open_by_media failed %d\n",
		    __func__, error);
	}

	return (error);
}

int
handle_remove_notifier(struct ldi_handle *lhp)
{
	handle_notifier_t notifier;

#ifdef DEBUG
	if (!lhp) {
		dprintf("%s missing handle\n", __func__);
		return (EINVAL);
	}
#endif

	if (lhp->lh_notifier == 0) {
		dprintf("%s no notifier installed\n", __func__);
		return (0);
	}

	/* First clear notifier pointer */
	notifier = lhp->lh_notifier;
	lhp->lh_notifier = 0;

#ifdef DEBUG
	/* Validate IONotifier object */
	if (!OSDynamicCast(IONotifier, notifier->obj)) {
		dprintf("%s %p is not an IONotifier\n", __func__,
		    notifier->obj);
		return (EINVAL);
	}
#endif

	notifier->obj->remove();
	kmem_free(notifier, sizeof (handle_notifier_t));
	return (0);
}

int
handle_register_notifier(struct ldi_handle *lhp)
{
	OSDictionary *matchDict;
	handle_notifier_t notifier;

	/* Make sure we have a handle and dev_t */
	if (!lhp || lhp->lh_dev == 0) {
		dprintf("%s no handle or missing dev_t\n", __func__);
		return (EINVAL);
	}

	notifier = (handle_notifier_t)kmem_alloc(
	    sizeof (struct _handle_notifier), KM_SLEEP);
	if (!notifier) {
		dprintf("%s couldn't alloc notifier struct\n", __func__);
		return (ENOMEM);
	}

	/* Get matchDict, will need to be released */
	matchDict = media_matchdict_from_dev(lhp->lh_dev);
	if (!matchDict) {
		dprintf("%s couldn't get matching dictionary\n", __func__);
		kmem_free(notifier, sizeof (handle_notifier_t));
		return (EINVAL);
	}

	/* Register IOMedia termination notification */
	notifier->obj = IOService::addMatchingNotification(
	    gIOTerminatedNotification, matchDict,
	    handle_media_terminate_cb, /* target */ 0,
	    /* refCon */ (void *)lhp, /* priority */ 0);
	matchDict->release();

	/* Error check notifier */
	if (!notifier->obj) {
		dprintf("%s addMatchingNotification failed\n",
		    __func__);
		kmem_free(notifier, sizeof (handle_notifier_t));
		return (ENOMEM);
	}

	/* Assign notifier to handle */
	lhp->lh_notifier = notifier;
	return (0);
}

/* Supports both IOKit and vnode handles by finding IOMedia from dev_t */
int
handle_set_wce_iokit(struct ldi_handle *lhp, int *wce)
{
	IOMedia *media;
	IORegistryEntry *parent;
	IOBlockStorageDevice *device;
	IOReturn result;
	bool value;

	if (!lhp || !wce) {
		return (EINVAL);
	}

	switch (lhp->lh_type) {
	case LDI_TYPE_IOKIT:
		if ((media = LH_MEDIA(lhp)) == NULL) {
			dprintf("%s couldn't get IOMedia\n", __func__);
			return (ENODEV);
		}
		/* Add a retain count */
		media->retain();
		break;
	case LDI_TYPE_VNODE:
		if (lhp->lh_dev == 0 ||
		    (media = media_from_dev(lhp->lh_dev)) == 0) {
			dprintf("%s couldn't find IOMedia for dev_t %d\n",
			    __func__, lhp->lh_dev);
			return (ENODEV);
		}
		/* Returned media has a retain count */
		break;
	default:
		dprintf("%s invalid handle\n", __func__);
		return (EINVAL);
	}

	/* Walk the parents of this media */
	for (parent = media->getParentEntry(gIOServicePlane);
	    parent != NULL;
	    parent = parent->getParentEntry(gIOServicePlane)) {
		/* Until a valid device is found */
		device = OSDynamicCast(IOBlockStorageDevice, parent);
		if (device != NULL) {
			device->retain();
			break;
		}
		/* Next parent */
	}
	media->release();
	media = 0;

	/* If no matching device was found */
	if (!device) {
		dprintf("%s no IOBlockStorageDevice found\n", __func__);
		return (ENODEV);
	}

	result = device->getWriteCacheState(&value);
	if (result != kIOReturnSuccess) {
		// dprintf("%s couldn't get current write cache state %d\n",
		//   __func__, ldi_zfs_handle->errnoFromReturn(result));
		device->release();
		return (ENXIO);
	}

	/* If requested value does not match current */
	if (value != *wce) {
		value = (*wce == 1);
		/* Attempt to change the value */
		result = device->setWriteCacheState(value);
	}

	/* Set error and wce to return */
	if (result != kIOReturnSuccess) {
		// dprintf("%s couldn't set write cache %d\n",
		//   __func__, ldi_zfs_handle->errnoFromReturn(result));
		/* Flip wce to indicate current status */
		*wce = !(*wce);
		device->release();
		return (ENXIO);
	}

	device->release();
	return (0);
}

int
handle_get_media_info_iokit(struct ldi_handle *lhp,
    struct dk_minfo *dkm)
{
	uint32_t blksize;
	uint64_t blkcount;

	if (!lhp || !dkm) {
		return (EINVAL);
	}

	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		dprintf("%s invalid IOKit handle\n", __func__);
		return (ENODEV);
	}

	LH_MEDIA(lhp)->retain();

	if ((blksize = LH_MEDIA(lhp)->getPreferredBlockSize()) == 0) {
		dprintf("%s invalid blocksize\n", __func__);
		LH_MEDIA(lhp)->release();
		return (ENXIO);
	}

	if ((blkcount = LH_MEDIA(lhp)->getSize() / blksize) == 0) {
		dprintf("%s invalid block count\n", __func__);
		LH_MEDIA(lhp)->release();
		return (ENXIO);
	}

	LH_MEDIA(lhp)->release();

	/* Set the return values */
	dkm->dki_capacity = blkcount;
	dkm->dki_lbsize = blksize;

	return (0);
}

int
handle_get_media_info_ext_iokit(struct ldi_handle *lhp,
    struct dk_minfo_ext *dkmext)
{
	OSObject *prop;
	OSNumber *number;
	uint32_t blksize, pblksize;
	uint64_t blkcount;

	if (!lhp || !dkmext) {
		printf("zfs: %s missing lhp or dkmext\n", __func__);
		return (EINVAL);
	}

	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		printf("zfs: %s invalid IOKit handle\n", __func__);
		return (ENODEV);
	}

	LH_MEDIA(lhp)->retain();

	prop = LH_MEDIA(lhp)->getProperty(kIOPropertyPhysicalBlockSizeKey,
	    gIOServicePlane, kIORegistryIterateRecursively |
	    kIORegistryIterateParents);

	number = OSDynamicCast(OSNumber, prop);
	if (!prop || !number) {
		printf("zfs: %s couldn't get physical blocksize\n", __func__);
		LH_MEDIA(lhp)->release();
		return (ENXIO);
	}

	pblksize = number->unsigned32BitValue();
	number = 0;
	prop = 0;

	if ((blksize = LH_MEDIA(lhp)->getPreferredBlockSize()) == 0) {
		printf("zfs: %s invalid blocksize\n", __func__);
		LH_MEDIA(lhp)->release();
		return (ENXIO);
	}

	if ((blkcount = LH_MEDIA(lhp)->getSize() / blksize) == 0) {
		printf("ZFS: %s invalid block count\n", __func__);
		LH_MEDIA(lhp)->release();
		return (ENXIO);
	}

	LH_MEDIA(lhp)->release();

#ifdef DEBUG
	printf("ZFS: %s phys blksize %u, logical blksize %u, blockcount %llu\n",
	    __func__, pblksize, blksize, blkcount);
#endif

	/*
	 * The Preferred Block Size may be smaler than the Physical Block
	 * Size.  The latter is what is bubbled up to "diskutil info -plist"'s
	 * <key>DeviceBlockSize</key>.
	 *
	 * In theory this should only lower-limit the ashift when adding a
	 * vdev.  It also is what "zpool get ashift pool vdev" returns.
	 *
	 * In practice, different external enclosures can return different
	 * physical block sizes for the same physical storage device, which
	 * results in zpool status -vx reporting mismatches, and problems with
	 * scrubs triggering vdev.bad_ashift and ejecting the physical device
	 * if it is moved from a working enclousre to a different enclosure.
	 *
	 * Therefore return the smaller of kIOPropertyPhysicalBlockSizeKey
	 * and getPreferredBlockSize in dki_pbsize.
	 */

	/* Set the return values */

	if (pblksize > blksize) {
		printf("ZFS: %s set dki_pbsize to %u instead of %u\n",
		    __func__, blksize, pblksize);
		dkmext->dki_pbsize = blksize;
	} else {
		dkmext->dki_pbsize = pblksize;
	}

	dkmext->dki_capacity = blkcount;
	dkmext->dki_lbsize = blksize;

	return (0);
}

int
handle_check_media_iokit(struct ldi_handle *lhp, int *status)
{
	/* Validate arguments */
	if (!lhp || !status) {
		return (EINVAL);
	}

	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		dprintf("%s invalid IOKit handle\n", __func__);
		return (ENODEV);
	}

	LH_MEDIA(lhp)->retain();

	/* Validate device size */
	if (LH_MEDIA(lhp)->getSize() == 0) {
		dprintf("%s media reported 0 size\n", __func__);
		LH_MEDIA(lhp)->release();
		return (ENXIO);
	}

	/* Validate write status if handle fmode is read-write */
	if ((lhp->lh_fmode & FWRITE) &&
	    LH_MEDIA(lhp)->isWritable() == false) {
		dprintf("%s media is not writeable\n", __func__);
		LH_MEDIA(lhp)->release();
		return (EPERM);
	}

	LH_MEDIA(lhp)->release();

	/* Success */
	*status = 0;
	return (0);
}

int
handle_is_solidstate_iokit(struct ldi_handle *lhp, int *isssd)
{
	OSDictionary *propDict = 0;
	OSString *property = 0;

	/* Validate arguments */
	if (!lhp || !isssd) {
		return (EINVAL);
	}

	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		dprintf("%s invalid IOKit handle\n", __func__);
		return (ENODEV);
	}

	LH_MEDIA(lhp)->retain();

	propDict = OSDynamicCast(OSDictionary, LH_MEDIA(lhp)->getProperty(
	    kIOPropertyDeviceCharacteristicsKey, gIOServicePlane));

	if (propDict != 0) {
		property = OSDynamicCast(OSString,
		    propDict->getObject(kIOPropertyMediumTypeKey));
		propDict = 0;
	}

	if (property != 0 &&
	    property->isEqualTo(kIOPropertyMediumTypeSolidStateKey)) {
		*isssd = 1;
	}
	property = 0;

	LH_MEDIA(lhp)->release();

	return (0);
}

int
handle_features_iokit(struct ldi_handle *lhp,
    uint32_t *data)
{
	if (!lhp || !data) {
		return (EINVAL);
	}

	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		dprintf("%s invalid IOKit handle\n", __func__);
		return (ENODEV);
	}

	LH_MEDIA(lhp)->retain();

	OSDictionary *dictionary = OSDynamicCast(
	    /* class  */ OSDictionary,
	    /* object */ LH_MEDIA(lhp)->getProperty(
	    /* key    */ kIOStorageFeaturesKey,
	    /* plane  */ gIOServicePlane));

	*data = 0;

	if (dictionary) {
		OSBoolean *boolean;

#ifdef DK_FEATURE_BARRIER
		boolean = OSDynamicCast(
		    /* class  */ OSBoolean,
		    /* object */ dictionary->getObject(
		    /* key    */ kIOStorageFeatureBarrier));

		if (boolean == kOSBooleanTrue)
			*(uint32_t *)data |= DK_FEATURE_BARRIER;
#endif

		boolean = OSDynamicCast(
		    /* class  */ OSBoolean,
		    /* object */ dictionary->getObject(
		    /* key    */ kIOStorageFeatureForceUnitAccess));

		if (boolean == kOSBooleanTrue)
			*(uint32_t *)data |= DK_FEATURE_FORCE_UNIT_ACCESS;

#ifdef DK_FEATURE_PRIORITY
		boolean = OSDynamicCast(
		    /* class  */ OSBoolean,
		    /* object */ dictionary->getObject(
		    /* key    */ kIOStorageFeaturePriority));

		if (boolean == kOSBooleanTrue)
			*(uint32_t *)data |= DK_FEATURE_PRIORITY;
#endif

		boolean = OSDynamicCast(
		    /* class  */ OSBoolean,
		    /* object */ dictionary->getObject(
		    /* key    */ kIOStorageFeatureUnmap));

		if (boolean == kOSBooleanTrue)
			*(uint32_t *)data |= DK_FEATURE_UNMAP;
	}

	LH_MEDIA(lhp)->release();
	return (0);
}

int
handle_unmap_iokit(struct ldi_handle *lhp,
    dkioc_free_list_ext_t *dkm)
{
	int error = 0;

	if (!lhp || !dkm) {
		return (EINVAL);
	}

	/* Validate IOMedia */
	if (!OSDynamicCast(IOMedia, LH_MEDIA(lhp))) {
		dprintf("%s invalid IOKit handle\n", __func__);
		return (ENODEV);
	}

	LH_MEDIA(lhp)->retain();

	/* We need to convert illumos' dkioc_free_list_t to dk_unmap_t */
	IOStorageExtent *extents;
	extents = IONew(IOStorageExtent, 1);
	extents[0].byteStart = dkm->dfle_start;
	extents[0].byteCount = dkm->dfle_length;

	/*
	 * dkm->dfl_flags vs IOStorageUnmapOptions
	 * #define DF_WAIT_SYNC 0x00000001
	 * Wait for full write-out of free.
	 * IOStorageUnmapOptions is only 0
	 */

	/* issue unmap */
	error = LH_MEDIA(lhp)->unmap(LH_CLIENT(lhp),
	    extents, 1, 0);

	if (error != 0) {
		dprintf("%s unmap: 0x%x\n", __func__, error);
		// Convert IOReturn to errno
		error = LH_MEDIA(lhp)->errnoFromReturn(error);
	}

	IODelete(extents, IOStorageExtent, 1);
	LH_MEDIA(lhp)->release();

	return (error);
}


} /* extern "C" */
