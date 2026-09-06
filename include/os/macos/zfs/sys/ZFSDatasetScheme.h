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

#ifndef ZFSDATASETSCHEME_H_INCLUDED
#define	ZFSDATASETSCHEME_H_INCLUDED

#define	kZFSDatasetSchemeClass	"ZFSDatasetScheme"

#include <IOKit/storage/IOPartitionScheme.h>
#include <sys/ZFSDataset.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int zfs_osx_proxy_get_osname(const char *bsdname,
    char *osname, int len);
int zfs_osx_proxy_get_bsdname(const char *osname,
    char *bsdname, int len);


void zfs_osx_proxy_remove(const char *osname);
int zfs_osx_proxy_create(const char *osname);

#ifdef __cplusplus
} /* extern "C" */

/* Not C external */
ZFSDataset * zfs_osx_proxy_get(const char *osname);

class ZFSDatasetScheme : public IOPartitionScheme
{
	OSDeclareDefaultStructors(ZFSDatasetScheme);
public:

	virtual void free(void) override;
	virtual bool init(OSDictionary *properties) override;
	virtual bool start(IOService *provider) override;
	virtual IOService *probe(IOService *provider, SInt32 *score) override;

	bool addDataset(const char *osname);
	bool removeDataset(const char *osname, bool force);

	/* Compatibility shims */
	virtual void read(IOService *client,
	    UInt64		byteStart,
	    IOMemoryDescriptor	*buffer,
	    IOStorageAttributes	*attributes,
	    IOStorageCompletion	*completion) override;

	virtual void write(IOService *client,
	    UInt64		byteStart,
	    IOMemoryDescriptor	*buffer,
	    IOStorageAttributes	*attributes,
	    IOStorageCompletion	*completion) override;

#if defined(MAC_OS_X_VERSION_10_11) && \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	virtual IOReturn synchronize(IOService *client,
	    UInt64			byteStart,
	    UInt64			byteCount,
	    IOStorageSynchronizeOptions	options = 0) override;
#else
	virtual IOReturn synchronizeCache(IOService *client) override;
#endif

	virtual IOReturn unmap(IOService *client,
	    IOStorageExtent		*extents,
	    UInt32			extentsCount,
#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	    IOStorageUnmapOptions	options = 0) override;
#else
	    UInt32	options = 0) override;
#endif

	virtual bool lockPhysicalExtents(IOService *client) override;

	virtual IOStorage *copyPhysicalExtent(IOService *client,
	    UInt64 *    byteStart,
	    UInt64 *    byteCount) override;

	virtual void unlockPhysicalExtents(IOService *client) override;

#if defined(MAC_OS_X_VERSION_10_10) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_10)
	virtual IOReturn setPriority(IOService *client,
	    IOStorageExtent	*extents,
	    UInt32		extentsCount,
	    IOStoragePriority	priority) override;
#endif

protected:
private:
	OSSet		*_datasets;
	OSOrderedSet	*_holes;
	uint64_t	_max_id;

	uint32_t getNextPartitionID();
	void returnPartitionID(uint32_t part_id);
};

#endif /* __cplusplus */
#endif /* ZFSDATASETSCHEME_H_INCLUDED */
