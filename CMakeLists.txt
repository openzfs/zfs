cmake_minimum_required(VERSION 3.13)
project(OpenZFS)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/contrib/windows/cmake")
find_package(WDK REQUIRED)

set(OPENSSL_USE_STATIC_LIBS TRUE)
variable_watch(OPENSSL_CRYPTO_STATIC)
find_package(OpenSSL REQUIRED)

option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)

function(use_clang)
# It seems "-include file" is "/FI file" on Windows
add_definitions("/FI ${CMAKE_SOURCE_DIR}/include/os/windows/zfs/zfs_config.h")
set(CMAKE_C_COMPILER clang-cl.exe PARENT_SCOPE)
set(CMAKE_CXX_COMPILER clang-cl.exe PARENT_SCOPE)
add_compile_options(
	# These are all MS headers
	-Wno-nonportable-include-path
	-Wno-unknown-pragmas
	-Wno-ignored-pragma-intrinsic
	-Wno-pragma-pack
	-Wno-microsoft-enum-forward-reference
	-Wno-visibility
	-Wno-microsoft-anon-tag
	-Wno-ignored-attributes
	-Wno-unused-command-line-argument
	-Wno-unused-local-typedef
	-Wno-int-to-void-pointer-cast
	# ZFS related
	#-Wno-misleading-indentation
	-Wno-dangling-else
	-Wno-missing-field-initializers
	#-fms-extensions
	-Wunused-function
)
endfunction()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_definitions(-D__x86_32__ -D__i386 -D__i386__ -D_LP32 -DWIN32)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions(-D__x86_64__ -D_LP64 -D__x86_64 -D_AMD64_ -D_WIN64 -DAMD64 -DWIN64)
else()
    message(FATAL_ERROR "Unsupported architecture")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDBG -DZFS_DEBUG)
endif()


add_subdirectory(module)
#add_subdirectory(module/zfs)
#add_subdirectory(zfsinstaller)

add_subdirectory(lib)
add_subdirectory(cmd)

