
use_clang()

add_library(libzfs_core
	libzfs_core.c
)
target_link_libraries(libzfs_core PUBLIC libnvpair libpthread libzutil libnvpair)
