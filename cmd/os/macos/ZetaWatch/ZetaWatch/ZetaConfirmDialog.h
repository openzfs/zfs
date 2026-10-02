//
//  ZetaConfirmDialog.h
//  ZetaWatch
//
//  Created by cbreak on 19.10.13.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

/* CSTYLED */
#import <Cocoa/Cocoa.h>

/* CSTYLED */
@interface ZetaConfirmDialog : NSObject <NSPopoverDelegate>

@property(weak) NSStatusItem * statusItem;
@property(weak) IBOutlet NSPopover * popover;
@property(weak) IBOutlet NSTextField * queryField;
@property(weak) IBOutlet NSTextField * infoField;

- (IBAction)ok:(id)sender;
- (IBAction)cancel:(id)sender;

- (BOOL)popoverShouldDetach:(NSPopover *)popover;

- (void)addQuery:(NSString*)query
    withInformation:(NSString*)info
    withCallback:(void(^)(bool))callback;

@end
