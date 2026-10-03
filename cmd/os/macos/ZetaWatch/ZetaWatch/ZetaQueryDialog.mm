//
//  ZetaQueryDialog.mm
//  ZetaWatch
//
//  Created by cbreak on 19.10.06.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaQueryDialog.h"

#include <deque>
#include <type_traits>

namespace
{
	typedef void (^QueryCallback)(NSString *);

	struct Query
	{
		NSString * query;
		NSString * defaultReply;
		QueryCallback reply;
	};
}

@interface ZetaQueryDialog ()
{
	std::deque<Query> queries;
}

@end

@implementation ZetaQueryDialog

- (void)addQuery:(NSString*)query
	 withDefault:(NSString*)defaultReply
	withCallback:(void(^)(NSString*))callback
{
	queries.push_back({query, defaultReply, callback});
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
		queries.front().reply([_replyField stringValue]);
	[self advanceQuery];
}

- (IBAction)cancel:(id)sender
{
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
		[_replyField setStringValue:@""];
	}
	else
	{
		[_queryField setStringValue:queries.front().query];
		[_replyField setStringValue:queries.front().defaultReply];
	}
}

- (BOOL)popoverShouldClose:(NSPopover *)popover
{
	if (!queries.empty())
	{
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
	[_replyField abortEditing];
}

@end
