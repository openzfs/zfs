//
//  IDSerialLinker.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.05.03.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_SERIALLINKER_HPP
#define ID_SERIALLINKER_HPP

#include "IDBaseLinker.hpp"

namespace ID
{
	class SerialLinker : public BaseLinker
	{
	public:
		explicit SerialLinker(std::string base, LogClient const & logger);

	public:
		virtual void diskAppeared(DADiskRef disk, DiskInformation const & info) override;

	private:
		std::string formatSerialPath(DiskInformation const & di) const;
	};
}

#endif
