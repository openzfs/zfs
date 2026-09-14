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

#ifndef ZFSDATASET_H_INCLUDED
#define	ZFSDATASET_H_INCLUDED

#ifdef __cplusplus

#include <IOKit/storage/IOMedia.h>
#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

#ifdef super
#undef super
#endif
#define	super IOMedia

// #define	kZFSContentHint		"6A898CC3-1DD2-11B2-99A6-080020736631"
#define	kZFSContentHint		"ZFS_Dataset"

#define	kZFSIOMediaPrefix	"ZFS "
#define	kZFSIOMediaSuffix	" Media"
#define	kZFSDatasetNameKey	"ZFS Dataset"
#define	kZFSDatasetClassKey	"ZFSDataset"

class ZFSDataset : public IOMedia
{
	OSDeclareDefaultStructors(ZFSDataset)
public:
#if 0
	/* XXX Only for debug tracing */
	virtual bool open(IOService *client,
	    IOOptionBits options, IOStorageAccess access = 0);
	virtual bool isOpen(const IOService *forClient = 0) const;
	virtual void close(IOService *client,
	    IOOptionBits options);

	virtual bool handleOpen(IOService *client,
	    IOOptionBits options, void *access);
	virtual bool handleIsOpen(const IOService *client) const;
	virtual void handleClose(IOService *client,
	    IOOptionBits options);

	virtual bool attach(IOService *provider);
	virtual void detach(IOService *provider);

	virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);
#endif

	virtual bool init(UInt64 base, UInt64 size,
	    UInt64 preferredBlockSize,
	    IOMediaAttributeMask attributes,
	    bool isWhole, bool isWritable,
	    const char *contentHint = 0,
	    OSDictionary *properties = 0) override;
	virtual void free() override;

	static ZFSDataset * withDatasetNameAndSize(const char *name,
	    uint64_t size);

	virtual void read(IOService *client,
	    UInt64 byteStart, IOMemoryDescriptor *buffer,
	    IOStorageAttributes *attributes,
	    IOStorageCompletion *completion) override;
	virtual void write(IOService *client,
	    UInt64 byteStart, IOMemoryDescriptor *buffer,
	    IOStorageAttributes *attributes,
	    IOStorageCompletion *completion) override;

#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	virtual IOReturn synchronize(IOService *client,
	    UInt64 byteStart, UInt64 byteCount,
	    IOStorageSynchronizeOptions options = 0) override;
#else
	virtual IOReturn synchronizeCache(IOService *client) override;
#endif

	virtual IOReturn unmap(IOService *client,
	    IOStorageExtent *extents, UInt32 extentsCount,
#if defined(MAC_OS_X_VERSION_10_11) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
	    IOStorageUnmapOptions	options = 0) override;
#else
	    UInt32	options = 0);
#endif

	virtual bool lockPhysicalExtents(IOService *client) override;
	virtual IOStorage *copyPhysicalExtent(IOService *client,
	    UInt64 *byteStart, UInt64 *byteCount) override;
	virtual void unlockPhysicalExtents(IOService *client) override;

#if defined(MAC_OS_X_VERSION_10_10) &&        \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_10)
	virtual IOReturn setPriority(IOService *client,
	    IOStorageExtent *extents, UInt32 extentsCount,
	    IOStoragePriority priority) override;
#endif

	virtual UInt64 getPreferredBlockSize() const override;
	virtual UInt64 getSize() const override;
	virtual UInt64 getBase() const override;

	virtual bool isEjectable() const override;
	virtual bool isFormatted() const override;
	virtual bool isWhole() const override;
	virtual bool isWritable() const override;

	virtual const char *getContent() const override;
	virtual const char *getContentHint() const override;
	virtual IOMediaAttributeMask getAttributes() const override;

protected:
private:
	bool setDatasetName(const char *);
};

#endif /* __cplusplus */

#endif /* ZFSDATASET_H_INCLUDED */
