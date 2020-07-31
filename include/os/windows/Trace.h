/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2020, DataCore Software Corp.
 */

#pragma once

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
// FUNC dprintf{FLAGS=MYDRIVER_ALL_INFO, LEVEL=TRACE_INFO}(MSG, ...);
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
