
use_clang()

um_add_executable(raidz_test
	raidz_test.c
	raidz_test.h
	raidz_bench.c
	os/windows/resource.rc
)
# https://github.com/openzfsonwindows/openzfs/issues/31
target_compile_definitions(raidz_test PRIVATE ZFS_DEBUG)

target_link_libraries(raidz_test PRIVATE
	libzfs
	libzfs_core
	libzpool
)
install(TARGETS raidz_test RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:raidz_test>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)
