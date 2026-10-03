//
//  ZetaNewFSDialog.mm
//  ZetaWatch
//
//  Created by cbreak on 19.10.06.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaDictQueryDialog.h"

#include <deque>
#include <type_traits>

namespace
{
	typedef void (^QueryCallback)(NSDictionary *);

	struct Query
	{
		NSMutableDictionary * query;
		QueryCallback reply;
	};
}

@interface ZetaDictQueryDialog ()
{
	std::deque<Query> queries;
	NSArray * topLevelObjects;
}

@end

@implementation ZetaDictQueryDialog

- (id)initWithDialog:(NSString*)dialogName
{
	if (self = [super init])
	{
		NSArray * tlo;
		[[NSBundle mainBundle] loadNibNamed:dialogName owner:self topLevelObjects:&tlo];
		topLevelObjects = tlo;
	}
	return self;
}

- (void)addQuery:(NSMutableDictionary*)query
	withCallback:(void(^)(NSDictionary*))callback
{
	queries.push_back({query, callback});
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
		queries.front().reply(queries.front().query);
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
	if (!queries.empty())
	{
		self.queryDict = queries.front().query;
	}
	else
	{
		self.queryDict = nullptr;
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
	self.queryDict = nullptr;
}

@end
