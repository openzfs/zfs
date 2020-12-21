find_program(INF2CAT_PROGRAM
	inf2cat
	PATHS "${WDK_ROOT}/bin/${WDK_VERSION}/x86" # no x64 version of the tool
	NO_DEFAULT_PATH
)

if (NOT INF2CAT_PROGRAM)
	message(FATAL_ERROR "Failed to find inf2cat.exe")
endif()

find_program(SIGNTOOL_PROGRAM
	signtool
	PATHS "${WDK_ROOT}/bin/${WDK_VERSION}/${WDK_PLATFORM}"
	NO_DEFAULT_PATH
)

if (NOT SIGNTOOL_PROGRAM)
	message(FATAL_ERROR "Failed to find signtool.exe")
endif()

if (NOT "${ZFSIN_SIGNTOOL_CERTSTORE}")
	set(ZFSIN_SIGNTOOL_CERTSTORE PrivateCertStore CACHE STRING "Name of the certificate store (PrivateCertStore) that contains the test certificate.")
endif()
if (NOT "${ZFSIN_SIGNTOOL_CERTNAME}")
	set(ZFSIN_SIGNTOOL_CERTNAME "Contoso.com(Test)" CACHE STRING "Name of the certificate (Contoso.com(Test)) that is installed in the specified certificate store.")
endif()
if (NOT "${ZFSIN_SIGNTOOL_TSA}")
	set(ZFSIN_SIGNTOOL_TSA "http://timestamp.digicert.com" CACHE STRING "Specifies URL of the TSA (http://timestamp.digicert.com) which will time stamp the digital signature.")
endif()

add_definitions(
	-D_CRT_SECURE_NO_WARNINGS
	-D_CRT_NONSTDC_NO_WARNINGS
)
list(APPEND WDK_COMPILE_DEFINITIONS	-D_KERNEL -D__KERNEL__)
list(APPEND WDK_COMPILE_FLAGS
	$<IF:$<CONFIG:Debug>,/MTd,/MT>
)

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

	target_link_options(${name} PRIVATE "/MANIFESTUAC:level=\"requireAdministrator\" uiAccess=\"false\"")
	add_custom_command(
		TARGET ${name}
		POST_BUILD
		#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /t "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
		#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /tr "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
		COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /sha1 "7FAC1AF0A39DF0D2AB9F92D6A544ECECD791D7AB" /t "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
		COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /sha1 "7FAC1AF0A39DF0D2AB9F92D6A544ECECD791D7AB" /tr "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:${name}>
		WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
		VERBATIM COMMENT "Sign userspace tool"
	)
endfunction()

set(OpenZFS_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/include/os/windows/" "${CMAKE_SOURCE_DIR}/include/os/windows/spl" "${CMAKE_SOURCE_DIR}/include/os/windows/zfs" "${CMAKE_SOURCE_DIR}/include")

add_subdirectory(icp)
add_subdirectory(lua)
add_subdirectory(zfs)
add_subdirectory(zcommon)
add_subdirectory(nvpair)
add_subdirectory(unicode)
add_subdirectory(zstd)
add_subdirectory(os/windows)
