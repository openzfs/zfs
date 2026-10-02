//
//  ZetaQueryDialog.h
//  ZetaWatch
//
//  Created by cbreak on 19.10.06.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

/* CSTYLED */
#import <Cocoa/Cocoa.h>

/* CSTYLED */
@interface ZetaQueryDialog : NSObject <NSPopoverDelegate>

@property(weak) NSStatusItem * statusItem;
@property(weak) IBOutlet NSPopover * popover;
@property(weak) IBOutlet NSTextField * replyField;
@property(weak) IBOutlet NSTextField * queryField;

- (IBAction)ok:(id)sender;
- (IBAction)cancel:(id)sender;

- (BOOL)popoverShouldDetach:(NSPopover *)popover;

- (void)addQuery:(NSString*)query
    withDefault:(NSString*)defaultReply
    withCallback:(void(^)(NSString*))callback;

@end
