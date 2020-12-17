# Redistribution and use is allowed under the OSI-approved 3-clause BSD license.
# Copyright (c) 2018 Sergey Podobry (sergey.podobry at gmail.com). All rights reserved.

#.rst:
# FindWDK
# ----------
#
# This module searches for the installed Windows Development Kit (WDK) and 
# exposes commands for creating kernel drivers and kernel libraries.
#
# Output variables:
# - `WDK_FOUND` -- if false, do not try to use WDK
# - `WDK_ROOT` -- where WDK is installed
# - `WDK_VERSION` -- the version of the selected WDK
# - `WDK_WINVER` -- the WINVER used for kernel drivers and libraries 
#        (default value is `0x0601` and can be changed per target or globally)
#
# Example usage:
#
#   find_package(WDK REQUIRED)
#
#   wdk_add_library(KmdfCppLib STATIC KMDF 1.15
#       KmdfCppLib.h 
#       KmdfCppLib.cpp
#       )
#   target_include_directories(KmdfCppLib INTERFACE .)
#
#   wdk_add_driver(KmdfCppDriver KMDF 1.15
#       Main.cpp
#       )
#   target_link_libraries(KmdfCppDriver KmdfCppLib)
#

if(DEFINED ENV{WDKContentRoot})
    file(GLOB WDK_NTDDK_FILES
        "$ENV{WDKContentRoot}/Include/*/km/ntddk.h"
    )
else()
    file(GLOB WDK_NTDDK_FILES
        "C:/Program Files*/Windows Kits/10/Include/*/km/ntddk.h"
    )
endif()

if(WDK_NTDDK_FILES)
    list(GET WDK_NTDDK_FILES -1 WDK_LATEST_NTDDK_FILE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WDK REQUIRED_VARS WDK_LATEST_NTDDK_FILE)

if (NOT WDK_LATEST_NTDDK_FILE)
    return()
endif()

get_filename_component(WDK_ROOT "${WDK_LATEST_NTDDK_FILE}" DIRECTORY)
get_filename_component(WDK_ROOT "${WDK_ROOT}" DIRECTORY)
get_filename_component(WDK_VERSION "${WDK_ROOT}" NAME)
get_filename_component(WDK_ROOT "${WDK_ROOT}" DIRECTORY)
get_filename_component(WDK_ROOT "${WDK_ROOT}" DIRECTORY)

message(STATUS "WDK_ROOT: " "${WDK_ROOT}")
message(STATUS "WDK_VERSION: " "${WDK_VERSION}")

set(WDK_WINVER "0x0601" CACHE STRING "Default WINVER for WDK targets")

set(WDK_ADDITIONAL_FLAGS_FILE "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/wdkflags.h")
file(WRITE "${WDK_ADDITIONAL_FLAGS_FILE}" "#pragma runtime_checks(\"suc\", off)")

define_property(TARGET 
	PROPERTY WDK_TARGET
	BRIEF_DOCS "Defines the target to be a WDK driver/library"
	FULL_DOCS "Defines the target to be a WDK driver/library"
)

list(APPEND WDK_COMPILE_FLAGS
	"/X"
    "/Zp8" # set struct alignment
    "/GF"  # enable string pooling
    "/GR-" # disable RTTI
    "/Gz" # __stdcall by default
    "/kernel"  # create kernel mode binary
    "/FIwarning.h" # disable warnings in WDK headers
    "/FI${WDK_ADDITIONAL_FLAGS_FILE}" # include file to disable RTC
)

list(APPEND WDK_COMPILE_DEFINITIONS
	"WINNT=1"
)
list(APPEND WDK_COMPILE_DEFINITIONS_DEBUG
	"MSC_NOOPT"
	"DEPRECATE_DDK_FUNCTIONS=1"
	"DBG=1"
)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND WDK_COMPILE_DEFINITIONS 
		"_X86_=1"
		"i386=1"
		"STD_CALL"
	)
    set(WDK_PLATFORM "x86")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND WDK_COMPILE_DEFINITIONS 
		"_WIN64"
		"_AMD64_"
		"AMD64"
	)
    set(WDK_PLATFORM "x64")
else()
    message(FATAL_ERROR "Unsupported architecture")
endif()

list(APPEND WDK_INCLUDE_DIRECTORIES
	"${WDK_ROOT}/Include/${WDK_VERSION}/shared"
    "${WDK_ROOT}/Include/${WDK_VERSION}/km"
	"${WDK_ROOT}/Include/${WDK_VERSION}/km/crt"
)

# Generate imported targets for WDK lib files
file(GLOB WDK_LIBRARIES "${WDK_ROOT}/Lib/${WDK_VERSION}/km/${WDK_PLATFORM}/*.lib")    
foreach(LIBRARY IN LISTS WDK_LIBRARIES)
    get_filename_component(LIBRARY_NAME "${LIBRARY}" NAME_WE)
    string(TOUPPER ${LIBRARY_NAME} LIBRARY_NAME)
    add_library(WDK::${LIBRARY_NAME} INTERFACE IMPORTED)
    set_property(TARGET WDK::${LIBRARY_NAME} PROPERTY INTERFACE_LINK_LIBRARIES "${LIBRARY}")
endforeach(LIBRARY)
unset(WDK_LIBRARIES)

function(wdk_add_driver _target)
    cmake_parse_arguments(WDK "" "KMDF;WINVER" "" ${ARGN})

    add_executable(${_target} ${WDK_UNPARSED_ARGUMENTS})

    set_target_properties(
		${_target} PROPERTIES
		SUFFIX ".sys"
		WDK_TARGET 1
		COMPILE_PDB_NAME ${_target}
		COMPILE_PDB_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
	target_compile_options(${_target} PRIVATE ${WDK_COMPILE_FLAGS})
	target_compile_definitions(${_target} PRIVATE
		${WDK_COMPILE_DEFINITIONS}
		$<$<CONFIG:Debug>:${WDK_COMPILE_DEFINITIONS_DEBUG}>
		_WIN32_WINNT=${WDK_WINVER}
	)
	target_link_options(${_target} PRIVATE
		"/MANIFEST:NO"
		"/DRIVER"
		"/OPT:REF"
		"/INCREMENTAL:NO"
		"/OPT:ICF"
		"/SUBSYSTEM:NATIVE"
		"/MERGE:_TEXT=.text"
		"/MERGE:_PAGE=PAGE"
		"/NODEFAULTLIB" # do not link default CRT
		"/SECTION:INIT,d"
		"/VERSION:10.0"
	)

    target_include_directories(${_target} SYSTEM PRIVATE ${WDK_INCLUDE_DIRECTORIES})
    target_link_libraries(${_target} PRIVATE WDK::NTOSKRNL WDK::HAL WDK::WMILIB)

	if (WDK_WINVER LESS 0x0602)
		target_link_libraries(${_target} PRIVATE WDK::BUFFEROVERFLOWK)
	else()
		target_link_libraries(${_target} PRIVATE WDK::BUFFEROVERFLOWFASTFAILK)
	endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_libraries(${_target} PRIVATE WDK::MEMCMP)
    endif()

    if(DEFINED WDK_KMDF)
        target_include_directories(${_target} SYSTEM PRIVATE "${WDK_ROOT}/Include/wdf/kmdf/${WDK_KMDF}")
        target_link_libraries(${_target} PRIVATE
            "${WDK_ROOT}/Lib/wdf/kmdf/${WDK_PLATFORM}/${WDK_KMDF}/WdfDriverEntry.lib"
            "${WDK_ROOT}/Lib/wdf/kmdf/${WDK_PLATFORM}/${WDK_KMDF}/WdfLdr.lib"
        )

        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
			target_link_options(${_target} PRIVATE "/ENTRY:FxDriverEntry@8")
        elseif(CMAKE_SIZEOF_VOID_P  EQUAL 8)
			target_link_options(${_target} PRIVATE "/ENTRY:FxDriverEntry")
        endif()
    else()
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
			target_link_options(${_target} PRIVATE "/ENTRY:GsDriverEntry@8")
        elseif(CMAKE_SIZEOF_VOID_P  EQUAL 8)
			target_link_options(${_target} PRIVATE "/ENTRY:GsDriverEntry")
        endif()
    endif()
endfunction()

function(wdk_add_library _target)
    cmake_parse_arguments(WDK "" "KMDF;WINVER" "" ${ARGN})

    add_library(${_target} STATIC ${WDK_UNPARSED_ARGUMENTS})

	set_target_properties(
		${_target} PROPERTIES
		WDK_TARGET 1
		COMPILE_PDB_NAME ${_target}
		COMPILE_PDB_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
    target_compile_options(${_target} PRIVATE ${WDK_COMPILE_FLAGS})
    target_compile_definitions(${_target} PRIVATE
		${WDK_COMPILE_DEFINITIONS}
		$<$<CONFIG:Debug>:${WDK_COMPILE_DEFINITIONS_DEBUG}>
		_WIN32_WINNT=${WDK_WINVER}
	)

	target_include_directories(${_target} SYSTEM PRIVATE ${WDK_INCLUDE_DIRECTORIES})

    if(DEFINED WDK_KMDF)
        target_include_directories(${_target} SYSTEM PRIVATE "${WDK_ROOT}/Include/wdf/kmdf/${WDK_KMDF}")
    endif()
endfunction()
