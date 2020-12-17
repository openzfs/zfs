
use_clang()

um_add_executable(zdb
	zdb.c
	zdb.h
	zdb_il.c
)
target_link_libraries(zdb PRIVATE
	libnvpair
	libuutil
	libzfs
	libzfs_core
	libzpool
)
install(TARGETS zdb RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zdb>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)
