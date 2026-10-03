//
//  ZetaKeyLoader.m
//  ZetaWatch
//
//  Created by cbreak on 19.06.16.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import "ZetaKeyLoader.h"

#import <Security/Security.h>

#include "ZFSUtils.hpp"

#include <deque>
#include <type_traits>

enum class LoaderState
{
	idle,
	examineFilesystem,
	loadKeyfile,
	loadStoredKey,
	loadInteractiveGet,
	loadInteractiveUnlock,
	loadCompleted,
};

enum class LoaderEvent
{
	newFilesystem,
	unlockSucceeded,
	unlockFailedPassword,
	unlockFailedOther,
	buttonLoad,
	buttonSkip,
};

@interface ZetaKeyLoader ()
{
	std::deque<NSString*> filesystems;
	LoaderState state;
	zfs::LibZFSHandle libZFS;
}

@end

@implementation ZetaKeyLoader

- (void)awakeFromNib
{
	state = LoaderState::idle;
	if (self.poolWatcher)
	{
		[self.poolWatcher.delegates addObject:self];
	}
}

- (IBAction)loadKey:(id)sender
{
	[self handleLoaderEvent:LoaderEvent::buttonLoad];
}

- (IBAction)skipFileSystem:(id)sender
{
	[self handleLoaderEvent:LoaderEvent::buttonSkip];
}

- (void)unlockFileSystem:(NSString*)filesystem
{
	filesystems.push_back(filesystem);
	[self handleLoaderEvent:LoaderEvent::newFilesystem];
}

- (void)advanceFilesystems
{
	filesystems.pop_front();
	if (filesystems.size() > 0)
		[self transitionToState:LoaderState::examineFilesystem];
	else
		[self transitionToState:LoaderState::idle];
}

// State transition function that enters a state, setting up UI, and starting
// functions that might trigger further state transitions. Can include direct
// transitions to other states.
- (void)transitionToState:(LoaderState)nextState
{
	state = nextState;
	switch (state)
	{
		case LoaderState::idle:
		{
			[self updateFileSystem];
			[_popover performClose:self];
			break;
		}
		case LoaderState::examineFilesystem:
		{
			[self updateFileSystem];
			[self show];
			[self examineFilesystem:filesystems.front()];
			break;
		}
		case LoaderState::loadKeyfile:
		{
			[self showActionInProgress:
			 NSLocalizedString(@"Loading Keyfile...",
							   @"LoadingKeyfileStatus")];
			[self loadKeyFileForFilesystem:filesystems.front()];
			break;
		}
		case LoaderState::loadStoredKey:
		{
			[self showActionInProgress:
			 NSLocalizedString(@"Loading stored Key...",
							   @"LoadingNonInteractiveKeyStatus")];
			[self loadStoredPasswordForFilesystem:filesystems.front()];
			break;
		}
		case LoaderState::loadInteractiveGet:
		{
			[self requestPassword];
			break;
		}
		case LoaderState::loadInteractiveUnlock:
		{
			[self showActionInProgress:
			 NSLocalizedString(@"Loading entered Key...",
							   @"LoadingInteractiveKeyStatus")];
			[self loadInteractivePasswordForFilesystem:filesystems.front()];
			break;
		}
		case LoaderState::loadCompleted:
		{
			[self advanceFilesystems];
			break;
		}
	}
}

// Event state transition function that decides which state to go to based on
// events it receives and the current state.
- (void)handleLoaderEvent:(LoaderEvent)event
{
	switch (state)
	{
		case LoaderState::idle:
		{
			switch (event)
			{
				case LoaderEvent::newFilesystem:
					[self transitionToState:LoaderState::examineFilesystem];
					return;
				default:
					return;
			}
			break;
		}
		case LoaderState::examineFilesystem:
		{
			// Transitional state
			break;
		}
		case LoaderState::loadKeyfile:
		{
			switch (event)
			{
				case LoaderEvent::unlockSucceeded:
					[self transitionToState:LoaderState::loadCompleted];
					return;
				case LoaderEvent::unlockFailedPassword:
				case LoaderEvent::unlockFailedOther:
					[self transitionToState:LoaderState::loadStoredKey];
					return;
				default:
					return;
			}
			break;
		}
		case LoaderState::loadStoredKey:
		{
			switch (event)
			{
				case LoaderEvent::unlockSucceeded:
					[self transitionToState:LoaderState::loadCompleted];
					return;
				case LoaderEvent::unlockFailedPassword:
				case LoaderEvent::unlockFailedOther:
					[self transitionToState:LoaderState::loadInteractiveGet];
					return;
				default:
					return;
			}
			break;
		}
		case LoaderState::loadInteractiveGet:
		{
			switch (event)
			{
				case LoaderEvent::buttonLoad:
					[self transitionToState:LoaderState::loadInteractiveUnlock];
					return;
				case LoaderEvent::buttonSkip:
					[self transitionToState:LoaderState::loadCompleted];
					return;
				default:
					return;
			}
			break;
		}
		case LoaderState::loadInteractiveUnlock:
		{
			switch (event)
			{
				case LoaderEvent::unlockSucceeded:
				case LoaderEvent::unlockFailedOther:
					[self transitionToState:LoaderState::loadCompleted];
					return;
				case LoaderEvent::unlockFailedPassword:
					[self transitionToState:LoaderState::loadInteractiveGet];
					return;
				default:
					return;
			}
			break;
		}
		case LoaderState::loadCompleted:
		{
			// Transitional state
			break;
		}
	}
}

- (void)show
{
//	[[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
	NSView * positioningView = [_statusItem button];
	[_popover showRelativeToRect:NSMakeRect(0, 0, 0, 0)
						  ofView:positioningView
				   preferredEdge:NSRectEdgeMinY];
}

- (void)loadKey:(NSString*)password forFilesystem:(NSString*)filesystem
		  storeInKeychain:(bool)storeInKeychain
{
	NSDictionary * opts = @{@"filesystem": filesystem, @"key": password};
	[_authorization loadKeyForFilesystem:opts withReply:^(NSError * error)
	 {
		bool success = [self handleLoadKeyReply:error];
		 if (success && storeInKeychain)
		 {
			 [self storePassword:password forFilesystem:filesystem];
		 }
	 }];
}

- (void)loadKeyFileForFilesystem:(NSString*)filesystem
{
	NSDictionary * opts = @{@"filesystem": filesystem};
	[_authorization loadKeyForFilesystem:opts withReply:^(NSError * error)
	 {
		[self handleLoadKeyReply:error];
	 }];
}

- (bool)handleLoadKeyReply:(NSError*)error
{
	if (error)
	{
		if ([error.domain isEqualToString:@"ZFSKeyError"])
		{
			[self showError:[error localizedDescription]];
			[self handleLoaderEvent:LoaderEvent::unlockFailedPassword];
		}
		else
		{
			[self notifyErrorFromHelper:error];
			[self handleLoaderEvent:LoaderEvent::unlockFailedOther];
		}
		return false;
	}
	else
	{
		[self handleLoaderEvent:LoaderEvent::unlockSucceeded];
		return true;
	}
}

- (void)showActionInProgress:(NSString*)action
{
	[self showStatus:action];
	[_progressIndicator startAnimation:self];
	[_loadButton setEnabled:NO];
	[_skipButton setEnabled:NO];
	[_passwordField setEnabled:NO];
	[_useKeychainCheckbox setEnabled:NO];
}

- (void)requestPassword
{
	[_progressIndicator stopAnimation:self];
	[_loadButton setEnabled:YES];
	[_skipButton setEnabled:YES];
	[_passwordField setEnabled:YES];
	[_useKeychainCheckbox setEnabled:YES];
}

- (void)showStatus:(NSString*)error
{
	[_statusField setStringValue:error];
	[_statusField setTextColor:[NSColor textColor]];
}

- (void)showError:(NSString*)error
{
	[_statusField setStringValue:error];
	[_statusField setTextColor:[NSColor systemRedColor]];
}

- (void)clearPassword
{
	// The password is copied all over the place by the view, the dictionary
	// and the IPC, so trying to clear it is probably a waste of time.
	[_passwordField setStringValue:@""];
}

- (void)examineFilesystem:(NSString*)filesystem
{
	auto fs = libZFS.filesystem([filesystem UTF8String]);
	if (fs.keyLocation() == zfs::ZFileSystem::KeyLocation::uri)
	{
		[self transitionToState:LoaderState::loadKeyfile];
	}
	else
	{
		[self transitionToState:LoaderState::loadStoredKey];
	}
}

- (void)loadInteractivePasswordForFilesystem:(NSString*)filesystem
{
	NSString * pass = [_passwordField stringValue];
	bool storeInKeychain = [_useKeychainCheckbox state] == NSControlStateValueOn;
	[self clearPassword];
	[self loadKey:pass forFilesystem:filesystem storeInKeychain:storeInKeychain];
}

- (void)loadStoredPasswordForFilesystem:(NSString*)filesystem
{
	NSString * password = [self retrievePasswordForFilesystem:filesystem];
	if (password)
	{
		[self loadKey:password forFilesystem:filesystem storeInKeychain:false];
	}
	else
	{
		[self showStatus:
		 NSLocalizedString(@"No stored key found",
						   @"NoStoredKeyStatus")];
		[self handleLoaderEvent:LoaderEvent::unlockFailedOther];
	}
}

- (NSString*)retrievePasswordForFilesystem:(NSString*)filesystem
{
	void const * keys[] = {
		kSecClass,
		kSecAttrService,
		kSecMatchLimit,
		kSecReturnData,
	};
	void const * values[] = {
		kSecClassGenericPassword,
		(__bridge CFStringRef)filesystem,
		kSecMatchLimitOne,
		kCFBooleanTrue,
	};
	static_assert(std::extent_v<decltype(keys)> == std::extent_v<decltype(values)>);
	constexpr size_t attributeCount = std::extent_v<decltype(keys)>;
	CFDictionaryRef attributes =  CFDictionaryCreate(nullptr,
		&keys[0], &values[0], attributeCount,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	void const * data = nullptr;
	OSStatus result = SecItemCopyMatching(attributes, &data);
	CFRelease(attributes);
	if (result != errSecSuccess)
	{
		return nullptr;
	}
	NSString * password = [[NSString alloc] initWithData:CFBridgingRelease((CFDataRef)data)
encoding:NSUTF8StringEncoding];
	return password;
}

- (bool)deletePasswordForFilesystem:(NSString*)filesystem
{
	void const * keys[] = {
		kSecClass,
		kSecAttrService,
		kSecMatchLimit,
	};
	void const * values[] = {
		kSecClassGenericPassword,
		(__bridge CFStringRef)filesystem,
		kSecMatchLimitAll,
	};
	static_assert(std::extent_v<decltype(keys)> == std::extent_v<decltype(values)>);
	constexpr size_t attributeCount = std::extent_v<decltype(keys)>;
	CFDictionaryRef attributes =  CFDictionaryCreate(nullptr,
		&keys[0], &values[0], attributeCount,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	OSStatus result = SecItemDelete(attributes);
	CFRelease(attributes);
	if (result != errSecSuccess)
	{
		return false;
	}
	return true;
}

- (bool)storePassword:(NSString*)password forFilesystem:(NSString*)filesystem
{
	[self deletePasswordForFilesystem:filesystem];
	NSData * passwordData = [password dataUsingEncoding:NSUTF8StringEncoding];
	NSString * label = [NSString stringWithFormat:@"ZetaWatch Password for ZFS filesystem %@", filesystem];
	void const * keys[] = {
		kSecClass,
		kSecAttrLabel,
		kSecAttrService,
		kSecValueData,
	};
	void const * values[] = {
		kSecClassGenericPassword,
		(__bridge CFStringRef)label,
		(__bridge CFStringRef)filesystem,
		(__bridge CFDataRef)passwordData,
	};
	static_assert(std::extent_v<decltype(keys)> == std::extent_v<decltype(values)>);
	constexpr size_t attributeCount = std::extent_v<decltype(keys)>;
	CFDictionaryRef attributes =  CFDictionaryCreate(nullptr,
		&keys[0], &values[0], attributeCount,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	OSStatus result = SecItemAdd(attributes, nil);
	CFRelease(attributes);
	if (result != errSecSuccess)
	{
		CFStringRef errorString = SecCopyErrorMessageString(result, nullptr);
		NSError * error = [NSError errorWithDomain:NSOSStatusErrorDomain
											  code:result userInfo:
		@{
			NSLocalizedDescriptionKey: CFBridgingRelease(errorString)
		}];
		[self notifyErrorFromHelper:error];
		return false;
	}
	return true;
}

- (void)updateFileSystem
{
	if (filesystems.empty())
	{
		[_queryField setStringValue:@""];
	}
	else
	{
		[_queryField setStringValue:[NSString stringWithFormat:NSLocalizedString(@"Enter the password for %@", @"Password Query"), filesystems.front()]];
		bool useKeychain = [[NSUserDefaults standardUserDefaults] boolForKey:@"useKeychain"];
		[_useKeychainCheckbox setState:useKeychain ? NSControlStateValueOn : NSControlStateValueOff];
	}
}

- (BOOL)popoverShouldDetach:(NSPopover *)popover
{
	return YES;
}

- (BOOL)popoverShouldClose:(NSPopover *)popover
{
	return filesystems.empty();
}

- (void)popoverWillShow:(NSNotification *)notification
{
}

- (void)popoverDidClose:(NSNotification *)notification
{
	[self clearPassword];
	[_passwordField abortEditing];
}

- (void)newPoolDetected:(const zfs::ZPool &)pool
{
	if ([[NSUserDefaults standardUserDefaults] boolForKey:@"autoUnlock"])
	{
		for (auto & fs : pool.allFileSystems())
		{
			auto [encRoot, isRoot] = fs.encryptionRoot();
			auto keyStatus = fs.keyStatus();
			if (isRoot && keyStatus == zfs::ZFileSystem::KeyStatus::unavailable)
			{
				NSString * fsName = [NSString stringWithUTF8String:fs.name()];
				[self unlockFileSystem:fsName];
			}
		}
	}
}

@end
