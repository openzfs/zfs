//
//  main.m
//  ZetaAuthorizationHelper
//
//  Created by cbreak on 18.01.01.
//  Copyright © 2018 the-color-black.net. All rights reserved.
//

#include "ZetaAuthorizationHelper.h"

#import <Foundation/Foundation.h>

int main(int argc, const char * argv[])
{
	@autoreleasepool
	{
		ZetaAuthorizationHelper *  helper = [[ZetaAuthorizationHelper alloc] init];
		[helper run];
	}
	return 0;
}
