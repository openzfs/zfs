//
//  ZetaPoolWatcher.mm
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.31.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#import "ZetaPoolWatcher.h"

#import <IOKit/pwr_mgt/IOPMLib.h>

#include <map>

CFStringRef powerAssertionName = CFSTR("ZFSScrub");
CFStringRef powerAssertionReason = CFSTR("ZFS Scrub in progress");

@interface ZetaPoolWatcher ()
{
	// ZFS
	std::vector<uint64_t> _knownPools;

	// Statistics
	std::map<uint64_t,zfs::VDevStat> _errorStats;

	// Sleep Prevention
	IOPMAssertionID assertionID;
	bool keptAwake;

	// Timing
	NSTimer * _autoUpdateTimer;
}

@end

bool containsMoreErrors(zfs::VDevStat const & a, zfs::VDevStat const & b)
{
	return b.errorRead > a.errorRead
		|| b.errorWrite > a.errorWrite
		|| b.errorChecksum > a.errorChecksum;
}

@implementation ZetaPoolWatcher

- (id)init
{
	if (self = [super init])
	{
		_autoUpdateTimer = [NSTimer timerWithTimeInterval:60
			target:self selector:@selector(timedUpdate:) userInfo:nil repeats:YES];
		_autoUpdateTimer.tolerance = 8;
		[[NSRunLoop currentRunLoop] addTimer:_autoUpdateTimer forMode:NSDefaultRunLoopMode];
		delegates = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)dealloc
{
	[_autoUpdateTimer invalidate];
	_autoUpdateTimer = nil;
	[self stopKeepingAwake];
}

- (void)timedUpdate:(NSTimer*)timer
{
	[self checkForChanges];
}

- (void)checkForChanges
{
	try
	{
		zfs::LibZFSHandle zfs;
		auto p = zfs.pools();
		[self checkForNewPools:p];
		[self checkForNewErrors:p];
		auto scrubCounter = [self countScrubsInProgress:p];
		auto sd = [NSUserDefaults standardUserDefaults];
		if (scrubCounter > 0 && [sd boolForKey:@"keepAwakeDuringScrub"])
			[self keepAwake];
		else
			[self stopKeepingAwake];
	}
	catch (std::exception const & e)
	{
		[self notifyError:e.what()];
	}
}

- (bool)checkDev:(zfs::NVList const &)dev
{
	auto guid = vdevGUID(dev);
	auto newStat = vdevStat(dev);
	auto & oldStat = _errorStats[guid];
	bool error = containsMoreErrors(oldStat, newStat);
	oldStat = newStat;
	return error;
}

- (bool)checkForNewErrors:(std::vector<zfs::ZPool> const &)pools
{
	try
	{
		for (auto && pool: pools)
		{
			auto vdevs = pool.vdevs();
			for (auto && vdev: vdevs)
			{
				if ([self checkDev:vdev])
				{
					[self notifyErrorInPool:pool.name()];
					return true;
				}

				auto devices = zfs::vdevChildren(vdev);
				for (auto && device: devices)
				{
					if ([self checkDev:device])
					{
						[self notifyErrorInPool:pool.name()];
						return true;
					}
				}
			}
		}
	}
	catch (std::exception const & e)
	{
		[self notifyError:e.what()];
	}
	return false;
}

- (void)notifyNewPoolDetected:(zfs::ZPool const &)pool
{
	for (id<ZetaPoolWatcherDelegate> d in [self delegates])
	{
		if ([d respondsToSelector:@selector(newPoolDetected:)])
		{
			[d newPoolDetected:pool];
		}
	}
}

- (void)notifyErrorInPool:(std::string const &)pool
{
	for (id<ZetaPoolWatcherDelegate> d in [self delegates])
	{
		if ([d respondsToSelector:@selector(errorDetectedInPool:)])
		{
			[d errorDetectedInPool:pool];
		}
	}
}

- (void)notifyError:(std::string const &)pool
{
	for (id<ZetaPoolWatcherDelegate> d in [self delegates])
	{
		if ([d respondsToSelector:@selector(errorDetected:)])
		{
			[d errorDetected:pool];
		}
	}
}

std::vector<uint64_t> poolsToGUID(std::vector<zfs::ZPool> const & pools)
{
	std::vector<uint64_t> guids(pools.size());
	std::transform(pools.begin(), pools.end(), guids.begin(),
				   [](auto const & pool) { return pool.guid(); });
	std::sort(guids.begin(), guids.end());
	return guids;
}

- (void)checkForNewPools:(std::vector<zfs::ZPool> const &)pools
{
	for (auto const & p : pools)
	{
		if (std::binary_search(_knownPools.begin(), _knownPools.end(), p.guid()))
		{
			// Already known pool
		}
		else
		{
			// New pool
			[self notifyNewPoolDetected:p];
		}
	}
	_knownPools = poolsToGUID(pools);
}

- (uint64_t)countScrubsInProgress:(std::vector<zfs::ZPool> const &)pools
{
	uint64_t scrubsInProgress = 0;
	try
	{
		for (auto && pool: pools)
		{
			auto vdevs = pool.vdevs();
			auto scan = pool.scanStat();
			if (scan.state == zfs::ScanStat::scanning)
				++scrubsInProgress;
		}
	}
	catch (std::exception const & e)
	{
		[self notifyError:e.what()];
	}
	return scrubsInProgress;
}

- (void)keepAwake
{
	if (!keptAwake)
	{
		IOReturn success = IOPMAssertionCreateWithDescription(
			kIOPMAssertPreventUserIdleSystemSleep,
			powerAssertionName, powerAssertionReason, 0, 0, 0, 0, &assertionID);
		if (success == kIOReturnSuccess)
		{
			keptAwake = true;
		}
		else
		{
			keptAwake = false;
			assertionID = 0;
		}
	}
}

- (void)stopKeepingAwake
{
	if (keptAwake)
	{
		IOReturn success = IOPMAssertionRelease(assertionID);
		if (success == kIOReturnSuccess)
		{
			keptAwake = false;
			assertionID = 0;
		}
	}
}

@synthesize delegates;

@end
