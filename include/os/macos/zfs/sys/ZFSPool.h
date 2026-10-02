// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2016, Evan Susarret.  All rights reserved.
 */

#ifndef	ZFSPOOL_H_INCLUDED
#define	ZFSPOOL_H_INCLUDED

#ifdef __cplusplus
#include <IOKit/IOService.h>

#pragma mark - ZFSPool

#define	kZFSPoolNameKey		"ZFS Pool Name"
#define	kZFSPoolSizeKey		"ZFS Pool Size"
#define	kZFSPoolGUIDKey		"ZFS Pool GUID"
#define	kZFSPoolReadOnlyKey	"ZFS Pool Read-Only"

typedef struct spa spa_t;

class ZFSPool : public IOService {
	OSDeclareDefaultStructors(ZFSPool);

protected:
#if 0
	/* XXX Only for debug tracing */
	virtual bool open(IOService *client,
	    IOOptionBits options, void *arg = 0);
	virtual bool isOpen(const IOService *forClient = 0) const;
	virtual void close(IOService *client,
	    IOOptionBits options);
#endif

	bool setPoolName(const char *name);

	virtual bool handleOpen(IOService *client,
	    IOOptionBits options, void *arg) override;
	virtual bool handleIsOpen(const IOService *client) const override;
	virtual void handleClose(IOService *client,
	    IOOptionBits options) override;

	virtual bool init(OSDictionary *properties, spa_t *spa);
	virtual void free() override;

#if 0
	/* IOBlockStorageDevice */
	virtual IOReturn doSynchronizeCache(void);
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *,
	    UInt64, UInt64, IOStorageAttributes *,
	    IOStorageCompletion *);
	virtual UInt32 doGetFormatCapacities(UInt64 *,
	    UInt32) const;
	virtual IOReturn doFormatMedia(UInt64 byteCapacity);
	virtual IOReturn doEjectMedia();
	virtual char *getVendorString();
	virtual char *getProductString();
	virtual char *getRevisionString();
	virtual char *getAdditionalDeviceInfoString();
	virtual IOReturn reportWriteProtection(bool *);
	virtual IOReturn reportRemovability(bool *);
	virtual IOReturn reportMediaState(bool *, bool *);
	virtual IOReturn reportBlockSize(UInt64 *);
	virtual IOReturn reportEjectability(bool *);
	virtual IOReturn reportMaxValidBlock(UInt64 *);

public:
	virtual void read(IOService *client, UInt64 byteStart,
	    IOMemoryDescriptor *buffer, IOStorageAttributes *attr,
	    IOStorageCompletion *completion) override;
	virtual void write(IOService *client, UInt64 byteStart,
	    IOMemoryDescriptor *buffer, IOStorageAttributes *attr,
	    IOStorageCompletion *completion) override;
#endif
public:
	static ZFSPool * withProviderAndPool(IOService *, spa_t *);

private:
	OSSet *_openClients;
	spa_t *_spa;

#if 0
	/* These are declared class static to share across instances */
	static const char *vendorString;
	static const char *revisionString;
	static const char *infoString;
	/* These are per-instance */
	const char *productString;
	bool isReadOnly;
#endif
};

/* C++ wrapper, C uses opaque pointer reference */
typedef struct spa_iokit {
	ZFSPool *proxy;
} spa_iokit_t;

extern "C" {
#endif /* __cplusplus */

/* C functions */
void spa_iokit_pool_proxy_destroy(spa_t *spa);
int spa_iokit_pool_proxy_create(spa_t *spa);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ZFSPOOL_H_INCLUDED */
