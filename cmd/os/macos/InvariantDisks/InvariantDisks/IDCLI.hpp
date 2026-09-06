//
//  IDCLI.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_CLI_HPP
#define ID_CLI_HPP

#include <string>
#include <memory>

namespace ID
{
	class LogClient;

	class CLI
	{
	public:
		CLI(int & argc, char ** argv, LogClient & logger);
		~CLI();

	public:
		int exec();
		void stop();

	private:
		void showVersion() const;
		void showHelp() const;
		void parse(int & argc, char ** argv);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}

#endif
