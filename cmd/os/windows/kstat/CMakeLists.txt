
use_clang()

um_add_executable(kstat kstat.c)

include_directories(PRIVATE "${CMAKE_SOURCE_DIR}/lib/os/windows")

target_link_libraries(kstat PRIVATE libkstat)
install(TARGETS kstat RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES $<TARGET_PDB_FILE:kstat>
	DESTINATION "${CMAKE_INSTALL_BINDIR}"
	OPTIONAL
)
