//
//  ZetaAuthorizationHelperProtocol.h
//  ZetaWatch
//
//  Created by cbreak on 18.01.01.
//  Copyright © 2018 the-color-black.net. All rights reserved.
//

#define	kHelperToolMachServiceName @"org.openzfsonosx.ZetaAuthorizationHelper"

@protocol ZetaAuthorizationHelperProtocol

@required

- (void)getVersionWithReply:(void(^)(NSError * error, NSString * version))reply;

- (void)stopHelperWithAuthorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)importPools:(NSDictionary *)importData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)importablePools:(NSDictionary *)importData
    authorization:(NSData*)authData
    withReply:(void(^)(NSError * error, NSArray * importablePools))reply;

- (void)exportPools:(NSDictionary *)exportData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)mountFilesystems:(NSDictionary *)mountData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)unmountFilesystems:(NSDictionary *)mountData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)snapshotFilesystem:(NSDictionary *)fsData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)rollbackFilesystem:(NSDictionary *)fsData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)cloneSnapshot:(NSDictionary *)fsData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)createFilesystem:(NSDictionary *)fsData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)createVolume:(NSDictionary *)fsData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)destroy:(NSDictionary *)fsData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)loadKeyForFilesystem:(NSDictionary *)loadData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)unloadKeyForFilesystem:(NSDictionary *)unloadData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

- (void)scrubPool:(NSDictionary *)poolData
    authorization:(NSData *)authData
    withReply:(void(^)(NSError * error))reply;

@end
