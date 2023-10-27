//
//  IDDispatchUtils.h
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.12.13.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_DISPATCHUTILS_HPP
#define ID_DISPATCHUTILS_HPP

#include <dispatch/dispatch.h>

#include <memory>

namespace ID
{
	struct DispatchDelete
	{
		void operator()(dispatch_source_s * source)
		{
			dispatch_source_set_event_handler_f(source, nullptr);
			dispatch_release(source);
		}
	};

	typedef std::unique_ptr<dispatch_source_s, DispatchDelete> DispatchSource;

	// Signal Source
	DispatchSource createSourceSignal(int sig, void * ctx, dispatch_function_t handler);

	// Timer Source
	DispatchSource createSourceTimer(void * ctx, dispatch_function_t handler);
	void scheduleSingleshot(DispatchSource & timerSource, int64_t delayInNS);
}

#endif
