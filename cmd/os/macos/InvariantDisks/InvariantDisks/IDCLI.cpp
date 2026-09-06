//
//  IDCLI.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDCLI.hpp"

#include "IDException.hpp"
#include "IDDiskArbitrationDispatcher.hpp"
#include "IDDiskInfoLogger.hpp"
#include "IDDAHandlerIdle.hpp"
#include "IDMediaPathLinker.hpp"
#include "IDUUIDLinker.hpp"
#include "IDSerialLinker.hpp"
#include "IDImagePathLinker.hpp"
#include "IDDispatchUtils.hpp"
#include "IDLogUtils.hpp"

#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <algorithm>
#include <thread>
#include <functional>

#include <CoreFoundation/CoreFoundation.h>

#include <dispatch/dispatch.h>

#include "git-version.h"

namespace ID
{
	struct CLI::Impl
	{
		std::mutex mutex;
		DispatchSource signalSourceINT;
		DispatchSource signalSourceTERM;
		bool showHelp = false;
		bool verbose = false;
		std::string basePath = "/var/run/disk";
		std::string logPath;
		int64_t idleTimeoutNS = 4000000000;
		CFRunLoopRef runloop = nullptr;
		LogClient * logger = nullptr;
	};

	CLI::CLI(int & argc, char ** argv, LogClient & logger) :
		m_impl(new Impl)
	{
		m_impl->logger = &logger;
		// Setup
		dispatch_function_t stopHandler = [](void * ctx){ static_cast<CLI*>(ctx)->stop();};
		m_impl->signalSourceINT = createSourceSignal(SIGINT, this, stopHandler);
		m_impl->signalSourceTERM = createSourceSignal(SIGTERM, this, stopHandler);
		// UI
		showVersion();
		parse(argc, argv);
	}

	CLI::~CLI()
	{
	}

	int CLI::exec()
	{
		// Print help and terminate
		if (m_impl->showHelp)
		{
			showHelp();
			return 0;
		}
		// Start runloop
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			if (m_impl->runloop)
				throw Exception("CLI already running");
			m_impl->runloop = CFRunLoopGetCurrent();
		}
		auto & logger = *m_impl->logger;
		if (!m_impl->logPath.empty())
			logger.addLogFile(m_impl->logPath.c_str());
		DiskArbitrationDispatcher dispatcher;
		dispatcher.addHandler(std::make_shared<DAHandlerIdle>(m_impl->basePath, m_impl->idleTimeoutNS, logger));
		dispatcher.addHandler(std::make_shared<DiskInfoLogger>(m_impl->verbose, logger));
		dispatcher.addHandler(std::make_shared<MediaPathLinker>(m_impl->basePath + "/by-path", logger));
		dispatcher.addHandler(std::make_shared<UUIDLinker>(m_impl->basePath + "/by-id", logger));
		dispatcher.addHandler(std::make_shared<SerialLinker>(m_impl->basePath + "/by-serial", logger));
		dispatcher.addHandler(std::make_shared<ImagePathLinker>(m_impl->basePath + "/by-image-path", logger));
		dispatcher.start();
		CFRunLoopRun();
		{
			std::lock_guard<std::mutex> lock(m_impl->mutex);
			m_impl->runloop = nullptr;
		}
		return 0;
	}

	void CLI::stop()
	{
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		if (m_impl->runloop)
			CFRunLoopStop(m_impl->runloop);
	}

	struct CLIFlagHandler
	{
		size_t argCount;
		std::function<void(char **)> func;
	};

	void CLI::showVersion() const
	{
		std::cout << "InvariantDisk " << GIT_VERSION << std::endl;
	}

	void CLI::showHelp() const
	{
		std::cout << "Usage: InvariantDisks [-hv] [-p <basePath>] [-t <timeoutMS>]\n";
		std::cout << "\t-h:\tprint help and exit\n";
		std::cout << "\t-v:\tverbose logging\n";
		std::cout << "\t-p <basePath>:\tset base path for symlinks (" << m_impl->basePath << ")\n";
		std::cout << "\t-l <logPath>:\tset optional path for logging (" << m_impl->logPath << ")\n";
		std::cout << "\t-t <timeoutMS>:\tset idle timeout (" << m_impl->idleTimeoutNS/1000000 << " ms)\n";
	}

	void CLI::parse(int & argc, char ** argv)
	{
		// Command Line Parsing
		std::map<std::string, CLIFlagHandler> cliFlags =
		{
			{"-h", { 0, [&](char **){ m_impl->showHelp = true; }}},
			{"-v", { 0, [&](char **){ m_impl->verbose = true; }}},
			{"-p", { 1, [&](char ** a){ m_impl->basePath = a[1]; }}},
			{"-l", { 1, [&](char ** a){ m_impl->logPath = a[1]; }}},
			{"-t", { 1, [&](char ** a){
				try
				{
					int64_t timeoutInNS = std::stol(a[1])*1000000ll;
					if (timeoutInNS < 0)
						throw std::out_of_range("negative");
					m_impl->idleTimeoutNS = timeoutInNS;
				}
				catch (std::exception const & e)
				{
					Throw<Exception>() << "Idle Timeout " << a[1] << " is not a valid timeout: " << e.what();
				}
			}}}
		};
		for (int argIdx = 0; argIdx < argc; ++argIdx)
		{
			auto flagIt = cliFlags.find(argv[argIdx]);
			if (flagIt != cliFlags.end())
			{
				CLIFlagHandler const & f = flagIt->second;
				if (argIdx + f.argCount >= argc)
					Throw<Exception>() << "Flag " << argv[argIdx] << " requires " << f.argCount << " arguments";
				f.func(&argv[argIdx]);
				argIdx += f.argCount;
			}
		}
	}
}
