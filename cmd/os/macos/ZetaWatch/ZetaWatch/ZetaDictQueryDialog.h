//
//  ZetaDictQueryDialog.h
//  ZetaWatch
//
//  Created by cbreak on 19.10.06.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

/* CSTYLED */
#import <Cocoa/Cocoa.h>

/* CSTYLED */
@interface ZetaDictQueryDialog : NSObject <NSPopoverDelegate>

@property(weak) NSStatusItem * statusItem;
@property(weak) IBOutlet NSPopover * popover;
@property(retain) NSMutableDictionary * queryDict;

- (IBAction)ok:(id)sender;
- (IBAction)cancel:(id)sender;

- (BOOL)popoverShouldDetach:(NSPopover *)popover;

- (void)addQuery:(NSMutableDictionary*)query
	withCallback:(void(^)(NSDictionary*))callback;

- (id)initWithDialog:(NSString*)dialogName;

@end
