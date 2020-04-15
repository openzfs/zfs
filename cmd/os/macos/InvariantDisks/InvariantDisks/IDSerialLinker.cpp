//
//  IDSerialLinker.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.05.03.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDSerialLinker.hpp"

#include "IDDiskArbitrationUtils.hpp"

#include <string>
#include <algorithm>

namespace ID
{
	SerialLinker::SerialLinker(std::string base, LogClient const & logger) :
		BaseLinker(std::move(base), logger)
	{
	}

	static bool isInvalidSerialChar(char c)
	{
		if (isalnum(c))
			return false;
		if (c == '-' || c == '_')
			return false;
		return true;
	}

	std::string trim(std::string const & s)
	{
		size_t first = s.find_first_not_of(' ');
		size_t last = s.find_last_not_of(' ');
		if (first != std::string::npos)
			return s.substr(first, last - first + 1);
		return s;
	}

	std::string formatSerial(DiskInformation const & di)
	{
		std::string model = trim(di.deviceModel);
		std::string serial = trim(di.ioSerial);
		std::string formated;
		if (!serial.empty())
		{
			if (model.empty())
				formated = serial;
			else
				formated = model + "-" + serial;
		}
		std::replace(formated.begin(), formated.end(), ' ', '_');
		formated.erase(std::remove_if(formated.begin(), formated.end(), isInvalidSerialChar), formated.end());
		if (!formated.empty())
			formated += partitionSuffix(di);
		return formated;
	}

	std::string SerialLinker::formatSerialPath(DiskInformation const & di) const
	{
		std::string serial = formatSerial(di);
		if (!serial.empty())
			serial = base() + "/" + serial;
		return serial;
	}

	void SerialLinker::diskAppeared(DADiskRef disk, DiskInformation const & di)
	{
		if (isDevice(di) && isRealDevice(di))
		{
			addLinkForDisk(formatSerialPath(di), di);
		}
	}

}
