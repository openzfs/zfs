//
//  IDDiskArbitrationUtils.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_DISKARBITRATIONUTILS_HPP
#define ID_DISKARBITRATIONUTILS_HPP

#include <DiskArbitration/DiskArbitration.h>

#include <iostream>
#include <string>

namespace ID
{
	struct DiskInformation
	{
		std::string volumeKind;
		std::string volumeUUID;
		std::string volumeName;
		std::string volumePath;
		std::string mediaKind;
		std::string mediaType;
		std::string mediaUUID;
		std::string mediaBSDName;
		std::string mediaName;
		std::string mediaPath;
		std::string mediaContent;
		bool isDevice;
		bool mediaWhole;
		bool mediaLeaf;
		bool mediaWritable;
		std::string deviceGUID;
		std::string devicePath;
		std::string deviceProtocol;
		std::string deviceModel;
		std::string busName;
		std::string busPath;
		std::string ioSerial;
		std::string imagePath;
	};

	DiskInformation getDiskInformation(DADiskRef disk);

	bool isDevice(DiskInformation const & di);
	bool isWhole(DiskInformation const & di);
	bool isRealDevice(DiskInformation const & di);
	std::string partitionSuffix(DiskInformation const & di);

	std::ostream & operator<<(std::ostream & os, DADiskRef disk);
	std::ostream & operator<<(std::ostream & os, DiskInformation const & disk);
}

#endif
