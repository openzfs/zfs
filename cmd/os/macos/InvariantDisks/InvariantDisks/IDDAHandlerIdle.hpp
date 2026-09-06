//
//  IDDAHandlerIdle.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_DAHANDLERIDLE_HPP
#define ID_DAHANDLERIDLE_HPP

#include "IDDiskArbitrationHandler.hpp"
#include "IDDispatchUtils.hpp"

#include <string>

namespace ID
{
	class DAHandlerIdle : public DiskArbitrationHandler
	{
	public:
		explicit DAHandlerIdle(std::string base, int64_t idleTimeoutNS,
							   LogClient const & logger);

	public:
		virtual void diskAppeared(DADiskRef disk, DiskInformation const & info) override;
		virtual void diskDisappeared(DADiskRef disk, DiskInformation const & info) override;

	private:
		void idle();
		void busy();

	private:
		std::string m_base;
		int64_t m_idleTimeout;
		DispatchSource m_idleTimer;
	};
}

#endif
