//
//  IDFileUtils.mm
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDFileUtils.hpp"

#include "IDException.hpp"

#import <Foundation/NSFileManager.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>

namespace ID
{
	void createPath(std::string const & path)
	{
		NSError * error = nullptr;
		NSFileManager * manager = [NSFileManager defaultManager];
		BOOL success = [manager createDirectoryAtPath:[NSString stringWithUTF8String:path.c_str()]
						  withIntermediateDirectories:YES attributes:nullptr error:&error];
		if (!success)
		{
			Throw<Exception> e;
			e << "Error creating directory " << path << ": " << [[error description] UTF8String];
		}
	}

	void createCleanPath(std::string const & path)
	{
		removeFSObject(path);
		createPath(path);
	}

	void createFile(std::string const & path)
	{
		if (path.empty())
			throw Exception("Can not create file with empty path");
		removeFSObject(path);
		NSError * error = nullptr;
		NSData * empty = [NSData data];
		BOOL success = [empty writeToFile:[NSString stringWithUTF8String:path.c_str()]
								  options:0 error:&error];
		if (!success)
		{
			Throw<Exception> e;
			e << "Error creating file " << path << ": " << [[error description] UTF8String];
		}
	}

	void createSymlink(std::string const & link, std::string const & target)
	{
		if (link.empty() || target.empty())
			throw Exception("Can not create symlink with empty path");
		removeFSObject(link);
		NSError * error = nullptr;
		NSFileManager * manager = [NSFileManager defaultManager];
		BOOL success = [manager createSymbolicLinkAtPath:[NSString stringWithUTF8String:link.c_str()]
									 withDestinationPath:[NSString stringWithUTF8String:target.c_str()]
												   error:&error];
		if (!success)
		{
			Throw<Exception> e;
			e << "Error creating symlink " << link << " pointing to " << target << ": "
				<< [[error description] UTF8String];
		}
	}

	void removeFSObject(std::string const & path)
	{
		if (path.empty())
			throw Exception("Can not remove file system object with empty path");
		NSError * error = nullptr;
		NSFileManager * manager = [NSFileManager defaultManager];
		BOOL success = [manager removeItemAtPath:[NSString stringWithUTF8String:path.c_str()] error:&error];
		if (!success && error.domain != NSCocoaErrorDomain && error.code != 4)
		{
			NSError * underlying = error.userInfo[NSUnderlyingErrorKey];
			if (underlying && underlying.domain == NSPOSIXErrorDomain && underlying.code == ENOENT)
				return; // Ignore non-existing files
			Throw<Exception> e;
			e << "Error removing file system object " << path << ": " << [[error description] UTF8String];
		}
	}
}
