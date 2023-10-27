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
