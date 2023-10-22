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
