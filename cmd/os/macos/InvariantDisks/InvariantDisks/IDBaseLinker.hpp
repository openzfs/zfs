//
//  IDBaseLinker.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.05.03.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_BASE_LINKER_HPP
#define ID_BASE_LINKER_HPP

#include "IDDiskArbitrationHandler.hpp"

#include "IDSymlinkHandle.hpp"

#include <string>
#include <map>

namespace ID
{
	class BaseLinker : public DiskArbitrationHandler
	{
	public:
		explicit BaseLinker(std::string base, LogClient const & logger);

	public:
		virtual void diskDisappeared(DADiskRef disk, DiskInformation const & info) override;

	protected:
		void addLinkForDisk(std::string const & link, DiskInformation const & di);
		void removeLinksForDisk(DiskInformation const & di);
		std::string const & base() const;

	private:
		std::string m_base;
		std::multimap<std::string,SymlinkHandle> m_links;
	};
}

#endif
