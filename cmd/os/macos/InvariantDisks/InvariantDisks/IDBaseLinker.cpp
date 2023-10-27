//
//  IDBaseLinker.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.05.03.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDBaseLinker.hpp"

#include "IDDiskArbitrationUtils.hpp"
#include "IDFileUtils.hpp"

namespace ID
{
	BaseLinker::BaseLinker(std::string base, LogClient const & logger) :
		DiskArbitrationHandler(logger),
		m_base(std::move(base))
	{
		createCleanPath(m_base);
	}

	void BaseLinker::diskDisappeared(DADiskRef disk, DiskInformation const & di)
	{
		removeLinksForDisk(di);
	}

	void BaseLinker::addLinkForDisk(std::string const & link, DiskInformation const & di)
	{
		try
		{
			if (link.empty())
				return;
			std::string devicePath = "/dev/" + di.mediaBSDName;
			logger().logDefault("Creating symlink: ", link, " -> ", devicePath);
			m_links.emplace(devicePath, SymlinkHandle(link, devicePath));
		}
		catch (std::exception const & e)
		{
			logger().logError("Could not create symlink: ", e.what());
		}
	}

	void BaseLinker::removeLinksForDisk(DiskInformation const & di)
	{
		try
		{
			std::string devicePath = "/dev/" + di.mediaBSDName;
			auto found = m_links.equal_range(devicePath);
			for (auto it = found.first; it != found.second; ++it)
			{
				logger().logDefault("Removing symlink: ", it->second.link());
				it->second.reset();
			}
			m_links.erase(found.first, found.second);
		}
		catch (std::exception const & e)
		{
			logger().logError("Could not remove symlink: ", e.what());
		}
	}

	std::string const & BaseLinker::base() const
	{
		return m_base;
	}
}
