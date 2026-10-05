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

#ifndef ZFSDATASETPROXY_H_INCLUDED
#define	ZFSDATASETPROXY_H_INCLUDED

#include <IOKit/storage/IOBlockStorageDevice.h>

class ZFSDatasetProxy : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(ZFSDatasetProxy);
public:

	virtual void free(void) override;
	virtual bool init(OSDictionary *properties) override;
	virtual bool start(IOService *provider) override;

	/* IOBlockStorageDevice */
	virtual IOReturn doSynchronizeCache(void) override;
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *,
	    UInt64, UInt64, IOStorageAttributes *,
	    IOStorageCompletion *) override;
	virtual UInt32 doGetFormatCapacities(UInt64 *,
	    UInt32) const override;
	virtual IOReturn doFormatMedia(UInt64 byteCapacity) override;
	virtual IOReturn doEjectMedia() override;
	virtual char *getVendorString() override;
	virtual char *getProductString() override;
	virtual char *getRevisionString() override;
	virtual char *getAdditionalDeviceInfoString() override;
	virtual IOReturn reportWriteProtection(bool *) override;
	virtual IOReturn reportRemovability(bool *) override;
	virtual IOReturn reportMediaState(bool *, bool *) override;
	virtual IOReturn reportBlockSize(UInt64 *) override;
	virtual IOReturn reportEjectability(bool *) override;
	virtual IOReturn reportMaxValidBlock(UInt64 *) override;

	virtual IOReturn setWriteCacheState(bool enabled) override;
	virtual IOReturn getWriteCacheState(bool *enabled) override;
#if 0
	virtual void read(IOService *client, UInt64 byteStart,
	    IOMemoryDescriptor *buffer, IOStorageAttributes *attr,
	    IOStorageCompletion *completion);
	virtual void write(IOService *client, UInt64 byteStart,
	    IOMemoryDescriptor *buffer, IOStorageAttributes *attr,
	    IOStorageCompletion *completion);
#endif

protected:
private:
	/* These are declared class static to share across instances */
	const char *vendorString;
	const char *revisionString;
	const char *infoString;
	/* These are per-instance */
	const char *productString;
	uint64_t _pool_bcount;
	bool isReadOnly;
};

#endif /* ZFSDATASETPROXY_H_INCLUDED */
