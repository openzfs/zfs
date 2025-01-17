//
//  IDDiskArbitrationHandler.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_DISKARBITRATIONHANDLER_HPP
#define ID_DISKARBITRATIONHANDLER_HPP

#include <DiskArbitration/DADisk.h>

#include "IDLogUtils.hpp"

namespace ID
{
	class DiskInformation;

	class DiskArbitrationHandler
	{
	public:
		explicit DiskArbitrationHandler(LogClient const & logger) : m_logger(logger) {}
		virtual ~DiskArbitrationHandler() = default;

	public:
		virtual void diskAppeared(DADiskRef disk, DiskInformation const & info) = 0;
		virtual void diskDisappeared(DADiskRef disk, DiskInformation const & info) = 0;

	protected:
		LogClient const & logger() const { return m_logger; }

	private:
		LogClient m_logger;
	};
}

#endif
