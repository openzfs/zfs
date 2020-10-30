cmake_minimum_required(VERSION 3.13)
project(OpenZFS)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/contrib/windows/cmake")
find_package(WDK REQUIRED)
option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)


add_subdirectory(module)
#add_subdirectory(module/zfs)
#add_subdirectory(zfsinstaller)


# Temporarily
add_subdirectory(lib/os/windows)

