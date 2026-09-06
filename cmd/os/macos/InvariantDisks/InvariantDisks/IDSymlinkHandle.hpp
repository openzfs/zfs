//
//  IDSymlinkHandle.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2015.10.25.
//  Copyright (c) 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_SYMLINK_HANDLE_HPP
#define ID_SYMLINK_HANDLE_HPP

#include <string>

namespace ID
{
	/*!
	 \brief A class representing a symlink in the file system

	 This class represents a symlink in the filesystem that only exists as long as its corresponding
	 instance exists.
	 */
	class SymlinkHandle
	{
	public:
		SymlinkHandle();
		explicit SymlinkHandle(std::string const & link, std::string const & target);
		~SymlinkHandle();

	public:
		SymlinkHandle(SymlinkHandle && other) noexcept;
		SymlinkHandle & operator=(SymlinkHandle && other);

	public:
		/*!
		 Resets the instance so that it represents no symlink.
		 */
		void reset();

	public:
		std::string const & link() const;
		std::string const & target() const;

	private:
		std::string m_link;
		std::string m_target;
	};
}

#endif
