//
//  PathValueTransformer.m
//  ZetaWatch
//
//  Created by cbreak on 19.11.08.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#import <Foundation/Foundation.h>

/*!
 \brief Transforms a path URL into a string and back, used for value binding
 from a xib file.
 */
@interface PathValueTransformer : NSValueTransformer

@end

@implementation PathValueTransformer

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
	NSString * string = value;
	return [NSURL URLWithString:string];
}

- (id)reverseTransformedValue:(id)value
{
	NSURL * url = value;
	return [url path];
}

- (void)initialize
{
	if (self == [PathValueTransformer self])
	{
		PathValueTransformer * transformer = [[PathValueTransformer alloc] init];
		[NSValueTransformer setValueTransformer:transformer
										forName:@"PathValueTransformer"];
	}
}

@end
