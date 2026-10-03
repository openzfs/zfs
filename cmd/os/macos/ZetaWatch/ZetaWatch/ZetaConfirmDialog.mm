//
//  ZetaConfirmDialog.mm
//  ZetaWatch
//
//  Created by cbreak on 19.10.13.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaConfirmDialog.h"

#include <deque>
#include <type_traits>

namespace
{
	typedef void (^QueryCallback)(bool);

	struct Query
	{
		NSString * query;
		NSString * info;
		QueryCallback reply;
	};
}

@interface ZetaConfirmDialog ()
{
	std::deque<Query> queries;
}

@end

@implementation ZetaConfirmDialog

- (void)addQuery:(NSString*)query
 withInformation:(NSString*)info
	withCallback:(void(^)(bool))callback
{
	queries.push_back({query, info, callback});
	if (queries.size() == 1)
		[self updateQuery];
	if (![_popover isShown])
		[self show];
}

- (void)show
{
	[[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
	NSView * positioningView = [_statusItem button];
	[_popover showRelativeToRect:NSMakeRect(0, 0, 0, 0)
						  ofView:positioningView
				   preferredEdge:NSRectEdgeMinY];
}

- (IBAction)ok:(id)sender
{
	if (!queries.empty())
		queries.front().reply(true);
	[self advanceQuery];
}

- (IBAction)cancel:(id)sender
{
	if (!queries.empty())
		queries.front().reply(false);
	[self advanceQuery];
}

- (BOOL)popoverShouldDetach:(NSPopover *)popover
{
	return YES;
}

- (void)advanceQuery
{
	queries.pop_front();
	[self updateQuery];
	if (queries.empty())
	{
		[_popover performClose:self];
	}
}

- (void)updateQuery
{
	if (queries.empty())
	{
		[_queryField setStringValue:@""];
		[_infoField setStringValue:@""];
	}
	else
	{
		[_queryField setStringValue:queries.front().query];
		[_infoField setStringValue:queries.front().info];
	}
}

- (BOOL)popoverShouldClose:(NSPopover *)popover
{
	if (!queries.empty())
	{
		queries.front().reply(false);
		queries.pop_front();
		if (queries.empty())
			return YES;
		[self updateQuery];
		return NO;
	}
	else
	{
		return YES;
	}
}

- (void)popoverWillShow:(NSNotification *)notification
{
}

- (void)popoverDidClose:(NSNotification *)notification
{
}

@end
