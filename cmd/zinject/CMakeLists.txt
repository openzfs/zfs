
use_clang()

um_add_executable(zinject
	zinject.c
	translate.c
	os/windows/resource.rc
)

target_link_libraries(zinject PRIVATE
	libnvpair
	libuuid
	libzfs
	libzfs_core
	libzpool
)
install(TARGETS zinject RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:zinject>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)
