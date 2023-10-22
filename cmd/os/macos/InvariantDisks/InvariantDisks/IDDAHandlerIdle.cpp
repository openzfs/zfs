//
//  IDDAHandlerIdle.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDDAHandlerIdle.hpp"

#include "IDFileUtils.hpp"

namespace ID
{
	DAHandlerIdle::DAHandlerIdle(std::string base, int64_t idleTimeoutNS,
								 LogClient const & logger) :
		DiskArbitrationHandler(logger),
		m_base(std::move(base)), m_idleTimeout(idleTimeoutNS),
		m_idleTimer(createSourceTimer(this, [](void * ctx){ static_cast<DAHandlerIdle*>(ctx)->idle(); }))
	{
		createPath(m_base);
		busy();
	}

	void DAHandlerIdle::diskAppeared(DADiskRef /*disk*/, DiskInformation const & /*info*/)
	{
		busy();
	}

	void DAHandlerIdle::diskDisappeared(DADiskRef /*disk*/, DiskInformation const & /*info*/)
	{
		busy();
	}

	void DAHandlerIdle::idle()
	{
		createFile(m_base + "/invariant.idle");
	}

	void DAHandlerIdle::busy()
	{
		removeFSObject(m_base + "/invariant.idle");
		scheduleSingleshot(m_idleTimer, m_idleTimeout);
	}
}
