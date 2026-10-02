//
//  ZetaAuthoized.m
//  ZetaWatch
//
//  Created by cbreak on 17.12.31.
//  Copyright © 2017 the-color-black.net. All rights reserved.
//

#import "ZetaAuthorization.h"

#import "ZetaAuthorizationHelperProtocol.h"
#import "CommonAuthorization.h"

#import "ZetaNotificationCenter.h"

#include <ServiceManagement/ServiceManagement.h>

#include <dispatch/dispatch.h>

@interface ZetaAuthorization ()
{
	AuthorizationRef _authRef;
}

@property (atomic, copy, readwrite) NSData * authorization;
@property (atomic, strong, readwrite) NSXPCConnection * helperToolConnection;

@end

@implementation ZetaAuthorization

- (void)awakeFromNib
{
	[self connectToAuthorization];
	[self installIfNeeded];
}

-(void)connectToAuthorization
{
	// Create our connection to the authorization system.
	//
	// If we can't create an authorization reference then the app is not going
	// to be able to do anything requiring authorization. Generally this only
	// happens when you launch the app in some wacky, and typically unsupported,
	// way. We continue with self->_authRef as NULL, which will cause all
	// authorized operations to fail.

	AuthorizationExternalForm extForm = {};
	OSStatus err = AuthorizationCreate(NULL, NULL, 0, &self->_authRef);
	if (err == errAuthorizationSuccess)
	{
		err = AuthorizationMakeExternalForm(self->_authRef, &extForm);
	}
	if (err == errAuthorizationSuccess)
	{
		self.authorization = [[NSData alloc] initWithBytes:&extForm length:sizeof(extForm)];
	}

	// If we successfully connected to Authorization Services, add definitions
	// for our default rights (unless they're already in the database).

	if (self->_authRef)
	{
		[CommonAuthorization setupAuthorizationRights:self->_authRef];
	}
}

-(void)install
{
	// Install the helper tool into the system location. As of 10.12 this is
	// /Library/LaunchDaemons/<TOOL> and /Library/PrivilegedHelperTools/<TOOL>
	Boolean success = NO;
	CFErrorRef error = nil;

	success = SMJobBless(kSMDomainSystemLaunchd,
						 CFSTR("org.openzfsonosx.ZetaAuthorizationHelper"),
						 self->_authRef, &error);

	if (success)
	{
	}
	else
	{
		NSLog(@"Error installing helper tool: %@\n", error);
		CFRelease(error);
	}
}

- (void)connectToHelperTool
{
	assert([NSThread isMainThread]);
	if (self.helperToolConnection == nil)
	{
		self.helperToolConnection = [[NSXPCConnection alloc] initWithMachServiceName:kHelperToolMachServiceName options:NSXPCConnectionPrivileged];
		self.helperToolConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ZetaAuthorizationHelperProtocol)];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-retain-cycles"
		// We can ignore the retain cycle warning because a) the retain taken by the
		// invalidation handler block is released by us setting it to nil when the block
		// actually runs, and b) the retain taken by the block passed to -addOperationWithBlock:
		// will be released when that operation completes and the operation itself is deallocated
		// (notably self does not have a reference to the NSBlockOperation).
		self.helperToolConnection.invalidationHandler = ^{
			// If the connection gets invalidated then, on the main thread, nil out our
			// reference to it.  This ensures that we attempt to rebuild it the next time around.
			self.helperToolConnection.invalidationHandler = nil;
			[[NSOperationQueue mainQueue] addOperationWithBlock:^{
				self.helperToolConnection = nil;
			}];
		};
#pragma clang diagnostic pop
		[self.helperToolConnection resume];
	}
}

- (void)stopHelper
{
	id proxy = [self.helperToolConnection synchronousRemoteObjectProxyWithErrorHandler:^(NSError * error)
	{
		// Ignore errors, invalidate connection anyway
		[self.helperToolConnection invalidate];
		self.helperToolConnection = nil;
	}];
	[proxy stopHelperWithAuthorization:self->_authorization
							 withReply:[=](NSError * error)
	{
		// Invalidate connection
		[self.helperToolConnection invalidate];
		self.helperToolConnection = nil;
	}];
}

- (void)installIfNeeded
{
	// Ensure that there's a helper tool connection in place.
	[self connectToHelperTool];
	id proxy = [self.helperToolConnection remoteObjectProxyWithErrorHandler:
				^(NSError * proxyError)
	{
		// Install on proxy error
		dispatch_async(dispatch_get_main_queue(), ^(){
			// Install if there's a proxy error
			[self install];
		});
	}];

	[proxy getVersionWithReply:^(NSError * error, NSString * helperVersion)
	 {
		 // Install if there's no valid helper version (because of error)
		 if (error)
		 {
			 dispatch_async(dispatch_get_main_queue(), ^(){
				 [self install];
			 });
			 return;
		 }
		 NSString * localVersion = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
		 // Install if the versions don't match exactly
		 if (![localVersion isEqualToString:helperVersion])
		 {
			 dispatch_async(dispatch_get_main_queue(), ^(){
				 [self install];
			 });
			 return;
		 }
	 }];
}

- (void)executeWhenConnected:(void(^)(id proxy))task
					 onError:(void(^)(NSError * error))handleError;
{
	// Ensure that there's a helper tool connection in place.
	[self connectToHelperTool];
	id proxy = [self.helperToolConnection remoteObjectProxyWithErrorHandler:handleError];
	task(proxy);
}

- (void)executeOnProxy:(SEL)selector
			  withData:(NSDictionary *)data
			 withReply:(void(^)(NSError * error))reply
	  withNotification:(ZetaNotification*)notification
{
	[self executeWhenConnected:^(id proxy)
	 {
		 auto block = ^(NSError * error)
		 {
			 [self dispatchReply:^(){
				 reply(error);
				 [self stopNotification:notification withError:error];
			 }];
		 };
		 auto sig = [proxy methodSignatureForSelector:selector];
		 auto inv = [NSInvocation invocationWithMethodSignature:sig];
		 [inv setTarget:proxy];
		 [inv setSelector:selector];
		 NSDictionary * dataLoc = data;
		 [inv setArgument:&dataLoc atIndex:2];
		 [inv setArgument:&self->_authorization atIndex:3];
		 [inv setArgument:&block atIndex:4];
		 [inv invoke];
	 }
					   onError:^(NSError * error)
	 {
		 [self dispatchReply:^(){
			 reply(error);
			 [self stopNotification:notification withError:error];
		 }];
	 }];
}

- (void)dispatchReply:(void(^)(void))reply
{
	[self performSelectorOnMainThread:@selector(dispatchReplyMainThread:) withObject:reply waitUntilDone:FALSE];
}

- (void)dispatchReplyMainThread:(void(^)(void))reply
{
	reply();
}

- (void)getVersionWithReply:(void (^)(NSError * error, NSString *))reply
{
	[self executeWhenConnected:^(id proxy)
	 {
		 [proxy getVersionWithReply:^(NSError * error, NSString * version)
		  {
			  [self dispatchReply:^(){ reply(error, version); }];
		  }];
	 }
					   onError:^(NSError * error)
	 {
		 [self dispatchReply:^(){ reply(error, nil); }];
	 }];
}

- (void)importPools:(NSDictionary *)importData
		  withReply:(void(^)(NSError * error))reply
{
	NSString * targetGUID = importData[@"poolGUID"];
	NSString * targetName = importData[@"poolName"];
	NSString * target;
	if (targetGUID == nil)
		target = @"all pools";
	else
		target = [NSString stringWithFormat:@"%@ (%@)", targetName, targetGUID];
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Importing", @"Importing Action") withTarget:target];
	[self executeOnProxy:@selector(importPools:authorization:withReply:)
				withData:importData withReply:reply
		withNotification:notification];
}

- (void)importablePools:(NSDictionary *)importData
			  withReply:(void(^)(NSError * error, NSArray * importablePools))reply
{
	[self executeWhenConnected:^(id proxy)
	 {
		 [proxy importablePools:importData
				  authorization:self.authorization
					  withReply:^(NSError * error, NSArray * importablePools)
		  {
			  [self dispatchReply:^(){ reply(error, importablePools); }];
		  }];
	 }
					   onError:^(NSError * error)
	 {
		 [self dispatchReply:^(){ reply(error, nil); }];
	 }];
}

- (void)exportPools:(NSDictionary *)exportData
		  withReply:(void(^)(NSError * error))reply
{
	NSString * target = exportData[@"pool"];
	if (target == nil)
		target = NSLocalizedString(@"all pools", @"All Pools");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Exporting", @"Exporting Action") withTarget:target];
	[self executeOnProxy:@selector(exportPools:authorization:withReply:)
				withData:exportData withReply:reply
		withNotification:notification];
}

- (void)mountFilesystems:(NSDictionary *)mountData
			   withReply:(void(^)(NSError * error))reply
{
	NSString * target = mountData[@"filesystem"];
	if (target == nil)
		target = NSLocalizedString(@"all filesystems", @"All Filesystems");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Mounting", @"Mounting Action") withTarget:target];
	[self executeOnProxy:@selector(mountFilesystems:authorization:withReply:)
				withData:mountData withReply:reply
		withNotification:notification];
}

- (void)unmountFilesystems:(NSDictionary *)mountData
			   withReply:(void(^)(NSError * error))reply
{
	NSString * target = mountData[@"filesystem"];
	if (target == nil)
		target = NSLocalizedString(@"all filesystems", @"All Filesystems");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Unmounting", @"Unmounting Action") withTarget:target];
	[self executeOnProxy:@selector(unmountFilesystems:authorization:withReply:)
				withData:mountData withReply:reply
		withNotification:notification];
}

- (void)snapshotFilesystem:(NSDictionary *)snapshotData
				 withReply:(void(^)(NSError * error))reply
{
	NSString * filesystem = snapshotData[@"filesystem"];
	if (filesystem == nil)
		std::logic_error("Missing required parameter \"filesystem\"");
	NSString * snapshot = snapshotData[@"snapshot"];
	if (snapshot == nil)
		std::logic_error("Missing required parameter \"snapshot\"");
	NSString * target = [NSString stringWithFormat:@"%@@%@", filesystem, snapshot];
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Snapshotting", @"Snapshot Action") withTarget:target];
	[self executeOnProxy:@selector(snapshotFilesystem:authorization:withReply:)
				withData:snapshotData withReply:reply
		withNotification:notification];
}

- (void)rollbackFilesystem:(NSDictionary *)rollbackData
				 withReply:(void(^)(NSError * error))reply
{
	NSString * target = rollbackData[@"snapshot"];
	if (target == nil)
		std::logic_error("Missing required parameter \"snapshot\"");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Rolling back", @"Rollback Action") withTarget:target];
	[self executeOnProxy:@selector(rollbackFilesystem:authorization:withReply:)
				withData:rollbackData withReply:reply
		withNotification:notification];
}

- (void)cloneSnapshot:(NSDictionary *)fsData
			withReply:(void(^)(NSError * error))reply
{
	NSString * target = fsData[@"snapshot"];
	if (target == nil)
		std::logic_error("Missing required parameter \"snapshot\"");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Cloning", @"Cloning Action") withTarget:target];
	[self executeOnProxy:@selector(cloneSnapshot:authorization:withReply:)
				withData:fsData withReply:reply
		withNotification:notification];
}

- (void)createFilesystem:(NSDictionary *)fsData
			   withReply:(void(^)(NSError * error))reply
{
	NSString * target = fsData[@"filesystem"];
	if (target == nil)
		std::logic_error("Missing required parameter \"filesystem\"");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Creating", @"Create Action") withTarget:target];
	[self executeOnProxy:@selector(createFilesystem:authorization:withReply:)
				withData:fsData withReply:reply
		withNotification:notification];
}

- (void)createVolume:(NSDictionary *)fsData
		   withReply:(void(^)(NSError * error))reply
{
	NSString * target = fsData[@"filesystem"];
	if (target == nil)
		std::logic_error("Missing required parameter \"filesystem\"");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Creating", @"Create Action") withTarget:target];
	[self executeOnProxy:@selector(createVolume:authorization:withReply:)
				withData:fsData withReply:reply
		withNotification:notification];
}

- (void)destroy:(NSDictionary *)fsData
	  withReply:(void(^)(NSError * error))reply
{
	NSString * target = fsData[@"filesystem"];
	if (target == nil)
		std::logic_error("Missing required parameter \"filesystem\"");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Destroying", @"Destroy Action") withTarget:target];
	[self executeOnProxy:@selector(destroy:authorization:withReply:)
				withData:fsData withReply:reply
		withNotification:notification];
}

- (void)loadKeyForFilesystem:(NSDictionary *)data
					withReply:(void(^)(NSError * error))reply
{
	NSString * target = data[@"filesystem"];
	if (target == nil)
		std::logic_error("Missing required parameter \"filesystem\"");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Loading Key for", @"LoadKey Action") withTarget:target];
	[self executeOnProxy:@selector(loadKeyForFilesystem:authorization:withReply:)
				withData:data withReply:reply
		withNotification:notification];
}

- (void)unloadKeyForFilesystem:(NSDictionary *)data
					 withReply:(void(^)(NSError * error))reply
{
	NSString * target = data[@"filesystem"];
	if (target == nil)
		std::logic_error("Missing required parameter \"filesystem\"");
	ZetaNotification * notification = [self startNotificationForAction:
		NSLocalizedString(@"Unloading Key for", @"UnloadKey Action") withTarget:target];
	[self executeOnProxy:@selector(unloadKeyForFilesystem:authorization:withReply:)
				withData:data withReply:reply
		withNotification:notification];
}

- (void)scrubPool:(NSDictionary *)poolData
		withReply:(void(^)(NSError * error))reply
{
	[self executeOnProxy:@selector(scrubPool:authorization:withReply:)
				withData:poolData withReply:reply
		withNotification:nullptr];
}

- (ZetaNotification*)startNotificationForAction:(NSString*)action withTarget:(NSString*)target
{
	NSString * titleFormat = NSLocalizedString(@"%@ %@", @"Helper Status notification Title Format");
	NSString * title = [NSString stringWithFormat:titleFormat, action, target];
	return [self.notificationCenter startAction:title];
}

- (void)stopNotification:(ZetaNotification*)notification withError:(NSError*)error
{
	if (notification)
	{
		[self.notificationCenter stopAction:notification withError:error];
	}
}

@end
