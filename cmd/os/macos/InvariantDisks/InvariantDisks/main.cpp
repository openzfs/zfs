//
//  main.cpp
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
#include "IDLogUtils.hpp"

#include <iostream>

int main(int argc, char ** argv)
{
	ID::LogClient logger;
	try
	{
		ID::CLI idCommandLine(argc, argv, logger);
		return idCommandLine.exec();
	}
	catch (ID::Exception const & e)
	{
		logger.logError(e.what());
	}
	catch (std::exception const & e)
	{
		logger.logError("Terminated by exception: ", e.what());
	}
	catch (...)
	{
		logger.logError("Terminated by unknown exception");
	}
	return -1;
}
