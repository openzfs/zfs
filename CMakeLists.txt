cmake_minimum_required(VERSION 3.13)
project(OpenZFS LANGUAGES C CXX ASM)
SET (CMAKE_ASM_COMPILER_ID Clang)
SET (CMAKE_ASM_COMPILER clang-cl.exe)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/contrib/windows/cmake")
find_package(WDK REQUIRED)

option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	set(OPENSSL_CRYPTO_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MTd/libcrypto_static.lib")
	set(OPENSSL_SSL_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MTd/libssl_static.lib")
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
	set(OPENSSL_CRYPTO_LIBRARY "C:/Program Files/OpenSSL-Win64-ARM/lib/VC/arm64/MTd/libcrypto_static.lib")
	set(OPENSSL_SSL_LIBRARY "C:/Program Files/OpenSSL-Win64-ARM/lib/VC/arm64/MTd/libssl_static.lib")
endif()

# Local per-developer overrides, e.g. a non-standard OpenSSL install path.
# OPENSSL_ROOT_DIR is already read from the environment by FindOpenSSL.cmake
# itself; these few also need to be pre-seeded into the cache here since the
# things that consume them (find_library/find_path calls below and in
# lib/libzfs/CMakeLists.txt) only check for an existing cache entry, not the
# environment. CI overrides the same variables via -D on the command line.
foreach(_openssl_override_var
	OPENSSL_INCLUDE_DIR
	LIB_EAY_DEBUG
	LIB_EAY_RELEASE
	SSL_EAY_DEBUG
	SSL_EAY_RELEASE
	CRYPTO_STATIC_TEST
)
	if (NOT ${_openssl_override_var} AND DEFINED ENV{${_openssl_override_var}})
		set(${_openssl_override_var} "$ENV{${_openssl_override_var}}" CACHE FILEPATH "")
	endif()
endforeach()

set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_MSVC_STATIC_RT TRUE)
find_package(OpenSSL REQUIRED)
# message(STATUS "OpenSSL libs: " ${OPENSSL_LIBRARIES})
# Seriously, why does it pick MD versions and not MT like requested?
# string(REGEX REPLACE "64MD" "64MT" INTERNAL_OPENSSL_LIBRARIES ${OPENSSL_LIBRARIES})
#message(STATUS "OpenSSL libs: " ${OPENSSL_CRYPTO_LIBRARY})
#cmake_path(GET ${OPENSSL_LIBRARIES} ROOT_PATH rootPath)
#get_filename_component(rootPath "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
#message(STATUS "OpenSSL root: " ${rootPath})


# Attempt to simulate scripts/make-gitrev.h and zfs_gitrev.h
include(GetGitRevisionDescription)
# Take this out, or all cherry-picks do full recompile.
# get_git_head_revision(GIT_REFSPEC GIT_HASH)
git_describe_working_tree(GIT_GITREV)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/zfs_gitrev.h.win" "${CMAKE_CURRENT_SOURCE_DIR}/include/zfs_gitrev.h" @ONLY)
#

function(use_clang)
# It seems "-include file" is "/FI file" on Windows
add_compile_options(
	"/FI${CMAKE_SOURCE_DIR}/include/os/windows/zfs/zfs_config.h"
	"/FI${CMAKE_SOURCE_DIR}/include/sys/simd_config.h"
)
set(CMAKE_C_COMPILER clang-cl.exe PARENT_SCOPE)
set(CMAKE_CXX_COMPILER clang-cl.exe PARENT_SCOPE)

if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x86")
		add_compile_options(-m32)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
		add_definitions(-D_LP64 -D__LP64__)
		add_compile_options(-m64)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64") # all except wdk_add_library
		add_compile_options(-target aarch64-pc-windows-msvc)
else()
	message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

add_compile_options(
	# These are all MS headers
	-Wno-nonportable-include-path
	-Wno-unknown-pragmas
	-Wno-ignored-pragma-intrinsic
	-Wno-pragma-pack
	-Wno-microsoft-enum-forward-reference
	-Wno-visibility
	-Wno-microsoft-anon-tag
	# -Wno-ignored-attributes
	-Wno-unused-command-line-argument
	-Wno-unused-local-typedef
	-Wno-int-to-void-pointer-cast
	# ZFS related
	-Wno-missing-field-initializers
	#-fms-extensions
	-Wno-unused-function
	-Wno-unused-label
	#Visual Studio 2022 update 2022114 started breaking builds with errors
	-Wno-int-conversion
	-Wunknown-pragmas
)

add_compile_options(/Zi /Od /Oy- /Zo /Ob0)
add_link_options(/DEBUG:FULL /INCREMENTAL /OPT:NOREF /OPT:NOICF)

endfunction()

string(CONCAT WDK_LINK_FLAGS
	"/DEBUG "
)
if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x86")
		list(APPEND WDK_COMPILE_DEFINITIONS	-D_KERNEL -D__KERNEL__ -D_LP32 -D__LP32__)
		list(APPEND WDK_COMPILE_FLAGS
			/Z7
			-m32
		)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
		list(APPEND WDK_COMPILE_DEFINITIONS	-D_KERNEL -D__KERNEL__ -D_LP64 -D__LP64__)
		list(APPEND WDK_COMPILE_FLAGS
			/Z7
			-m64
		)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64") # all except wdk_add_library
		list(APPEND WDK_COMPILE_DEFINITIONS	-D_KERNEL -D__KERNEL__ -DARM64 -D_ARM64_ -D__aarch64__ -D_M_ARM64 -D_LP64 -D__LP64__)
		list(APPEND WDK_COMPILE_FLAGS
			/Z7
			--target=arm64-windows-msvc
		)
else()
	message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Force-include zfs_config.h in all WDK kernel builds (mirrors /FI in use_clang for userland)
list(APPEND WDK_COMPILE_FLAGS "/FI${CMAKE_SOURCE_DIR}/include/os/windows/zfs/zfs_config.h")
list(APPEND WDK_COMPILE_FLAGS "/FI${CMAKE_SOURCE_DIR}/include/sys/simd_config.h")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDBG -DZFS_DEBUG)
endif()

# Avoid dependency add_compile_definitions vcruntime140.dll
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

# Let's try to emulate gitrev
#find_path(BASH_DIR NAMES bash.exe git-bash.exe PATHS "/c/Program Files (x86)/Git/")
#execute_process(COMMAND $BASH_DIR -c "scripts/make_gitrev.h include/zfs_gitrev.h" )


add_subdirectory(module)
#add_subdirectory(module/zfs)
#add_subdirectory(zfsinstaller)

add_subdirectory(lib)
add_subdirectory(cmd)

