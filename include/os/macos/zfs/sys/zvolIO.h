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
 * Copyright (c) 2013, 2016 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef	ZVOLIO_H_INCLUDED
#define	ZVOLIO_H_INCLUDED

/* Linux polutes 'current' */
#undef current

#ifdef __cplusplus
#include <IOKit/IOService.h>

extern "C" {
#endif /* __cplusplus */

#include <sys/zvol.h>
#include <sys/zvol_impl.h>

extern size_t zvolIO_strategy(char *addr, uint64_t offset,
    size_t len, zfs_uio_rw_t rw, const void *privptr);

#ifdef __cplusplus
} /* extern "C" */

class org_openzfsonosx_zfs_zvol : public IOService
{
	OSDeclareDefaultStructors(org_openzfsonosx_zfs_zvol)

private:

public:
	virtual bool init(OSDictionary* dictionary = NULL) override;
	virtual void free(void) override;
	virtual IOService* probe(IOService* provider, SInt32* score) override;
	virtual bool start(IOService* provider) override;
	virtual void stop(IOService* provider) override;

	virtual bool handleOpen(IOService *client,
	    IOOptionBits options, void *arg) override;
	virtual bool handleIsOpen(const IOService *client) const override;
	virtual void handleClose(IOService *client,
	    IOOptionBits options) override;
	virtual bool isOpen(const IOService *forClient = 0) const override;

private:
	OSSet *_openClients;
};

#include <IOKit/storage/IOBlockStorageDevice.h>

class org_openzfsonosx_zfs_zvol_device : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(org_openzfsonosx_zfs_zvol_device)

private:
	// IOService *m_provider;
	zvol_state_t *zv;

public:
	virtual bool init(zvol_state_t *c_zv,
	    OSDictionary* properties = 0);

	virtual bool attach(IOService* provider) override;
	virtual void detach(IOService* provider) override;
	virtual IOReturn doEjectMedia(void) override;
	virtual IOReturn doFormatMedia(UInt64 byteCapacity) override;
	virtual UInt32 doGetFormatCapacities(UInt64 * capacities,
	    UInt32 capacitiesMaxCount) const override;

	virtual IOReturn doLockUnlockMedia(bool doLock) override;
	virtual IOReturn doSynchronizeCache(void) override;
	virtual char *getVendorString(void) override;
	virtual char *getProductString(void) override;
	virtual char *getRevisionString(void) override;
	virtual char *getAdditionalDeviceInfoString(void) override;
	virtual IOReturn reportBlockSize(UInt64 *blockSize) override;
	virtual IOReturn reportEjectability(bool *isEjectable) override;
	virtual IOReturn reportLockability(bool *isLockable) override;
	virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock) override;
	virtual IOReturn reportMediaState(bool *mediaPresent,
	    bool *changedState) override;

	virtual IOReturn reportPollRequirements(bool *pollRequired,
	    bool *pollIsExpensive) override;

	virtual IOReturn reportRemovability(bool *isRemovable) override;
	virtual IOReturn reportWriteProtection(bool *isWriteProtected) override;
	virtual IOReturn getWriteCacheState(bool *enabled) override;
	virtual IOReturn setWriteCacheState(bool enabled) override;
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer,
	    UInt64 block, UInt64 nblks,
	    IOStorageAttributes *attributes,
	    IOStorageCompletion *completion) override;

	virtual IOReturn doDiscard(UInt64 block, UInt64 nblks) override;
	virtual IOReturn doUnmap(IOBlockStorageDeviceExtent *extents,
	    UInt32 extentsCount, UInt32 options) override;

	virtual bool handleOpen(IOService *client,
	    IOOptionBits options, void *access) override;

	virtual void handleClose(IOService *client,
	    IOOptionBits options) override;

	virtual int getBSDName(void);
	virtual int renameDevice(void);
	virtual int offlineDevice(void);
	virtual int onlineDevice(void);
	virtual int refreshDevice(void);

	virtual void clearState(void);
};
#endif /* __cplusplus */

#endif /* ZVOLIO_H_INCLUDED */
