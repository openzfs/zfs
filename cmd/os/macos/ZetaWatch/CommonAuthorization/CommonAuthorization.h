//
//  CommonAuthorization.h
//  ZetaWatch
//
//  Created by cbreak on 18.01.01.
//  Copyright © 2018 the-color-black.net. All rights reserved.
//

/* CSTYLED */
#import <Foundation/Foundation.h>

@interface CommonAuthorization : NSObject

// For a given command selector, return the associated authorization
// right name.
+ (NSString *)authorizationRightForCommand:(SEL)command;

// Set up the default authorization rights in the authorization database.
+ (void)setupAuthorizationRights:(AuthorizationRef)authRef;

@end
