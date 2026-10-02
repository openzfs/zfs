//
//  ZetaAuthoized.h
//  ZetaWatch
//
//  Created by cbreak on 17.12.31.
//  Copyright © 2017 the-color-black.net. All rights reserved.
//

/* CSTYLED */
#import <Foundation/Foundation.h>

@class ZetaNotificationCenter;

/*
 * This class wraps privileged operations that require authorization.
 *
 * This is heavily based on apple's EvenBetterAuthorizationSample
 * https://developer.apple.com/library/content/samplecode/Even
 *  BetterAuthorizationSample/Introduction/Intro.html
 */
@interface ZetaAuthorization : NSObject

// Call this after the program finished starting
- (void)connectToAuthorization;

- (void)stopHelper;

- (void)importPools:(NSDictionary *)importData
    withReply:(void(^)(NSError * error))reply;

- (void)importablePools:(NSDictionary *)importData
    withReply:(void(^)(NSError * error, NSArray * importablePools))reply;

- (void)exportPools:(NSDictionary *)exportData
    withReply:(void(^)(NSError * error))reply;

- (void)mountFilesystems:(NSDictionary *)mountData
    withReply:(void(^)(NSError * error))reply;

- (void)unmountFilesystems:(NSDictionary *)mountData
    withReply:(void(^)(NSError * error))reply;

- (void)snapshotFilesystem:(NSDictionary *)snapshotData
    withReply:(void(^)(NSError * error))reply;

- (void)rollbackFilesystem:(NSDictionary *)rollbackData
    withReply:(void(^)(NSError * error))reply;

- (void)cloneSnapshot:(NSDictionary *)fsData
    withReply:(void(^)(NSError * error))reply;

- (void)createFilesystem:(NSDictionary *)fsData
    withReply:(void(^)(NSError * error))reply;

- (void)createVolume:(NSDictionary *)fsData
    withReply:(void(^)(NSError * error))reply;

- (void)destroy:(NSDictionary *)fsData
    withReply:(void(^)(NSError * error))reply;

- (void)loadKeyForFilesystem:(NSDictionary *)loadData
    withReply:(void(^)(NSError * error))reply;

- (void)unloadKeyForFilesystem:(NSDictionary *)unloadData
    withReply:(void(^)(NSError * error))reply;

- (void)scrubPool:(NSDictionary *)poolData
    withReply:(void(^)(NSError * error))reply;

- (void)executeWhenConnected:(void(^)(id proxy))task
    onError:(void(^)(NSError * error))handleError;

@property(weak) IBOutlet ZetaNotificationCenter * notificationCenter;

@end
