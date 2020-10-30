#pragma once

#if !defined WPPFILE || !defined RUN_WPP    // To enable Wpp; Select Yes under Run Wpp Tracing in Project properties
#undef WPPFILE                              // And define both RUN_WPP and WPPFILE="%(FileName).tmh" in C/C++ preprocessor
#undef RUN_WPP                              // To disable Wpp; Select No under Run Wpp Tracing; undef RUN_WPP/WPPFILE in C/C++ preprocessor
#endif

static const int TRACE_FATAL = 1;
static const int TRACE_ERROR = 2;
static const int TRACE_WARNING = 3;
static const int TRACE_INFO = 4;
static const int TRACE_VERBOSE = 5;
static const int TRACE_NOISY = 8;

#if defined RUN_WPP && defined WPPFILE
#define WPPNAME		ZFSinTraceGuid
#define WPPGUID		c20c603c,afd4,467d,bf76,c0a4c10553df

#define WPP_DEFINE_DEFAULT_BITS                                        \
        WPP_DEFINE_BIT(MYDRIVER_ALL_INFO)                              \
        WPP_DEFINE_BIT(TRACE_KDPRINT)                                  \
        WPP_DEFINE_BIT(DEFAULT_TRACE_LEVEL)

#undef WPP_DEFINE_CONTROL_GUID	
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(WPPNAME,(WPPGUID), \
        WPP_DEFINE_DEFAULT_BITS )

#define WPP_FLAGS_LEVEL_LOGGER(Flags, level)                                  \
    WPP_LEVEL_LOGGER(Flags)

#define WPP_FLAGS_LEVEL_ENABLED(Flags, level)                                 \
    (WPP_LEVEL_ENABLED(Flags) && \
    WPP_CONTROL(WPP_BIT_ ## Flags).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)


// begin_wpp config
// FUNC dprintf{FLAGS=MYDRIVER_ALL_INFO, LEVEL=TRACE_INFO}(MSG, ...);
// FUNC TraceEvent{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
// end_wpp

#include WPPFILE

#else

#undef WPP_INIT_TRACING
#define WPP_INIT_TRACING(...)	((void)(0, __VA_ARGS__))

#undef WPP_CLEANUP
#define WPP_CLEANUP(...)	((void)(0, __VA_ARGS__))
#endif

#ifndef WPP_CHECK_INIT
#define WPP_CHECK_INIT
#endif


void ZFSWppInit(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath);

void ZFSWppCleanup(PDRIVER_OBJECT pDriverObject);