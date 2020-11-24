
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libpthread INTERFACE)
target_include_directories(libpthread INTERFACE "")
