//
//  IDDiskArbitrationDispatcher.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDDiskArbitrationDispatcher.hpp"

#include "IDDiskArbitrationHandler.hpp"
#include "IDDiskArbitrationUtils.hpp"

#include <DiskArbitration/DiskArbitration.h>

#include <thread>
#include <vector>
#include <algorithm>

namespace ID
{
	struct DiskArbitrationDispatcher::Impl
	{
		std::mutex mutex;
		std::vector<Handler> handler;
		DASessionRef session = nullptr;
		bool scheduled = false;
	};

	DiskArbitrationDispatcher::DiskArbitrationDispatcher() :
		m_impl(new Impl)
	{
		m_impl->session = DASessionCreate(kCFAllocatorDefault);
		DARegisterDiskAppearedCallback(m_impl->session, nullptr, [](DADiskRef disk, void * ctx)
			{ static_cast<DiskArbitrationDispatcher*>(ctx)->diskAppeared(disk); }, this);
		DARegisterDiskDisappearedCallback(m_impl->session, nullptr, [](DADiskRef disk, void * ctx)
			{ static_cast<DiskArbitrationDispatcher*>(ctx)->diskDisappeared(disk); }, this);
	}

	DiskArbitrationDispatcher::~DiskArbitrationDispatcher()
	{
		stop();
		CFRelease(m_impl->session);
	}

	void DiskArbitrationDispatcher::addHandler(Handler handler)
	{
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		m_impl->handler.push_back(std::move(handler));
	}

	void DiskArbitrationDispatcher::removeHandler(Handler const & handler)
	{
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		m_impl->handler.erase(std::find(m_impl->handler.begin(), m_impl->handler.end(), handler),
							  m_impl->handler.end());
	}

	void DiskArbitrationDispatcher::clearHandler()
	{
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		m_impl->handler.clear();
	}

	void DiskArbitrationDispatcher::start()
	{
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		if (!m_impl->scheduled)
		{
			DASessionScheduleWithRunLoop(m_impl->session,
				CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
			m_impl->scheduled = true;
		}
	}

	void DiskArbitrationDispatcher::stop()
	{
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		if (m_impl->scheduled)
		{
			DASessionUnscheduleFromRunLoop(m_impl->session,
				CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
			m_impl->scheduled = false;
		}
	}

	void DiskArbitrationDispatcher::diskAppeared(DADiskRef disk) const
	{
		DiskInformation info = getDiskInformation(disk);
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		for (auto const & handler: m_impl->handler)
			handler->diskAppeared(disk, info);
	}

	void DiskArbitrationDispatcher::diskDisappeared(DADiskRef disk) const
	{
		DiskInformation info = getDiskInformation(disk);
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		for (auto const & handler: m_impl->handler)
			handler->diskDisappeared(disk, info);
	}
}
