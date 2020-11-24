
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libtpool
    thread_pool.c
)
target_include_directories(libspl BEFORE PUBLIC include)
target_link_libraries(libspl PRIVATE libpthread advapi32 shell32)
