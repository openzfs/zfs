
use_clang()

um_add_executable(zfs
	 zfs_iter.c
	 zfs_main.c
	 zfs_iter.h
	 zfs_util.h
	 zfs_project.c
)
target_link_libraries(zfs PRIVATE
	libnvpair
	libuutil
	libzfs
	libzfs_core
	libzpool
)
install(TARGETS zfs RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zfs>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)
