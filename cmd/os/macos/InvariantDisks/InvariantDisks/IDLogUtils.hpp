//
//  IDASLUtils.h
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2015.08.01.
//  Copyright (c) 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_LOGUTILS_HPP
#define ID_LOGUTILS_HPP

#include <memory>
#include <sstream>

namespace ID
{
	class LogClient
	{
	public:
		explicit LogClient();

	public:
		int addLogFile(char const * logFile);

	public:
		template<typename... ARGS>
		void logInfo(ARGS const & ... args) const;

		template<typename... ARGS>
		void logDefault(ARGS const & ... args) const;

		template<typename... ARGS>
		void logError(ARGS const & ... args) const;

	public:
		void logInfo(std::string const & msg) const;
		void logDefault(std::string const & msg) const;
		void logError(std::string const & msg) const;

	private:
		class Impl;
		std::shared_ptr<Impl> m_impl;
	};

	// Private Implementation
	template<typename... ARGS>
	void LogClient::logInfo(ARGS const & ... args) const
	{
		std::stringstream ss;
		int ignored[] = {(ss << args, 0)...};
		(void)ignored;
		logInfo(ss.str());
	}

	template<typename... ARGS>
	void LogClient::logDefault(ARGS const & ... args) const
	{
		std::stringstream ss;
		int ignored[] = {(ss << args, 0)...};
		(void)ignored;
		logDefault(ss.str());
	}

	template<typename... ARGS>
	void LogClient::logError(ARGS const & ... args) const
	{
		std::stringstream ss;
		int ignored[] = {(ss << args, 0)...};
		(void)ignored;
		logError(ss.str());
	}
}

#endif
