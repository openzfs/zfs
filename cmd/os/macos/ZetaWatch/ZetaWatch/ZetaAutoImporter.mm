//
//  ZetaIDWatcher.cpp
//  ZetaWatch
//
//  Created by cbreak on 19.08.02.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaAutoImporter.h"

#include "IDDiskArbitrationDispatcher.hpp"
#include "IDDiskArbitrationHandler.hpp"
#include "IDDiskArbitrationUtils.hpp"

#include <vector>
#include <set>
#include <string>
#include <algorithm>

/*!
 If Auto-Import is configured:
 Discovered importable pools get classified into known and new pools. Pools that
 have not been seen recently are importable and are imported automatically. This
 set prevents attempting to import a pool multiple times in case of an error.
 Pools that were imported previously are added to the knownPools set and are
 not imported. They are ignored until one of their underlying devices disappears.
 */
@interface ZetaAutoImporter ()
{
	// ID
	ID::DiskArbitrationDispatcher _idDispatcher;
	NSTimer * checkTimer;

	// Management
	std::vector<zfs::ImportablePool> _importable;
	std::vector<zfs::ImportablePool> _importedBefore;
}

- (void)scheduleChecking;
- (void)handleDisappearedDevice:(ID::DiskInformation const &)info;

@end

class ZetaIDHandler : public ID::DiskArbitrationHandler
{
public:
	ZetaIDHandler(ZetaAutoImporter * watcher) : watcher(watcher)
	{
		scheduleChecking();
	}

public:
	virtual void diskAppeared(DADiskRef disk, ID::DiskInformation const & info)
	{
		scheduleChecking();
	}

	virtual void diskDisappeared(DADiskRef disk, ID::DiskInformation const & info)
	{
		scheduleChecking();
		[watcher handleDisappearedDevice:info];
	}

private:
	void scheduleChecking()
	{
		[watcher scheduleChecking];
	}

private:
	ZetaAutoImporter __weak * watcher;
};

@implementation ZetaAutoImporter

- (id)init
{
	if (self = [super init])
	{
		// ID
		_idDispatcher.addHandler(std::make_shared<ZetaIDHandler>(self));
		_idDispatcher.start();
		// Auto-Import handling
		[self seedKnownPools];
	}
	return self;
}

- (void)dealloc
{
	_idDispatcher.stop();
}

- (void)scheduleChecking
{
	if (checkTimer && [checkTimer isValid])
	{
		return;
	}
	checkTimer = [NSTimer timerWithTimeInterval:4 target:self
		selector:@selector(checkForImportablePools) userInfo:nil repeats:NO];
	checkTimer.tolerance = 2;
	[[NSRunLoop currentRunLoop] addTimer:checkTimer forMode:NSDefaultRunLoopMode];
}

- (void)handleDisappearedDevice:(ID::DiskInformation const &)info
{
	if (info.mediaBSDName.empty())
		return;
	std::string devicePath = "/dev/" + info.mediaBSDName;
	// Forget pools that were once importable but now are no longer since at
	// least one device was removed
	_importedBefore.erase(
		std::remove_if(_importedBefore.begin(), _importedBefore.end(),
					   [&](zfs::ImportablePool const & pool)
	{
		auto const & devices = pool.devices;
		return std::find(devices.begin(), devices.end(), devicePath) != devices.end();
	}), _importedBefore.end());
}

- (void)checkForImportablePools
{
	auto defaults = [NSUserDefaults standardUserDefaults];
	NSMutableDictionary * importData = [[NSMutableDictionary alloc] init];
	if (auto spo = [defaults arrayForKey:@"searchPathOverride"])
	{
		[importData setObject:spo forKey:@"searchPathOverride"];
	}
	[_authorization importablePools:importData withReply:
	 ^(NSError * error, NSArray * importablePools)
	 {
		if (error)
			[self notifyErrorFromHelper:error];
		else
			[self handleImportablePools:importablePools];
	 }];
}

std::vector<std::string> arrayToStringVec(NSArray<NSString*> * stringArray)
{
	std::vector<std::string> strings;
	for (NSString * string in stringArray)
	{
		strings.push_back([string UTF8String]);
	}
	return strings;
}

std::vector<zfs::ImportablePool> arrayToPoolVec(NSArray * poolsArray)
{
	std::vector<zfs::ImportablePool> pools;
	for (NSDictionary * poolDict in poolsArray)
	{
		pools.push_back({
			[poolDict[@"name"] UTF8String],
			[poolDict[@"guid"] unsignedLongLongValue],
			[poolDict[@"status"] unsignedLongLongValue],
			arrayToStringVec(poolDict[@"devices"]),
		});
	}
	std::sort(pools.begin(), pools.end());
	return pools;
}

- (void)seedKnownPools
{
	zfs::LibZFSHandle lib;
	std::vector<zfs::ImportablePool> knownPools;
	for (auto const & pool : lib.pools())
	{
		knownPools.push_back({
			pool.name(),
			pool.guid(),
			pool.status(),
			lib.devicesFromPoolConfig(pool.config()),
		});
	}
	std::sort(knownPools.begin(), knownPools.end());
	_importedBefore = std::move(knownPools);
}

- (void)handleImportablePools:(NSArray*)importablePools
{
	std::vector<zfs::ImportablePool> importableCurrent = arrayToPoolVec(importablePools);
	// Find the pools that had not been imported before, for auto import
	std::vector<zfs::ImportablePool> importableNew;
	std::set_difference(importableCurrent.begin(), importableCurrent.end(),
						_importedBefore.begin(), _importedBefore.end(),
						std::back_inserter(importableNew));
	auto importedPools = [self handleNewImportablePools:importableNew];
	// Aggregate all known pools to prevent double-auto import
	std::vector<zfs::ImportablePool> importedBefore;
	std::set_union(_importedBefore.begin(), _importedBefore.end(),
				   importedPools.begin(), importedPools.end(),
				   std::back_inserter(importedBefore));
	// Update currently importable pools collection
	_importable = std::move(importableCurrent);
	_importedBefore = std::move(importedBefore);
}

- (std::vector<zfs::ImportablePool>)handleNewImportablePools:(std::vector<zfs::ImportablePool> const &)importableNew
{
	std::vector<zfs::ImportablePool> importedPools;
	auto defaults = [NSUserDefaults standardUserDefaults];
	bool allowHostIDMismatch = [defaults boolForKey:@"allowHostIDMismatch"];
	if ([defaults boolForKey:@"autoImport"])
	{
		for (auto const & pool : importableNew)
		{
			if (!zfs::healthy(pool.status, allowHostIDMismatch))
				continue; // skip pools that aren't healthy
			NSNumber * guid = [NSNumber numberWithUnsignedLongLong:pool.guid];
			NSString * name = [NSString stringWithUTF8String:pool.name.c_str()];
			NSString * title = [NSString stringWithFormat:
				NSLocalizedString(@"Auto-importing %@",
								  @"Pool AutoImport short format"),
					name];
			NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"Auto-importing Pool %@ (%@)",
								  @"Pool AutoImport format"),
								name, guid];
			[self notifySuccessWithTitle:title text:text];
			NSDictionary * poolDict = @{ @"poolGUID": guid, @"poolName": name};
			NSMutableDictionary * mutablePool = [poolDict mutableCopy];
			[mutablePool setValue:[NSNumber numberWithBool:allowHostIDMismatch]
						   forKey:@"allowHostIDMismatch"];
			if ([defaults boolForKey:@"useAltroot"])
			{
				[mutablePool setObject:[defaults stringForKey:@"defaultAltroot"]
								forKey:@"altroot"];
			}
			if (auto spo = [defaults arrayForKey:@"searchPathOverride"])
			{
				[mutablePool setObject:spo forKey:@"searchPathOverride"];
			}
			[_authorization importPools:mutablePool withReply:^(NSError * error)
			 {
				 [self handlePoolImportReply:error forPool:poolDict];
			 }];
			importedPools.push_back(pool);
		}
	}
	return importedPools;
}

- (void)handlePoolImportReply:(NSError*)error forPool:(NSDictionary*)pool
{
	if (error)
	{
		[self notifyErrorFromHelper:error];
	}
	else
	{
		NSString * title = [NSString stringWithFormat:
			NSLocalizedString(@"Pool %@ auto-imported",
							  @"Pool AutoImport Success short format"),
				pool[@"poolName"]];
		NSString * text = [NSString stringWithFormat:
			NSLocalizedString(@"Pool %@ (%@) auto-imported",
							  @"Pool AutoImport Success format"),
			pool[@"poolName"], pool[@"poolGUID"]];
		[self notifySuccessWithTitle:title text:text];
	}
}

- (std::vector<zfs::ImportablePool> const &)importablePools
{
	return _importable;
}

@end
