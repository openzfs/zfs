//
//  ZetaMainMenu.mm
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.20.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#import "ZetaMainMenu.h"
#import "ZetaImportMenu.h"
#import "ZetaPoolWatcher.h"
#import "ZetaAuthorization.h"
#import "ZetaSnapshotMenu.h"
#import "ZetaBookmarkMenu.h"
#import "ZetaFileSystemPropertyMenu.h"
#import "ZetaPoolPropertyMenu.h"
#import "ZetaNotificationCenter.h"

#include "ZFSUtils.hpp"
#include "ZFSStrings.hpp"

#include "InvariantDisks/IDDiskArbitrationUtils.hpp"

#include <type_traits>
#include <iomanip>
#include <sstream>
#include <chrono>

@interface ZetaMainMenu ()
{
	NSMutableArray * _dynamicMenus;
	DASessionRef _diskArbitrationSession;
	zfs::LibZFSHandle _zfs;
}

@end

@implementation ZetaMainMenu

- (id)init
{
	if (self = [super init])
	{
		_dynamicMenus = [[NSMutableArray alloc] init];
		_diskArbitrationSession = DASessionCreate(nullptr);
	}
	return self;
}

- (void)dealloc
{
	CFRelease(_diskArbitrationSession);
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
	[self clearDynamicMenu:menu];
	[self resetLibZFS];
	[self createNotificationMenu:menu];
	[self createPoolMenu:menu];
	[self createActionMenu:menu];
}

#pragma mark Formating

NSString * formatErrorStat(zfs::VDevStat stat, bool emoji)
{
	NSString * status = emoji ?
		zfs::emojistring_vdev_state_t(stat.state, stat.aux) :
		zfs::localized_describe_vdev_state_t(stat.state, stat.aux);
	NSString * errors = nil;
	if (stat.errorRead == 0 && stat.errorWrite == 0 && stat.errorChecksum == 0)
	{
		errors = NSLocalizedString(@"No Errors", @"Format vdev_stat_t");
	}
	else
	{
		NSString * format = NSLocalizedString(@"%llu Read Errors, %llu Write Errors, %llu Checksum Errors", @"Format vdev_stat_t");
		errors = [NSString stringWithFormat:format, stat.errorRead, stat.errorWrite, stat.errorChecksum];
	}
	return [NSString stringWithFormat:@"%@, %@", status, errors];
}

std::chrono::seconds getElapsed(zfs::ScanStat const & scanStat)
{
	auto elapsed = time(0) - scanStat.passStartTime;
	elapsed -= scanStat.passPausedSeconds;
	elapsed = (elapsed > 0) ? elapsed : 1;
	return std::chrono::seconds(elapsed);
}

inline std::string formatTimeRemaining(zfs::ScanStat const & scanStat, std::chrono::seconds const & time)
{
	auto bytesRemaining = scanStat.total - scanStat.issued;
	auto issued = scanStat.passIssued;
	if (issued == 0)
		issued = 1;
	auto secondsRemaining = bytesRemaining * time.count() / issued;
	std::stringstream ss;
	ss << std::setfill('0');
	ss << (secondsRemaining / (60*60*24)) << " days "
	<< std::setw(2) << ((secondsRemaining / (60*60)) % 24) << ":"
	<< std::setw(2) << ((secondsRemaining / 60) % 60) << ":"
	<< std::setw(2) << (secondsRemaining % 60);
	return ss.str();
}

#pragma mark ZFS Inspection

NSMenu * createFSMenu(zfs::ZFileSystem && fs, ZetaMainMenu * delegate)
{
	NSMenu * fsMenu = [[NSMenu alloc] init];
	[fsMenu setAutoenablesItems:NO];
	NSString * fsName = [NSString stringWithUTF8String:fs.name()];
	auto addFSCommand = [&](NSString * title, SEL selector)
	{
		auto item = [fsMenu addItemWithTitle:title
									  action:selector keyEquivalent:@""];
		item.representedObject = fsName;
		item.target = delegate;
	};
	if (fs.type() == zfs::ZFileSystem::FSType::filesystem)
	{
		auto [encRoot, isRoot] = fs.encryptionRoot();
		if (isRoot)
		{
			if (fs.keyStatus() != zfs::ZFileSystem::KeyStatus::available)
			{
				addFSCommand(NSLocalizedString(@"Load Key...", @"Load Key"), @selector(loadKey:));
			}
			else
			{
				addFSCommand(NSLocalizedString(@"Unload Key", @"Unload Key"), @selector(unloadKey:));
			}
			[fsMenu addItem:[NSMenuItem separatorItem]];
		}
		addFSCommand(NSLocalizedString(@"Mount Recursively", @"Mount Recursively"), @selector(mountFilesystemRecursive:));
		if (!fs.mounted() && fs.mountable())
		{
			addFSCommand(NSLocalizedString(@"Mount", @"Mount"), @selector(mountFilesystem:));
		}
		addFSCommand(NSLocalizedString(@"Unmount Recursively", @"Unmount Recursively"), @selector(unmountFilesystemRecursive:));
		if (fs.mounted())
		{
			addFSCommand(NSLocalizedString(@"Unmount", @"Unmount"), @selector(unmountFilesystem:));
			addFSCommand(NSLocalizedString(@"Unmount (Force)", @"Unmount (Force)"), @selector(unmountFilesystemForce:));
		}
	}
	// Snapshots
	[fsMenu addItem:[NSMenuItem separatorItem]];
	addFSCommand(NSLocalizedString(@"Snapshot...", @"Snapshot"), @selector(snapshotFilesystem:));
	addFSCommand(NSLocalizedString(@"Snapshot Recursively...", @"Snapshot Recursively"), @selector(snapshotFilesystemRecursive:));
	{
		// Snapshots submenu
		NSString * snapsTitle = NSLocalizedString(@"Snapshots", @"Snapshots");
		NSMenu * snaps = [[NSMenu alloc] initWithTitle:snapsTitle];
		ZetaSnapshotMenu * sd = [[ZetaSnapshotMenu alloc] initWithFileSystem:zfs::ZFileSystem(fs) delegate:delegate];
		snaps.delegate = sd;
		NSMenuItem * snapsItem = [[NSMenuItem alloc] initWithTitle:snapsTitle
			action:nullptr keyEquivalent:@""];
		snapsItem.submenu = snaps;
		snapsItem.representedObject = sd;
		[fsMenu addItem:snapsItem];
	}
	{
		// Bookmarks Submenu
		NSString * bookmarksTitle = NSLocalizedString(@"Bookmarks", @"Bookmarks");
		NSMenu * bookmarks = [[NSMenu alloc] initWithTitle:bookmarksTitle];
		ZetaBookmarkMenu * bd = [[ZetaBookmarkMenu alloc] initWithFileSystem:zfs::ZFileSystem(fs) delegate:delegate];
		bookmarks.delegate = bd;
		NSMenuItem * bookmarksItem = [[NSMenuItem alloc] initWithTitle:bookmarksTitle
			action:nullptr keyEquivalent:@""];
		bookmarksItem.submenu = bookmarks;
		bookmarksItem.representedObject = bd;
		[fsMenu addItem:bookmarksItem];
	}
	// Create
	[fsMenu addItem:[NSMenuItem separatorItem]];
	addFSCommand(NSLocalizedString(@"Create Filesystem...", @"Create Filesystem..."),
		@selector(createFilesystem:));
	addFSCommand(NSLocalizedString(@"Create Volume...", @"Create Volume..."),
		@selector(createVolume:));
	// Destroy
	[fsMenu addItem:[NSMenuItem separatorItem]];
	if (!fs.isRoot())
	{
		addFSCommand(NSLocalizedString(@"Destroy", @"Destroy"), @selector(destroy:));
	}
	addFSCommand(NSLocalizedString(@"Destroy Recursively", @"Destroy Recursively"), @selector(destroyRecursive:));
	// Selected Properties
	[fsMenu addItem:[NSMenuItem separatorItem]];
	addMenuItem(fsMenu, delegate,
				NSLocalizedString(@"Available:          \t %s", @"FS Available Menu Entry"),
				formatBytes(fs.available()));
	addMenuItem(fsMenu, delegate,
				NSLocalizedString(@"Used:               \t %s", @"FS Used Menu Entry"),
				formatBytes(fs.used()));
	addMenuItem(fsMenu, delegate,
				NSLocalizedString(@"Referenced:         \t %s", @"FS Referenced Menu Entry"),
				formatBytes(fs.referenced()));
	addMenuItem(fsMenu, delegate,
				NSLocalizedString(@"Logical Used:       \t %s", @"FS Logically Used Menu Entry"),
				formatBytes(fs.logicalused()));
	addMenuItem(fsMenu, delegate,
				NSLocalizedString(@"Compression Ratio:  \t %1.2fx", @"FS Compression Menu Entry"),
				fs.compressRatio());
	addMenuItem(fsMenu, delegate,
				NSLocalizedString(@"Mount Point:        \t %s", @"FS Mountpoint Menu Entry"),
				fs.mountpoint());
	// All Properties
	NSString * allPropsTitle = NSLocalizedString(@"All Properties", @"All Properties");
	NSMenu * allProps = [[NSMenu alloc] initWithTitle:allPropsTitle];
	ZetaFileSystemPropertyMenu * pd = [[ZetaFileSystemPropertyMenu alloc] initWithFileSystem:std::move(fs)];
	allProps.delegate = pd;
	NSMenuItem * allPropsItem = [[NSMenuItem alloc] initWithTitle:allPropsTitle
		action:nullptr keyEquivalent:@""];
	allPropsItem.submenu = allProps;
	allPropsItem.representedObject = pd;
	[fsMenu addItem:allPropsItem];
	return fsMenu;
}

NSString * formatStatus(zfs::ZFileSystem const & fs)
{
	NSString * mountStatus = fs.mounted() ?
		NSLocalizedString(@"📌", @"mounted status") :
		NSLocalizedString(@"🕳", @"unmounted status");
	NSString * encStatus = nil;
	switch (fs.keyStatus())
	{
		case zfs::ZFileSystem::KeyStatus::none:
			encStatus = @"";
			break;
		case zfs::ZFileSystem::KeyStatus::unavailable:
			encStatus = NSLocalizedString(@"🔒", @"locked status");
			break;
		case zfs::ZFileSystem::KeyStatus::available:
			encStatus = NSLocalizedString(@"🔑", @"unlocked status");
			break;
	}
	auto [encRoot, isRoot] = fs.encryptionRoot();
	NSString * encRootStr = isRoot ? @"🎁" : @"";
	NSString * fsLine = [NSString stringWithFormat:NSLocalizedString(@"%s (%@%@%@)", @"File System Menu Entry"), fs.name(), mountStatus, encStatus, encRootStr];
	return fsLine;
}

NSMenuItem * addVdev(zfs::ZPool const & pool, zfs::NVList const & device,
	NSMenu * menu, DASessionRef daSession, ZetaMainMenu * delegate)
{
	// Menu Item
	auto stat = zfs::vdevStat(device);
	auto item = addMenuItem(menu, delegate, NSLocalizedString(@"%s (%@)", @"Device Menu Entry"),
							pool.vdevName(device), formatErrorStat(stat, true));
	// Submenu
	// ZFS Info
	NSMenu * subMenu = [[NSMenu alloc] init];
	addMenuItem(subMenu, delegate, formatErrorStat(stat, false));
	addMenuItem(subMenu, delegate,
				NSLocalizedString(@"Space:          \t %s used / %s total", @"VDev Space Menu Entry"),
				formatBytes(stat.alloc), formatBytes(stat.space));
	addMenuItem(subMenu, delegate,
				NSLocalizedString(@"Fragmentation:  \t %llu%%", @"VDev Fragmentation Menu Entry"),
				stat.fragmentation);
	addMenuItem(subMenu, delegate,
				NSLocalizedString(@"VDev GUID:      \t %llu", @"VDev GUID Menu Entry"),
				zfs::vdevGUID(device));
	std::string type = zfs::vdevType(device);
	addMenuItem(subMenu, delegate,
				NSLocalizedString(@"Device:         \t %s (%s)", @"VDev Device Menu Entry"),
				pool.vdevDevice(device), type);
	// Disk Info, only if state is at least 5 or higher, (FAULTED, DEGRADED, HEALTHY)
	if (type == "disk" && stat.state >= 5)
	{
		[subMenu addItem:[NSMenuItem separatorItem]];
		auto devicePath = pool.vdevDevice(device);
		DADiskRef daDisk = DADiskCreateFromBSDName(nullptr, daSession, devicePath.c_str());
		auto diskInfo = ID::getDiskInformation(daDisk);
		addMenuItem(subMenu, delegate,
					NSLocalizedString(@"UUID:           \t %s", @"VDev MediaUUID Menu Entry"), diskInfo.mediaUUID);
		addMenuItem(subMenu, delegate,
					NSLocalizedString(@"Model:          \t %s", @"VDev Model Menu Entry"), trim(diskInfo.deviceModel));
		addMenuItem(subMenu, delegate,
					NSLocalizedString(@"Serial:         \t %s", @"VDev Serial Menu Entry"), trim(diskInfo.ioSerial));
		CFRelease(daDisk);
	}
	item.submenu = subMenu;
	return item;
}

void createScrubMenu(zfs::ZPool & pool, ZetaMainMenu * delegate, NSMenu * vdevMenu)
{
	// Scrub
	auto scrub = pool.scanStat();
	auto startDate = [NSDate dateWithTimeIntervalSince1970:scrub.scanStartTime];
	auto endDate = [NSDate dateWithTimeIntervalSince1970:scrub.scanEndTime];
	auto startString = [NSDateFormatter localizedStringFromDate:startDate dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterMediumStyle];
	auto endString = [NSDateFormatter localizedStringFromDate:endDate dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterMediumStyle];
	NSString * scanLine0;
	switch (scrub.state)
	{
		case zfs::ScanStat::stateNone:
		{
			scanLine0 = [NSString stringWithFormat:NSLocalizedString(
				@"Never scrubbed", @"Scrub None")];
			break;
		}
		case zfs::ScanStat::scanning:
		{
			scanLine0 = [NSString stringWithFormat:NSLocalizedString(
				@"Scrub started %@ still in progress", @"Scrub Scanning"),
				startString];
			break;
		}
		case zfs::ScanStat::finished:
		{
			scanLine0 = [NSString stringWithFormat:NSLocalizedString(
				@"Scrub started %@ finished successfully at %@", @"Scrub Finished"),
				startString, endString];
			break;
		}
		case zfs::ScanStat::canceled:
		{
			scanLine0 = [NSString stringWithFormat:NSLocalizedString(
				@"Scrub started %@ was canceled at $@", @"Scrub Canceled"),
				startString, endString];
			break;
		}
	}
	auto scrubItem = [vdevMenu addItemWithTitle:scanLine0 action:nullptr keyEquivalent:@""];
	auto scrubMenu = [[NSMenu alloc] init];
	scrubItem.submenu = scrubMenu;
	NSString * poolName = [NSString stringWithUTF8String:pool.name()];
	if (scrub.state == zfs::ScanStat::scanning && scrub.passPauseTime == 0)
	{
		auto item = [scrubMenu addItemWithTitle:
			NSLocalizedString(@"Stop Scrub", @"Stop Scrub")
			action:@selector(scrubStopPool:) keyEquivalent:@""];
		item.representedObject = poolName;
		item.target = delegate;
		item = [scrubMenu addItemWithTitle:
			NSLocalizedString(@"Pause Scrub", @"Pause Scrub")
			action:@selector(scrubPausePool:) keyEquivalent:@""];
		item.representedObject = poolName;
		item.target = delegate;
	}
	else
	{
		auto item = [scrubMenu addItemWithTitle:
			NSLocalizedString(@"Start Scrub", @"Start Scrub")
			action:@selector(scrubPool:) keyEquivalent:@""];
		item.representedObject = poolName;
		item.target = delegate;
	}
	if (scrub.state == zfs::ScanStat::scanning)
	{
		// Scan Stats
		auto elapsed = getElapsed(scrub);
		if (scrub.passPauseTime != 0)
		{
			auto pauseDate = [NSDate dateWithTimeIntervalSince1970:scrub.passPauseTime];
			auto pauseString = [NSDateFormatter localizedStringFromDate:pauseDate dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterMediumStyle];
			NSString * scanLinePaused = [NSString stringWithFormat:NSLocalizedString(
				@"Scrub Paused at %@", @"Scrub Paused"), pauseString];
			auto m = [vdevMenu addItemWithTitle:scanLinePaused action:nullptr keyEquivalent:@""];
			m.indentationLevel = 1;
		}
		NSString * scanLine1 = [NSString stringWithFormat:NSLocalizedString(
			@"%s scanned at %s, %s issued at %s", @"Scrub Menu Entry 1"),
								formatBytes(scrub.scanned).c_str(),
								formatRate(scrub.passScanned, elapsed).c_str(),
								formatBytes(scrub.issued).c_str(),
								formatRate(scrub.passIssued, elapsed).c_str()];
		NSString * scanLine2 = [NSString stringWithFormat:NSLocalizedString(
			@"%s total, %0.2f %% done, %s remaining, %llu errors", @"Scrub Menu Entry 2"),
								formatBytes(scrub.total).c_str(),
								100.0*scrub.issued/scrub.total,
								formatTimeRemaining(scrub, elapsed).c_str(),
								scrub.errors];
		auto m1 = [vdevMenu addItemWithTitle:scanLine1 action:nullptr keyEquivalent:@""];
		auto m2 = [vdevMenu addItemWithTitle:scanLine2 action:nullptr keyEquivalent:@""];
		m1.indentationLevel = 1;
		m2.indentationLevel = 1;
	}
}

NSMenu * createVdevMenu(zfs::ZPool && pool, ZetaMainMenu * delegate, DASessionRef daSession)
{
	NSMenu * vdevMenu = [[NSMenu alloc] init];
	[vdevMenu setAutoenablesItems:NO];
	try
	{
		createScrubMenu(pool, delegate, vdevMenu);
		[vdevMenu addItem:[NSMenuItem separatorItem]];
		// VDevs
		auto vdevs = pool.vdevs();
		for (auto && vdev: vdevs)
		{
			// VDev
			addVdev(pool, vdev, vdevMenu, daSession, delegate);
			// Children
			auto devices = zfs::vdevChildren(vdev);
			for (auto && device: devices)
			{
				auto item = addVdev(pool, device, vdevMenu, daSession, delegate);
				[item setIndentationLevel:1];
			}
		}
		// Caches
		auto caches = pool.caches();
		if (caches.size() > 0)
		{
			[vdevMenu addItemWithTitle:@"cache" action:nullptr keyEquivalent:@""];
			for (auto && cache: caches)
			{
				auto item = addVdev(pool, cache, vdevMenu, daSession, delegate);
				[item setIndentationLevel:1];
			}
		}
		// Filesystems
		[vdevMenu addItem:[NSMenuItem separatorItem]];
		auto childFileSystems = pool.allFileSystems();
		if (childFileSystems.empty())
		{
			// This seems to happen when a pool is UNAVAIL
			NSMenuItem * item = [vdevMenu addItemWithTitle:@"No Filesystems!" action:nil keyEquivalent:@""];
			[item setEnabled:NO];
		}
		else
		{
			for (auto & fs : childFileSystems)
			{
				auto fsLine = formatStatus(fs);
				NSMenuItem * item = [vdevMenu addItemWithTitle:fsLine action:nullptr keyEquivalent:@""];
				item.representedObject = [NSString stringWithUTF8String:fs.name()];
				item.submenu = createFSMenu(std::move(fs), delegate);
			}
		}
		// Command helper
		NSString * poolName = [NSString stringWithUTF8String:pool.name()];
		auto addRootFSCommand = [&](NSString * title, SEL selector)
		{
			auto item = [vdevMenu addItemWithTitle:title
				action:selector keyEquivalent:@""];
			item.representedObject = poolName;
			item.target = delegate;
		};
		// Mount Recursively
		[vdevMenu addItem:[NSMenuItem separatorItem]];
		addRootFSCommand(NSLocalizedString(@"Mount Recursively", @"Mount Recursively"),
						 @selector(mountFilesystemRecursive:));
		addRootFSCommand(NSLocalizedString(@"Unmount Recursively", @"Unmount Recursively"),
						 @selector(unmountFilesystemRecursive:));
		// Snapshot
		[vdevMenu addItem:[NSMenuItem separatorItem]];
		addRootFSCommand(NSLocalizedString(@"Snapshot Recursively...", @"Snapshot Recursively"),
						 @selector(snapshotFilesystemRecursive:));
		// Create
		[vdevMenu addItem:[NSMenuItem separatorItem]];
		addRootFSCommand(NSLocalizedString(@"Create Filesystem...", @"Create Filesystem..."),
						 @selector(createFilesystem:));
		addRootFSCommand(NSLocalizedString(@"Create Volume...", @"Create Volume..."),
						 @selector(createVolume:));
		// Export Actions
		[vdevMenu addItem:[NSMenuItem separatorItem]];
		addRootFSCommand(NSLocalizedString(@"Export", @"Export"),
						 @selector(exportPool:));
		addRootFSCommand(NSLocalizedString(@"Export (Force)", @"Export (Force)"),
						 @selector(exportPoolForce:));
		// All Properties
		[vdevMenu addItem:[NSMenuItem separatorItem]];
		NSMenu * allProps = [[NSMenu alloc] initWithTitle:@"All Properties"];
		ZetaPoolPropertyMenu * pd = [[ZetaPoolPropertyMenu alloc] initWithPool:std::move(pool)];
		allProps.delegate = pd;
		NSMenuItem * allPropsItem = [[NSMenuItem alloc] initWithTitle:@"All Properties" action:nullptr keyEquivalent:@""];
		allPropsItem.submenu = allProps;
		allPropsItem.representedObject = pd;
		[vdevMenu addItem:allPropsItem];
	}
	catch (std::exception const & e)
	{
		[vdevMenu addItemWithTitle:NSLocalizedString(@"Error reading pool configuration", @"Pool Config Error Message")
							action:nullptr keyEquivalent:@""];
	}
	return vdevMenu;
}

- (void)createNotificationMenu:(NSMenu*)menu
{
	if ([self.notificationCenter.inProgressActions count] > 0)
	{
		NSUInteger notifIdx = 0;
		for (ZetaNotification * notification in self.notificationCenter.inProgressActions)
		{
			NSMenuItem * notifItem = [[NSMenuItem alloc] initWithTitle:notification.title action:nil keyEquivalent:@""];
			[menu insertItem:notifItem atIndex:0];
			[_dynamicMenus addObject:notifItem];
			++notifIdx;
		}
		NSMenuItem * sepItem = [NSMenuItem separatorItem];
		[menu insertItem:sepItem atIndex:notifIdx];
		[_dynamicMenus addObject:sepItem];
	}
}

- (void)createPoolMenu:(NSMenu*)menu
{
	NSInteger poolMenuIdx = [menu indexOfItemWithTag:ZPoolAnchorMenuTag];
	if (poolMenuIdx < 0)
		return;
	NSInteger poolItemRootIdx = poolMenuIdx + 1;
	NSUInteger poolIdx = 0;
	try
	{
		for (auto && pool: _zfs.pools())
		{
			NSString * poolLine = [NSString stringWithFormat:NSLocalizedString(@"%s (%@)", @"Pool Menu Entry"),
								   pool.name(), zfs::emojistring_pool_status_t(pool.status())];
			NSMenuItem * poolItem = [[NSMenuItem alloc] initWithTitle:poolLine action:NULL keyEquivalent:@""];
			NSMenu * vdevMenu = createVdevMenu(std::move(pool), self, _diskArbitrationSession);
			[poolItem setSubmenu:vdevMenu];
			[menu insertItem:poolItem atIndex:poolItemRootIdx + poolIdx];
			[_dynamicMenus addObject:poolItem];
			++poolIdx;
		}
	}
	catch (std::exception const & e)
	{
		NSString * error = [NSString stringWithFormat:@"Exception during pool iteration: %s", e.what()];
		NSMenuItem * errorItem = [[NSMenuItem alloc] initWithTitle:error action:nullptr keyEquivalent:@""];
		[menu insertItem:errorItem atIndex:poolItemRootIdx];
		[_dynamicMenus addObject:errorItem];
	}
}

- (void)createActionMenu:(NSMenu*)menu
{
	NSInteger actionMenuIdx = [menu indexOfItemWithTag:ActionAnchorMenuTag];
	if (actionMenuIdx < 0)
		return;
	// Unlock
	NSMenuItem * unlockItem = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Load Keys...", @"Load Key Menu Entry") action:NULL keyEquivalent:@""];
	NSMenu * unlockMenu = [[NSMenu alloc] init];
	[unlockItem setSubmenu:unlockMenu];
	NSMutableArray<NSString*> * lockedEncryptionRoots = [NSMutableArray array];
	NSMenuItem * unlockAllItem = [unlockMenu addItemWithTitle:NSLocalizedString(@"Load All Keys...", @"Load All Menu Entry") action:@selector(loadAllKeys:) keyEquivalent:@""];
	unlockAllItem.target = self;
	unlockAllItem.representedObject = lockedEncryptionRoots;
	[unlockMenu addItem:[NSMenuItem separatorItem]];
	// Lock
	NSMenuItem * lockItem = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Unload Keys", @"Unload Key Menu Entry") action:NULL keyEquivalent:@""];
	NSMenu * lockMenu = [[NSMenu alloc] init];
	[lockItem setSubmenu:lockMenu];
	NSMutableArray<NSString*> * unlockedEncryptionRoots = [NSMutableArray array];
	NSMenuItem * lockAllItem = [lockMenu addItemWithTitle:NSLocalizedString(@"Unload All Keys", @"Unload All Menu Entry") action:@selector(unloadAllKeys:) keyEquivalent:@""];
	lockAllItem.target = self;
	lockAllItem.representedObject = unlockedEncryptionRoots;
	[lockMenu addItem:[NSMenuItem separatorItem]];
	// Individual entries
	try
	{
		for (auto && pool: _zfs.pools())
		{
			for (auto & fs : pool.allFileSystems())
			{
				auto [encRoot, isRoot] = fs.encryptionRoot();
				auto keyStatus = fs.keyStatus();
				if (isRoot)
				{
					NSString * fsName = [NSString stringWithUTF8String:fs.name()];
					if (keyStatus == zfs::ZFileSystem::KeyStatus::unavailable)
					{
						NSMenuItem * item = [unlockMenu addItemWithTitle:fsName
																  action:@selector(loadKey:) keyEquivalent:@""];
						item.representedObject = fsName;
						item.target = self;
						[lockedEncryptionRoots addObject:fsName];
					}
					else
					{
						NSMenuItem * item = [lockMenu addItemWithTitle:fsName
																action:@selector(unloadKey:) keyEquivalent:@""];
						item.representedObject = fsName;
						item.target = self;
						[unlockedEncryptionRoots addObject:fsName];
					}
				}
			}
		}
		if ([unlockedEncryptionRoots count] > 0)
		{
			[menu insertItem:lockItem atIndex:actionMenuIdx + 1];
			[_dynamicMenus addObject:lockItem];
		}
		if ([lockedEncryptionRoots count] > 0)
		{
			[menu insertItem:unlockItem atIndex:actionMenuIdx + 1];
			[_dynamicMenus addObject:unlockItem];
		}
	}
	catch (std::exception const & e)
	{
		NSString * error = [NSString stringWithFormat:@"Exception during pool iteration: %s", e.what()];
		NSMenuItem * errorItem = [[NSMenuItem alloc] initWithTitle:error action:nullptr keyEquivalent:@""];
		[menu insertItem:errorItem atIndex:actionMenuIdx + 1];
		[_dynamicMenus addObject:lockItem];
	}
}

- (void)resetLibZFS
{
	// Reset library to get fresh property state. This seems to be essential
	// for getting up-to-date altroot of pools that have been imported after
	// being known to the previous libzfs state. Calling zfs_refresh_properties
	// is insufficient for refreshing this state.
	// This also invalidates all filesystem and pool handles that still exist.
	_zfs.reset();
}

- (void)clearDynamicMenu:(NSMenu*)menu
{
	for (NSMenuItem * m in _dynamicMenus)
	{
		[menu removeItem:m];
	}
	[_dynamicMenus removeAllObjects];
}

- (void)handlePoolChangeReply:(NSError*)error
{
	if (error)
		[self notifyErrorFromHelper:error];
	else
		[[self poolWatcher] checkForChanges];
}

- (void)handleFileSystemChangeReply:(NSError*)error
{
	if (error)
		[self notifyErrorFromHelper:error];
}

- (void)handleMetaDataChangeReply:(NSError*)error
{
	if (error)
		[self notifyErrorFromHelper:error];
}

#pragma mark ZFS Maintenance

- (IBAction)exportPool:(id)sender
{
	NSDictionary * opts = @{@"pool": [sender representedObject]};
	[_authorization exportPools:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Export succeeded",
												  @"Export succeeded");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ exported",
								  @"Export Success format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handlePoolChangeReply:error];
	 }];
}

- (IBAction)exportPoolForce:(id)sender
{
	NSDictionary * opts = @{@"pool": [sender representedObject], @"force": @YES};
	[_authorization exportPools:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Forced export succeeded",
												  @"Forced export succeeded");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ force-exported",
								  @"Force-Export Successful format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handlePoolChangeReply:error];
	 }];
}

- (IBAction)mountFilesystem:(id)sender
{
	NSDictionary * opts = @{@"filesystem": [sender representedObject]};
	[_authorization mountFilesystems:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Mount succeeded",
												  @"Mount succeeded");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ mounted", @"Mount Successful format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handleFileSystemChangeReply:error];
	 }];
}

- (IBAction)mountFilesystemRecursive:(id)sender
{
	NSDictionary * opts = @{@"filesystem": [sender representedObject], @"recursive": @TRUE};
	[_authorization mountFilesystems:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Recursive mount succeeded",
												  @"Recursive mount succeeded");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ mounted recursively",
								  @"Recursive Mount Success format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handleFileSystemChangeReply:error];
	 }];
}

- (IBAction)unmountFilesystem:(id)sender
{
	NSDictionary * opts = @{@"filesystem": [sender representedObject]};
	[_authorization unmountFilesystems:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Unmount succeeded",
												  @"Unmount succeeded");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ unmounted",
								  @"FS Unmount Successful format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handleFileSystemChangeReply:error];
	 }];
}

- (IBAction)unmountFilesystemRecursive:(id)sender
{
	NSDictionary * opts = @{@"filesystem": [sender representedObject], @"recursive": @TRUE};
	[_authorization unmountFilesystems:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Recursive unmount succeeded",
												  @"Recursive unmount succeeded");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ unmounted recursively",
								  @"Recursive Unmount Success format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handleFileSystemChangeReply:error];
	 }];
}

- (IBAction)unmountFilesystemForce:(id)sender
{
	NSDictionary * opts = @{@"filesystem": [sender representedObject], @"force": @YES};
	[_authorization unmountFilesystems:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Filesystem unmount (forced) succeeded",
												  @"Filesystem unmount (forced) succeeded");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"%@ force-unmounted",
								  @"FS ForceUnmount Success format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handleFileSystemChangeReply:error];
	 }];
}

static NSString * defaultSnapshotName()
{
	auto now = [NSDate date];
	auto formater = [[NSDateFormatter alloc] init];
	formater.dateFormat = @"'ZetaSnap'-yyyy-MM-dd-HH-mm-ss";
	auto nowString = [formater stringFromDate:now];
	return nowString;
}

- (IBAction)snapshotFilesystem:(id)sender
{
	NSString * filesystem = [sender representedObject];
	[_zetaQueryDialog addQuery:NSLocalizedString(@"Enter snapshot name", @"Snapshot Query")
				   withDefault:defaultSnapshotName()
				  withCallback:^(NSString * snapshot)
	 {
		 NSDictionary * opts = @{@"filesystem": filesystem, @"snapshot": snapshot};
		 [self->_authorization snapshotFilesystem:opts withReply:^(NSError * error)
		  {
			  if (!error)
			  {
				  NSString * title = NSLocalizedString(@"Snapshot succeeded",
													   @"Snapshot succeeded");
				  NSString * text = [NSString stringWithFormat:
					NSLocalizedString(@"%@@%@ created",
									  @"Snapshot Success format"),
									  filesystem, snapshot];
				  [self notifySuccessWithTitle:title text:text];
			  }
			  [self handleFileSystemChangeReply:error];
		  }];
	 }];
}

- (IBAction)snapshotFilesystemRecursive:(id)sender
{
	NSString * filesystem = [sender representedObject];
	[_zetaQueryDialog addQuery:NSLocalizedString(@"Enter snapshot name", @"Snapshot Query")
				   withDefault:defaultSnapshotName()
				  withCallback:^(NSString * snapshot)
	 {
		 NSDictionary * opts = @{@"filesystem": filesystem, @"snapshot": snapshot, @"recursive": @YES};
		 [self->_authorization snapshotFilesystem:opts withReply:^(NSError * error)
		  {
			  if (!error)
			  {
				  NSString * title = NSLocalizedString(@"Recursive snapshot succeeded",
													   @"Recursive snapshot succeeded");
				  NSString * text = [NSString stringWithFormat:
					NSLocalizedString(@"%@@%@ created recursively",
									  @"RecursiveSnapshot Success format"),
					filesystem, snapshot];
				  [self notifySuccessWithTitle:title text:text];
			  }
			  [self handleFileSystemChangeReply:error];
		  }];
	 }];
}

static NSMutableString * appendAsString(NSMutableString * fileSystemString, std::vector<zfs::ZFileSystem> const & fileSystems)
{
	for (auto const & d : fileSystems)
		[fileSystemString appendFormat:@"%s\n", d.name()];
	return fileSystemString;
}

static NSMutableString * toString(std::vector<zfs::ZFileSystem> const & fileSystems)
{
	NSMutableString * fileSystemString = [NSMutableString string];
	return appendAsString(fileSystemString, fileSystems);
}

static NSString * formatRollbackDependents(
	std::vector<zfs::ZFileSystem> const & clones,
	std::vector<zfs::ZFileSystem> const & snap,
	std::vector<zfs::ZFileSystem> const & bookmarks)
{
	NSMutableString * depString = [NSMutableString string];
	if (!clones.empty())
	{
		[depString appendString:@"# Clones\n"];
		appendAsString(depString, clones);
	}
	if (!snap.empty())
	{
		[depString appendString:@"# Snapshots\n"];
		appendAsString(depString, snap);
	}
	if (!bookmarks.empty())
	{
		[depString appendString:@"# Bookmarks\n"];
		appendAsString(depString, bookmarks);
	}
	return depString;
}

- (IBAction)rollbackFilesystem:(NSString*)snapNameStr Force:(bool)force
{
	std::string snapName([snapNameStr UTF8String]);
	std::string baseName = snapName.substr(0, snapName.find_last_of('@'));
	zfs::LibZFSHandle lib;
	auto snap = lib.filesystem(snapName);
	auto fs = lib.filesystem(baseName);
	std::vector<zfs::ZFileSystem> clones;
	auto snapshots = fs.snapshotsSince(snap);
	for (auto && snap : snapshots)
	{
		if (snap.cloneCount() > 0)
		{
			auto dep = snap.dependents();
			std::move(dep.begin(), dep.end(),
					  std::back_inserter(clones));
		}
	}
	auto bookmarks = fs.bookmarksSince(snap);

	auto rollBackBlock = ^(bool ok)
	{
		if (ok)
		{
			NSDictionary * opts = @{
				@"snapshot": snapNameStr,
				@"force": [NSNumber numberWithBool:force]
			};
			[self->_authorization rollbackFilesystem:opts withReply:^(NSError * error)
			 {
				 if (!error)
				 {
					 NSString * title = NSLocalizedString(@"Rollback succeeded",
														  @"Rollback succeeded");
					 NSString * text = [NSString stringWithFormat:
						NSLocalizedString(@"%@ rolled back",
										  @"Rollback Success format"),
										 snapNameStr];
					 [self notifySuccessWithTitle:title text:text];
				 }
				 [self handleFileSystemChangeReply:error];
			 }];
		}
	};
	if (!snapshots.empty() || !clones.empty() || !bookmarks.empty())
	{
		[_zetaConfirmDialog addQuery:NSLocalizedString(@"The following will be destroyed by the rollback", @"Rollback Snapshot Query")
					 withInformation:formatRollbackDependents(clones, snapshots, bookmarks)
						withCallback:rollBackBlock];
	}
	else
	{
		rollBackBlock(true);
	}
}

- (IBAction)rollbackFilesystem:(id)sender
{
	[self rollbackFilesystem:[sender representedObject] Force:false];
}

- (IBAction)rollbackFilesystemForce:(id)sender
{
	[self rollbackFilesystem:[sender representedObject] Force:true];
}

- (IBAction)cloneSnapshot:(id)sender
{
	NSString * snapshot = [sender representedObject];
	NSString * newFileSystem = [snapshot stringByReplacingOccurrencesOfString:@"@" withString:@"-"];
	[_zetaQueryDialog addQuery:NSLocalizedString(@"Enter new filesystem name", @"Clone Query")
				   withDefault:newFileSystem
				  withCallback:^(NSString * newFileSystem)
	 {
		 NSDictionary * opts = @{@"snapshot": snapshot, @"newFilesystem": newFileSystem};
		 [self->_authorization cloneSnapshot:opts withReply:^(NSError * error)
		  {
			  if (!error)
			  {
				  NSString * title = NSLocalizedString(@"Clone succeeded",
													   @"Clone succeeded");
				  NSString * text = [NSString stringWithFormat:
					NSLocalizedString(@"%@ cloned to %@",
									  @"Clone Success format"),
					snapshot, newFileSystem];
				  [self notifySuccessWithTitle:title text:text];
			  }
			  [self handleFileSystemChangeReply:error];
		  }];
	 }];
}

- (IBAction)createFilesystem:(id)sender
{
	NSString * parentFilesyStem = [sender representedObject];
	NSMutableDictionary * query = [NSMutableDictionary dictionary];
	query[@"filesystem"] = [NSString stringWithFormat:@"%@/NewFilesystem", parentFilesyStem];
	[_zetaNewFSDialog addQuery:query
				  withCallback:^(NSDictionary * opts)
	 {
		 [self->_authorization createFilesystem:opts withReply:^(NSError * error)
		  {
			  if (!error)
			  {
				  NSString * title = NSLocalizedString(@"Filesystem creation succeeded",
													   @"Filesystem creation succeeded");
				  NSString * text = [NSString stringWithFormat:
					NSLocalizedString(@"Filesystem %@ created",
									  @"FS Create Success format"),
					opts[@"filesystem"]];
				  [self notifySuccessWithTitle:title text:text];
			  }
			  [self handleFileSystemChangeReply:error];
		  }];
	 }];
}

- (IBAction)createVolume:(id)sender
{
	NSString * parentFilesyStem = [sender representedObject];
	NSMutableDictionary * query = [NSMutableDictionary dictionary];
	query[@"filesystem"] = [NSString stringWithFormat:@"%@/NewVolume", parentFilesyStem];
	query[@"size"] = [NSNumber numberWithUnsignedLongLong:(1 << 30)];
	[_zetaNewVolDialog addQuery:query
				   withCallback:^(NSDictionary * opts)
	 {
		 [self->_authorization createVolume:opts withReply:^(NSError * error)
		  {
			  if (!error)
			  {
				  NSString * title = NSLocalizedString(@"Volume creation succeeded",
													   @"Volume creation succeeded");
				  NSString * text = [NSString stringWithFormat:
					NSLocalizedString(@"Volume %@ created",
									  @"Volume Create Success format"),
					opts[@"filesystem"]];
				  [self notifySuccessWithTitle:title text:text];
			  }
			  [self handleFileSystemChangeReply:error];
		  }];
	 }];
}

- (IBAction)destroy:(id)sender
{
	NSString * fsName = [sender representedObject];
	zfs::LibZFSHandle lib;
	auto fs = lib.filesystem([fsName UTF8String]);
	auto dependents = fs.dependents();
	if (!dependents.empty())
	{
  // cannot destroy 'tank/nuts': filesystem has children
		[_zetaConfirmDialog addQuery:NSLocalizedString(@"Unable to destroy, the filesystem has dependent datasets", @"Destroy Dep Failure")
					 withInformation:toString(dependents)
						withCallback:^(bool ok)
		 {
		 }];
	}
	else
	{
		NSDictionary * opts = @{@"filesystem": fsName};
		[_authorization destroy:opts withReply:^(NSError * error)
		 {
			 if (!error)
			 {
				 NSString * title = NSLocalizedString(@"Destroy succeeded",
													  @"Destroy Succeeded");
				 NSString * text = [NSString stringWithFormat:
					NSLocalizedString(@"%@ destroyed",
									  @"Destroy Success format"),
					[sender representedObject]];
				 [self notifySuccessWithTitle:title text:text];
			 }
			 [self handleFileSystemChangeReply:error];
		 }];
	}
}

- (IBAction)destroyRecursive:(id)sender
{
	NSString * fsName = [sender representedObject];
	zfs::LibZFSHandle lib;
	auto fs = lib.filesystem([fsName UTF8String]);
	auto dependents = fs.dependents();
	auto destroyBlock = ^(bool ok)
	{
		if (ok)
		{
			NSDictionary * opts = @{@"filesystem": [sender representedObject], @"recursive": @TRUE};
			[self->_authorization destroy:opts withReply:^(NSError * error)
			 {
				 if (!error)
				 {
					 NSString * title = NSLocalizedString(@"Recursive destroy succeeded",
														  @"Recursive destroy succeeded");
					 NSString * text = [NSString stringWithFormat:
						NSLocalizedString(@"%@ destroyed recursively",
										  @"Destroy Recursive Success format"),
						[sender representedObject]];
					 [self notifySuccessWithTitle:title text:text];
				 }
				 [self handleFileSystemChangeReply:error];
			 }];
		}
	};
	if (!dependents.empty())
	{
		[_zetaConfirmDialog addQuery:NSLocalizedString(@"The following dependent datasets will be destroyed", @"Destroy Dep Query")
					 withInformation:toString(dependents)
						withCallback:destroyBlock];
	}
	else
	{
		destroyBlock(true);
	}
}

- (IBAction)loadKey:(id)sender
{
	NSString * fs = [sender representedObject];
	[_zetaKeyLoader unlockFileSystem:fs];
}

- (IBAction)loadAllKeys:(id)sender
{
	NSArray<NSString*> * fss = [sender representedObject];
	for (NSString * fs in fss) {
		[_zetaKeyLoader unlockFileSystem:fs];
	}
}

- (IBAction)unloadKey:(id)sender
{
	NSDictionary * opts = @{@"filesystem": [sender representedObject]};
	[_authorization unloadKeyForFilesystem:opts withReply:^(NSError * error)
	 {
		 if (!error)
		 {
			 NSString * title = NSLocalizedString(@"Key unloaded successfully",
												  @"Key unloaded successfully");
			 NSString * text = [NSString stringWithFormat:
				NSLocalizedString(@"Key for %@ unloaded",
								  @"Key Unload Success format"),
				[sender representedObject]];
			 [self notifySuccessWithTitle:title text:text];
		 }
		 [self handleFileSystemChangeReply:error];
	 }];
}

- (void)unloadNextKey:(NSMutableArray<NSString*>*)fileSystems
{
	if ([fileSystems count] > 0)
	{
		NSString * fs = [fileSystems lastObject];
		[fileSystems removeLastObject];
		NSDictionary * opts = @{@"filesystem": fs};
		[_authorization unloadKeyForFilesystem:opts withReply:^(NSError * error)
		 {
			 if (!error)
			 {
				 NSString * title = NSLocalizedString(@"Key unloaded successfully",
													  @"Key unloaded successfully");
				 NSString * text = [NSString stringWithFormat:
					NSLocalizedString(@"Key for %@ unloaded",
									  @"Key Unload Success format"),
					fs];
				 [self notifySuccessWithTitle:title text:text];
			 }
			 [self handleFileSystemChangeReply:error];
			 [self unloadNextKey:fileSystems];
		 }];
	}
}

- (IBAction)unloadAllKeys:(id)sender
{
	NSMutableArray<NSString*> * fileSystems = [sender representedObject];
	[self unloadNextKey:fileSystems];
}

- (IBAction)scrubPool:(id)sender
{
	NSDictionary * opts = @{@"pool": [sender representedObject]};
	[_authorization scrubPool:opts withReply:^(NSError * error)
	 {
		 [self handleMetaDataChangeReply:error];
	 }];
}

- (IBAction)scrubStopPool:(id)sender
{
	NSDictionary * opts = @{@"pool": [sender representedObject], @"command": @"stop"};
	[_authorization scrubPool:opts withReply:^(NSError * error)
	 {
		 [self handleMetaDataChangeReply:error];
	 }];
}

- (IBAction)scrubPausePool:(id)sender
{
	NSDictionary * opts = @{@"pool": [sender representedObject], @"command": @"pause"};
	[_authorization scrubPool:opts withReply:^(NSError * error)
	 {
		 [self handleMetaDataChangeReply:error];
	 }];
}

@end
