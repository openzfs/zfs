
use_clang()

um_add_executable(zfsinstaller
	zfsinstaller.cpp
	zfsinstaller.h
	resource.rc
)
target_compile_definitions(zfsinstaller PRIVATE UNICODE _UNICODE)
target_link_libraries(zfsinstaller setupapi libspl)
install(TARGETS zfsinstaller RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/driver")
install(FILES $<TARGET_PDB_FILE:zfsinstaller>
	DESTINATION "${CMAKE_INSTALL_BINDIR}/driver"
	OPTIONAL
)
