//
//  IDDispatchUtils.cpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.12.13.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#include "IDDispatchUtils.hpp"

namespace ID
{
	DispatchSource createSourceSignal(int sig, void * ctx, dispatch_function_t handler)
	{
		signal(sig, SIG_IGN);
		dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, sig, 0,
														  DISPATCH_TARGET_QUEUE_DEFAULT);
		dispatch_set_context(source, ctx);
		dispatch_source_set_event_handler_f(source, handler);
		dispatch_resume(source);
		return DispatchSource(source);
	}

	DispatchSource createSourceTimer(void * ctx, dispatch_function_t handler)
	{
		dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
														  DISPATCH_TARGET_QUEUE_DEFAULT);
		dispatch_set_context(source, ctx);
		dispatch_source_set_event_handler_f(source, handler);
		dispatch_resume(source);
		return DispatchSource(source);
	}

	void scheduleSingleshot(DispatchSource & timerSource, int64_t delayInNS)
	{
		dispatch_time_t start = dispatch_time(0, delayInNS);
		// Schedule a single shot timer with a tolerance of 256ms
		dispatch_source_set_timer(timerSource.get(), start, DISPATCH_TIME_FOREVER, 256000000);
	}
}
