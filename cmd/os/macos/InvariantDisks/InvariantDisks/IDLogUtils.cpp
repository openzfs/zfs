//
//  IDASLUtils.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2015.08.01.
//  Copyright (c) 2015 the-color-black.net. All rights reserved.
//

#include "IDLogUtils.hpp"

#include <vector>
#include <algorithm>

#include <unistd.h>
#include <fcntl.h>

// Auto Detect the use of ASL or OS Log if it is not already enforced by the
// build environment
#if !(defined(ID_USE_ASL) || defined(ID_USE_OS_LOG))
	#include <TargetConditionals.h>
	#include <AvailabilityMacros.h>
	#if defined (MAC_OS_X_VERSION_10_12) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
		#define ID_USE_OS_LOG
	#else
		#define ID_USE_ASL
	#endif
#endif

#if defined(ID_USE_ASL)
// Uses ASL:
// https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/asl.3.html
#include <asl.h>
#elif defined(ID_USE_OS_LOG)
// Uses the new OS Log facilities:
// https://developer.apple.com/reference/os/logging
#include <os/log.h>
#else
#error "Neither ID_USE_ASL nor ID_USE_OS_LOG have been defined"
#endif

static char const * const logFacility = "net.the-color-black";
static char const * const logCategory = "InvariantDisks";

namespace ID
{
#if defined(ID_USE_ASL)

	class LogClient::Impl
	{
	public:
		explicit Impl() :
			client(asl_open(logCategory, logFacility, ASL_OPT_STDERR))
		{
		}

		~Impl()
		{
			asl_close(client);
			for (auto fd: fds)
				close(fd);
		}

	public:
		Impl(Impl const &) = delete;
		Impl & operator=(Impl const &) = delete;

	public:
		int addLogFile(char const * logFile)
		{
			int fd = open(logFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			if (fd < 0)
			{
				asl_log(client, 0, ASL_LEVEL_ERR, "Error opening log file \"%s\" (%m)", logFile);
				return -1;
			}
			int r = asl_add_log_file(client, fd);
			if (r != 0)
			{
				asl_log(client, 0, ASL_LEVEL_ERR, "Error registering file \"%s\" (%d)", logFile, r);
				close(fd);
				return -1;
			}
			fds.push_back(fd);
			return fd;
		}

		void removeLogFile(int fd)
		{
			auto found = std::find(fds.begin(), fds.end(), fd);
			if (found != fds.end())
			{
				std::swap(*found, fds.back());
				fds.pop_back();
				asl_remove_log_file(client, fd);
				close(fd);
			}
		}

	public:
		aslclient client;
		std::vector<int> fds;
	};

	void LogClient::logInfo(std::string const & msg) const
	{
		asl_log(m_impl->client, 0, ASL_LEVEL_INFO, "%s", msg.c_str());
	}

	void LogClient::logDefault(std::string const & msg) const
	{
		asl_log(m_impl->client, 0, ASL_LEVEL_NOTICE, "%s", msg.c_str());
	}

	void LogClient::logError(std::string const & msg) const
	{
		asl_log(m_impl->client, 0, ASL_LEVEL_ERR, "%s", msg.c_str());
	}

#elif defined(ID_USE_OS_LOG)

	class LogClient::Impl
	{
	public:
		Impl() :
			client(os_log_create(logFacility, logCategory))
		{
		}

		~Impl()
		{
			// Should release client from os_log_create, but no function for
			// this is documented or defined.
		}

	public:
		Impl(Impl const &) = delete;
		Impl & operator=(Impl const &) = delete;

	public:
		int addLogFile(char const *)
		{
			os_log_with_type(client, OS_LOG_TYPE_DEFAULT,
				"Log Files are no longer supported with os_log, use the logging subsystem instead");
			return 0;
		}

		void removeLogFile(int)
		{
		}

	public:
		os_log_t client;
	};

	void LogClient::logInfo(std::string const & msg) const
	{
		os_log_with_type(m_impl->client, OS_LOG_TYPE_INFO, "%{public}s", msg.c_str());
	}

	void LogClient::logDefault(std::string const & msg) const
	{
		os_log_with_type(m_impl->client, OS_LOG_TYPE_DEFAULT, "%{public}s", msg.c_str());
	}

	void LogClient::logError(std::string const & msg) const
	{
		os_log_with_type(m_impl->client, OS_LOG_TYPE_ERROR, "%{public}s", msg.c_str());
	}

#endif

	LogClient::LogClient() :
		m_impl(std::make_shared<Impl>())
	{
	}

	int LogClient::addLogFile(char const * logFile)
	{
		return m_impl->addLogFile(logFile);
	}
}
