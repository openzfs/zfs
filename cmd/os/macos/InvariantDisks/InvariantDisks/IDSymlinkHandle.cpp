//
//  IDSymlinkHandle.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2015.10.25.
//  Copyright (c) 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDSymlinkHandle.hpp"

#include "IDFileUtils.hpp"
#include "IDLogUtils.hpp"

namespace ID
{
	SymlinkHandle::SymlinkHandle()
	{
	}

	SymlinkHandle::SymlinkHandle(std::string const & link, std::string const & target) :
		m_link(link), m_target(target)
	{
		createSymlink(link, target);
	}

	SymlinkHandle::~SymlinkHandle()
	{
		try
		{
			reset();
		}
		catch (std::exception const & e)
		{
			// Swallow exceptions during destruction
		}
	}

	SymlinkHandle::SymlinkHandle(SymlinkHandle && other) noexcept :
		m_link(std::move(other.m_link)), m_target(std::move(other.m_target))
	{
		other.m_link.clear();
		other.m_target.clear();
	}

	SymlinkHandle & SymlinkHandle::operator=(SymlinkHandle && other)
	{
		swap(m_link, other.m_link);
		other.reset();
		return *this;
	}

	void SymlinkHandle::reset()
	{
		if (!m_link.empty())
		{
			removeFSObject(m_link);
			m_link.clear();
			m_target.clear();
		}
	}

	std::string const & SymlinkHandle::link() const
	{
		return m_link;
	}

	std::string const & SymlinkHandle::target() const
	{
		return m_target;
	}
}
