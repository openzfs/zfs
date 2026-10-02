// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2020, DataCore Software Corp.
 */

#pragma once

#pragma clang diagnostic ignored "-Wignored-attributes"
#pragma clang diagnostic ignored "-Wextern-initializer"

static const int TRACE_FATAL = 1;
static const int TRACE_ERROR = 2;
static const int TRACE_WARNING = 3;
static const int TRACE_INFO = 4;
static const int TRACE_VERBOSE = 5;
static const int TRACE_NOISY = 8;

#ifdef WPPFILE
#define	WPPNAME		OpenZFSTraceGuid
#define	WPPGUID		c20c603c, afd4, 467d, bf76, c0a4c10553df

#define	WPP_DEFINE_DEFAULT_BITS \
	WPP_DEFINE_BIT(MYDRIVER_ALL_INFO) \
	WPP_DEFINE_BIT(TRACE_KDPRINT) \
	WPP_DEFINE_BIT(DEFAULT_TRACE_LEVEL)

#undef WPP_DEFINE_CONTROL_GUID
#define	WPP_CONTROL_GUIDS \
	WPP_DEFINE_CONTROL_GUID(WPPNAME, (WPPGUID), \
	WPP_DEFINE_DEFAULT_BITS)

#define	WPP_FLAGS_LEVEL_LOGGER(Flags, level)                                  \
    WPP_LEVEL_LOGGER(Flags)

#define	WPP_FLAGS_LEVEL_ENABLED(Flags, level) \
	(WPP_LEVEL_ENABLED(Flags) && \
    WPP_CONTROL(WPP_BIT_ ## Flags).Level >= level)

#define	WPP_LEVEL_FLAGS_LOGGER(lvl, flags) \
	WPP_LEVEL_LOGGER(flags)

#define	WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
	(WPP_LEVEL_ENABLED(flags) && \
	WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)


// begin_wpp config
// FUNC TraceEvent{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
// end_wpp

#define	STRINGIZE_DETAIL(x) #x
#define	STRINGIZE(x) STRINGIZE_DETAIL(x)

#include STRINGIZE(WPPFILE)

#else

#undef WPP_INIT_TRACING
#define	WPP_INIT_TRACING(...)	((void)(0, __VA_ARGS__))

#undef WPP_CLEANUP
#define	WPP_CLEANUP(...)	((void)(0, __VA_ARGS__))
#endif

#ifndef WPP_CHECK_INIT
#define	WPP_CHECK_INIT
#endif


void ZFSWppInit(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath);

void ZFSWppCleanup(PDRIVER_OBJECT pDriverObject);
