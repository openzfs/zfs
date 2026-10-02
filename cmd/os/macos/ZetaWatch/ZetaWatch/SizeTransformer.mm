//
//  SizeTransformer.m
//  ZetaWatch
//
//  Created by cbreak on 19.11.08.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import <Foundation/Foundation.h>

#include "ZetaFormatHelpers.hpp"

/*!
 \brief Transforms between a string and a byte number, used for value binding
 from a xib file.
 */
@interface SizeTransformer : NSValueTransformer

@end

@implementation SizeTransformer

+ (Class)transformedValueClass
{
	return [NSString class];
}

+ (BOOL)allowsReverseTransformation
{
	return YES;
}

- (id)transformedValue:(id)value
{
	NSNumber * number = value;
	auto str = formatBytes([number unsignedLongLongValue]);
	return [NSString stringWithUTF8String:str.c_str()];
}

- (id)reverseTransformedValue:(id)value
{
	NSString * string = value;
	std::size_t bytes;
	if (parseBytes<std::size_t>([string UTF8String], bytes))
	{
		return [NSNumber numberWithUnsignedLongLong:bytes];
	}
	return nullptr;
}

- (void)initialize
{
	if (self == [SizeTransformer self])
	{
		SizeTransformer * transformer = [[SizeTransformer alloc] init];
		[NSValueTransformer setValueTransformer:transformer
										forName:@"SizeTransformer"];
	}
}

@end
