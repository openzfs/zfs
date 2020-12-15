
use_clang()

add_library(libuutil
	uu_alloc.c
	uu_avl.c
	uu_dprintf.c
	uu_ident.c
	uu_list.c
	uu_misc.c
	uu_open.c
	uu_pname.c
	uu_string.c
)
target_link_libraries(libuutil PUBLIC libspl libpthread)
