cmake_minimum_required(VERSION 3.13)
project(OpenZFS)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/contrib/windows/cmake")
find_package(WDK REQUIRED)

option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)

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
git_describe_working_tree(GIT_GITREV)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/zfs_gitrev.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/include/zfs_gitrev.h" @ONLY)
#

function(use_clang)
# It seems "-include file" is "/FI file" on Windows
add_definitions("/FI ${CMAKE_SOURCE_DIR}/include/os/windows/zfs/zfs_config.h")
set(CMAKE_C_COMPILER clang-cl.exe PARENT_SCOPE)
set(CMAKE_CXX_COMPILER clang-cl.exe PARENT_SCOPE)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	add_compile_options(-m32)
else()
	add_compile_options(-m64)
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
	-Wno-ignored-attributes
	-Wno-unused-command-line-argument
	-Wno-unused-local-typedef
	-Wno-int-to-void-pointer-cast
	# ZFS related
	-Wno-misleading-indentation
	-Wno-dangling-else
	-Wno-missing-field-initializers
	#-fms-extensions
	-Wno-unused-function
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

# Avoid dependency add_compile_definitions vcruntime140.dll
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
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

