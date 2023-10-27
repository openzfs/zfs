//
//  IDImagePathLinker.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDImagePathLinker.hpp"

#include "IDDiskArbitrationUtils.hpp"

namespace ID
{
	ImagePathLinker::ImagePathLinker(std::string const & base, LogClient const & logger) :
		BaseLinker(base, logger)
	{
	}

	static std::string formatImagePath(DiskInformation const & di)
	{
		std::string filteredPath = di.imagePath + partitionSuffix(di);
		std::replace(filteredPath.begin(), filteredPath.end(), '/', '-');
		return filteredPath;
	}

	void ImagePathLinker::diskAppeared(DADiskRef disk, DiskInformation const & di)
	{
		if (!di.imagePath.empty() && !di.mediaBSDName.empty())
		{
			std::string imagePath = formatImagePath(di);
			addLinkForDisk(base() + "/" + imagePath, di);
		}
	}
}
