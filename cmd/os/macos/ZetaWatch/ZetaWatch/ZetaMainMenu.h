//
//  ZetaMainMenu.h
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.20.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the conditions of the "3-Clause
//  BSD" license described in the BSD.LICENSE file are met. Additional licensing
//  options are described in the README file.
//

/* CSTYLED */
#import <Cocoa/Cocoa.h>

#import "ZetaPoolWatcher.h"
#import "ZetaCommanderBase.h"
#import "ZetaPoolWatcher.h"
#import "ZetaAutoImporter.h"
#import "ZetaKeyLoader.h"
#import "ZetaQueryDialog.h"
#import "ZetaDictQueryDialog.h"
#import "ZetaConfirmDialog.h"

@class ZetaNotificationCenter;

enum ZetaMenuTags
{
	ZPoolAnchorMenuTag = 100,
	ActionAnchorMenuTag = 101
};

/* CSTYLED */
@interface ZetaMainMenu : ZetaCommanderBase <NSMenuDelegate>

@property(weak) IBOutlet ZetaPoolWatcher * poolWatcher;
@property(weak) IBOutlet ZetaKeyLoader * zetaKeyLoader;
@property(weak) IBOutlet ZetaQueryDialog * zetaQueryDialog;
@property(weak) IBOutlet ZetaDictQueryDialog * zetaNewFSDialog;
@property(weak) IBOutlet ZetaDictQueryDialog * zetaNewVolDialog;
@property(weak) IBOutlet ZetaConfirmDialog * zetaConfirmDialog;
@property(weak) IBOutlet ZetaNotificationCenter * notificationCenter;

- (IBAction)exportPool:(id)sender;
- (IBAction)exportPoolForce:(id)sender;
- (IBAction)mountFilesystem:(id)sender;
- (IBAction)mountFilesystemRecursive:(id)sender;
- (IBAction)unmountFilesystem:(id)sender;
- (IBAction)unmountFilesystemRecursive:(id)sender;
- (IBAction)unmountFilesystemForce:(id)sender;
- (IBAction)snapshotFilesystem:(id)sender;
- (IBAction)snapshotFilesystemRecursive:(id)sender;
- (IBAction)rollbackFilesystem:(id)sender;
- (IBAction)rollbackFilesystemForce:(id)sender;
- (IBAction)cloneSnapshot:(id)sender;
- (IBAction)destroy:(id)sender;
- (IBAction)destroyRecursive:(id)sender;
- (IBAction)loadKey:(id)sender;
- (IBAction)loadAllKeys:(id)sender;
- (IBAction)unloadKey:(id)sender;
- (IBAction)unloadAllKeys:(id)sender;
- (IBAction)scrubPool:(id)sender;
- (IBAction)scrubStopPool:(id)sender;

@end
