//
//  IDDiskArbitrationDispatcher.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_DISKARBITRATIONDISPATCHER_HPP
#define ID_DISKARBITRATIONDISPATCHER_HPP

#include <DiskArbitration/DADisk.h>

#include <memory>

namespace ID
{
	class DiskArbitrationHandler;

	/*!
	 \brief Dispatches DiskArbitration events, wrapper around the DiskArbitration framework
	 */
	class DiskArbitrationDispatcher
	{
	public:
		typedef std::shared_ptr<DiskArbitrationHandler> Handler;

	public:
		DiskArbitrationDispatcher();
		~DiskArbitrationDispatcher();

	public:
		void addHandler(Handler handler);
		void removeHandler(Handler const & handler);
		void clearHandler();

	public:
		void start();
		void stop();

	private:
		void diskAppeared(DADiskRef disk) const;
		void diskDisappeared(DADiskRef disk) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}

#endif
