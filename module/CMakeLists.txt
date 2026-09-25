# x86/inf2cat.exe can run on 64bit, and arm64.
find_program(INF2CAT_PROGRAM
	inf2cat
	PATHS "${WDK_ROOT}/bin/${WDK_VERSION}/x86" # no x64 version of the tool
	NO_DEFAULT_PATH
)

if (NOT INF2CAT_PROGRAM)
	message(FATAL_ERROR "Failed to find inf2cat.exe")
endif()

# Alas FindWDK sets SIGNTOOL without cross-compiling understanding.
unset(SIGNTOOL_PROGRAM CACHE)
unset(STAMPINF_PROGRAM CACHE)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
	find_program(SIGNTOOL_PROGRAM
		signtool
		HINTS
			"${WDK_ROOT}/bin/${WDK_VERSION}/x86"
			"$ENV{WINSDK_NUGET_ROOT}/bin/${WDK_VERSION}/x86"
		NO_DEFAULT_PATH
	)
	find_program(STAMPINF_PROGRAM
		stampinf
		HINTS "${WDK_ROOT}/bin/${WDK_VERSION}/x86"
		NO_DEFAULT_PATH
	)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	find_program(SIGNTOOL_PROGRAM
		signtool
		HINTS
			"${WDK_ROOT}/bin/${WDK_VERSION}/x64"
			"$ENV{WINSDK_NUGET_ROOT}/bin/${WDK_VERSION}/x64"
		NO_DEFAULT_PATH
	)
	find_program(STAMPINF_PROGRAM
		stampinf
		HINTS "${WDK_ROOT}/bin/${WDK_VERSION}/x64"
		NO_DEFAULT_PATH
	)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
	find_program(SIGNTOOL_PROGRAM
		signtool
		HINTS
			"${WDK_ROOT}/bin/${WDK_VERSION}/arm64"
			"$ENV{WINSDK_NUGET_ROOT}/bin/${WDK_VERSION}/arm64"
		NO_DEFAULT_PATH
	)
	find_program(STAMPINF_PROGRAM
		stampinf
		HINTS "${WDK_ROOT}/bin/${WDK_VERSION}/arm64"
		NO_DEFAULT_PATH
	)
endif()

if (NOT SIGNTOOL_PROGRAM)
	message(FATAL_ERROR "Failed to find signtool.exe")
endif()

find_program(STAMPINF_PROGRAM
	stampinf
	PATHS "${WDK_ROOT}/bin/${WDK_VERSION}/${WDK_PLATFORM}"
	NO_DEFAULT_PATH
)

if (NOT STAMPINF_PROGRAM)
	message(FATAL_ERROR "Failed to find stampinf.exe")
endif()

# Local per-developer override: set OPENZFS_SIGNTOOL_SHA1 as an OS
# environment variable to sign test builds with your own certificate.
# Leave it unset to build unsigned - all signing steps are skipped
# automatically. CI overrides it via -D on the command line instead.
if (NOT OPENZFS_SIGNTOOL_SHA1 AND DEFINED ENV{OPENZFS_SIGNTOOL_SHA1})
	set(OPENZFS_SIGNTOOL_SHA1 "$ENV{OPENZFS_SIGNTOOL_SHA1}" CACHE STRING "SHA1 of the certificate.")
endif()

if (NOT "${OPENZFS_SIGNTOOL_CERTSTORE}")
	set(OPENZFS_SIGNTOOL_CERTSTORE PrivateCertStore CACHE STRING "Name of the certificate store (PrivateCertStore) that contains the test certificate.")
endif()
if (NOT "${OPENZFS_SIGNTOOL_CERTNAME}")
	set(OPENZFS_SIGNTOOL_CERTNAME "OpenZFS Test Signing Certificate" CACHE STRING "Name of the certificate (OpenZFS Test Signing Certificate) that is installed in the specified certificate store.")
endif()
if (NOT "${OPENZFS_SIGNTOOL_TSA}")
	set(OPENZFS_SIGNTOOL_TSA "http://timestamp.digicert.com" CACHE STRING "Specifies URL of the TSA (http://timestamp.digicert.com) which will time stamp the digital signature.")
endif()

add_definitions(
	-D_CRT_SECURE_NO_WARNINGS
	-D_CRT_NONSTDC_NO_WARNINGS
)
list(APPEND WDK_COMPILE_FLAGS
	$<IF:$<CONFIG:Debug>,/MTd,/MT>
)
#list(APPEND WDK_LINK_FLAGS
#	/DEBUG
#)

set(CMAKE_C_STANDARD_LIBRARIES "")

# add_definitions(-D_CRT_LOADCFG_DISABLE_CET)
# list(APPEND WDK_COMPILE_DEFINITIONS	-D_CRT_LOADCFG_DISABLE_CET)
# What the blazes is __guard_eh_cont_table


# Silence MSVC++ things
add_compile_options(
	/we4013 # 'function' undefined; assuming extern returning int

	/wd4057 # 'operator' : 'identifier1' indirection to slightly different base types from 'identifier2'
	/wd4100 # 'identifier' : unreferenced formal parameter
	/wd4152 # nonstandard extension, function/data pointer conversion in expression
	/wd4200 # nonstandard extension used : zero-sized array in struct/union
	/wd4201 # nonstandard extension used : nameless struct/union
	/wd4211 # nonstandard extension used: redefined extern to static
	/wd4204 # nonstandard extension used: non-constant aggregate initializer
	/wd4210 # nonstandard extension used : function given file scope
	/wd4389 # 'operator' : signed/unsigned mismatch
	/wd4706 # assignment within conditional expression
	/wd4131 # uses old-style declarator
	/wd4115 # named type definition in parentheses
	/wd4018 # '>=': signed/unsigned mismatch
	/wd4206 # nonstandard extension used: translation unit is empty
	/wd4324 # structure was padded due to alignment specifier
	/wd4053 # one void operand for '?:'
)

string(REPLACE "/W3" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
string(REGEX REPLACE "[-/]DWIN32" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

set(CRT_LINK_REGEX "/M[TD]d?")
string(REGEX MATCH "${CRT_LINK_REGEX}" CRT_FLAG_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
string(REGEX REPLACE "${CRT_LINK_REGEX}" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
message(STATUS "CMAKE_C_FLAGS_DEBUG is now ${CMAKE_C_FLAGS_DEBUG}")

string(REGEX MATCH "${CRT_LINK_REGEX}" CRT_FLAG_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
string(REGEX REPLACE "${CRT_LINK_REGEX}" "" CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")

string(REGEX MATCH "${CRT_LINK_REGEX}" CRT_FLAG_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
string(REGEX REPLACE "${CRT_LINK_REGEX}" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

string(REGEX MATCH "${CRT_LINK_REGEX}" CRT_FLAG_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
string(REGEX REPLACE "${CRT_LINK_REGEX}" "" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

add_compile_options(
	$<$<AND:$<CONFIG:Debug>,$<NOT:$<BOOL:$<TARGET_PROPERTY:WDK_TARGET>>>>:${CRT_FLAG_DEBUG}>
	$<$<AND:$<CONFIG:MinSizeRel>,$<NOT:$<BOOL:$<TARGET_PROPERTY:WDK_TARGET>>>>:${CRT_FLAG_MINSIZEREL}>
	$<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:$<TARGET_PROPERTY:WDK_TARGET>>>>:${CRT_FLAG_RELEASE}>
	$<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<BOOL:$<TARGET_PROPERTY:WDK_TARGET>>>>:${CRT_FLAG_RELWITHDEBINFO}>
)

function(um_add_executable name)
	add_executable(${ARGV})

	target_link_options(${name} PRIVATE "/MANIFESTUAC:level=\"asInvoker\" uiAccess=\"false\"")
	if (OPENZFS_SIGNTOOL_SHA1)
		add_custom_command(
			TARGET ${name}
			POST_BUILD
			#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${OPENZFS_SIGNTOOL_CERTSTORE}" /n "${OPENZFS_SIGNTOOL_CERTNAME}" /t "${OPENZFS_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
			#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${OPENZFS_SIGNTOOL_CERTSTORE}" /n "${OPENZFS_SIGNTOOL_CERTNAME}" /tr "${OPENZFS_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
			#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /sha1 "${OPENZFS_SIGNTOOL_SHA1}" /t "${OPENZFS_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
			# Lundman's line
			COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256 /sha1 "${OPENZFS_SIGNTOOL_SHA1}" /tr "${OPENZFS_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
			WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
			VERBATIM COMMENT "Sign userspace tool"
		)
	endif()
endfunction()

function(tracewpp OUTPUT_DIR SOURCE)

	if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
		set(WDK_HOST_PLATFORM "x86")
	elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
		set(WDK_HOST_PLATFORM "x64")
	elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
		set(WDK_HOST_PLATFORM "arm64")
	endif()

	find_program(TRACEWPP_PROGRAM
		tracewpp
		HINTS
			"${WDK_ROOT}/bin/${WDK_VERSION}/${WDK_HOST_PLATFORM}"
			"$ENV{WINSDK_NUGET_ROOT}/bin/${WDK_VERSION}/${WDK_HOST_PLATFORM}"
		NO_DEFAULT_PATH
	)

	if (NOT TRACEWPP_PROGRAM)
		message(FATAL_ERROR "Failed to find tracewpp.exe")
	endif()

	file(GLOB WDF_TRACE_ENUMS
		"${WDK_ROOT}/Include/wdf/kmdf/*/wdftraceenums.h"
	)

	list(SORT WDF_TRACE_ENUMS)
	list(REVERSE WDF_TRACE_ENUMS)
	list(GET WDF_TRACE_ENUMS 0 SCAN)

	if (NOT EXISTS "${SCAN}")
		message(FATAL_ERROR "Failed to find wdftraceenums.h")
	endif()

	get_filename_component(FILEN ${SOURCE} NAME)
	set(TMH_FILEN ${FILEN}.tmh)
	set(TMH ${OUTPUT_DIR}/${TMH_FILEN})

	file(TO_NATIVE_PATH "${SOURCE}" NATIVE_SOURCE)
	file(TO_NATIVE_PATH "${OUTPUT_DIR}" NATIVE_OUTPUT_DIR)
	file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}" NATIVE_CURRENT_BINARY_DIR)

	get_filename_component(TRACEWPP_DIR "${TRACEWPP_PROGRAM}" DIRECTORY)
	get_filename_component(WPP_BIN_DIR "${TRACEWPP_DIR}" DIRECTORY)
	set(CFGDIR "${WPP_BIN_DIR}/wppconfig/rev1")

	file(TO_NATIVE_PATH "${CFGDIR}" NATIVE_CFGDIR)

	set(TRACE "TraceEvent{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...)")
#	set(SCAN "${WDK_ROOT}/Include/wdf/kmdf/1.9/WdfTraceEnums.h")
	file(TO_NATIVE_PATH "${SCAN}" NATIVE_SCAN)

	set(WPP_MACRO "WPP_INLINE __inline")

	execute_process(
		COMMAND "${TRACEWPP_PROGRAM}"
			-scan:${NATIVE_SCAN}
			/D${WPP_MACRO}
			-cfgdir:${NATIVE_CFGDIR}
			-I${NATIVE_CURRENT_BINARY_DIR}
			-odir:${NATIVE_OUTPUT_DIR}
			-km
			-func:${TRACE}
			-gen:{km-default.tpl}*.tmh
			${NATIVE_SOURCE}
		RESULT_VARIABLE TRACEWPP_RESULT
		OUTPUT_VARIABLE TRACEWPP_STDOUT
		ERROR_VARIABLE TRACEWPP_STDERR
	)

	if (NOT TRACEWPP_RESULT EQUAL 0)
		message(FATAL_ERROR
			"tracewpp failed with exit code ${TRACEWPP_RESULT}\n"
			"stdout: ${TRACEWPP_STDOUT}\n"
			"stderr: ${TRACEWPP_STDERR}")
	endif()
endfunction()

set(OpenZFS_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/include/os/windows/" "${CMAKE_SOURCE_DIR}/include/os/windows/spl" "${CMAKE_SOURCE_DIR}/include/os/windows/zfs" "${CMAKE_SOURCE_DIR}/include")

add_subdirectory(icp)
add_subdirectory(lua)
add_subdirectory(zfs)
add_subdirectory(zcommon)
add_subdirectory(nvpair)
add_subdirectory(zstd)

add_subdirectory(os/windows)
