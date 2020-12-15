
use_clang()

add_library(libtpool
    thread_pool.c
)
target_include_directories(libspl BEFORE PUBLIC include)
target_link_libraries(libspl PRIVATE libpthread advapi32 shell32)
