
use_clang()

add_library(libefi
	rdwr_efi_windows.c
)
target_link_libraries(libefi PRIVATE libuuid zlib)
