use_clang()

wdk_add_driver(OpenZVOL
  openzvol.c
  zfs_windows_zvol.c
  zfs_windows_zvol_scsi.c
  zfs_windows_zvol_wmi.c
  ../../../avl/avl.c
  ../debug.c
  # OpenZFS_perf.rc
  # resource.rc
)

target_link_libraries(OpenZVOL
	splkern
#	zlibkern
#	icpkern
#	luakern
#	zfskern
#	zfskern_os
#	zcommonkern
#	nvpairkern
#	unicodekern
	# WDK libraries
	WDK::WDMSEC
	WDK::STORPORT
	WDK::SCSIWMI
	WDK::FLTMGR
)

# set(CMAKE_LINKER link.exe)

set_target_properties(OpenZVOL
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
)

configure_file(OpenZVOL.inf "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZVOL.inf" COPYONLY)

set(OPENZVOL_CAT_SIGN_COMMANDS "")
if (OPENZFS_SIGNTOOL_SHA1)
	list(APPEND OPENZVOL_CAT_SIGN_COMMANDS
		COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256 /sha1 "${OPENZFS_SIGNTOOL_SHA1}" /tr "${OPENZFS_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZVOL.cat"
		COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /td sha256 /sha1 "${OPENZFS_SIGNTOOL_SHA1}" /tr "${OPENZFS_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZVOL>
	)
endif()

add_custom_command(
	TARGET OpenZVOL
	POST_BUILD
	BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZVOL.cat"
    COMMAND "${STAMPINF_PROGRAM}" -d "*" -v "*" -f "${CMAKE_CURRENT_BINARY_DIR}\\driver\\OpenZVOL.inf"
	COMMAND "${INF2CAT_PROGRAM}" /verbose "/driver:${CMAKE_CURRENT_BINARY_DIR}/driver" /os:10_19H1_X86,10_19H1_X64,10_19H1_ARM64,ServerRS5_X64 /uselocaltime
	${OPENZVOL_CAT_SIGN_COMMANDS}
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
	VERBATIM COMMENT "Generating and signing OpenZVOL.cat file"
)

#add_dependencies(OpenZFS zed_cat)
	
install(TARGETS OpenZVOL RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/driver")
install(FILES 
	"${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZVOL.inf"
	"${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZVOL.cat"
#    "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.man"
	DESTINATION "${CMAKE_INSTALL_BINDIR}/../driver"
)

install(FILES
	$<TARGET_PDB_FILE:OpenZVOL>
	DESTINATION "${CMAKE_INSTALL_BINDIR}/../driver"
	OPTIONAL
)

#execute_process(COMMAND  "${WDK_ROOT}/bin/${WDK_VERSION}/x86/ctrpp.exe"
#  -o ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.h
#  -rc ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.rc
#  -ch ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_counters.h
# ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS.man )

file(GLOB CSOURCES CONFIGURE_DEPENDS "*.c")
# /c/Program Files (x86)/Windows Kits/10/Include/wdf/kmdf/1.9/wdf.h
# // "${WDK_ROOT}/bin/${WDK_VERSION}
# set(SCAN "${WDK_ROOT}/Include/wdf/kmdf/1.9/WdfTraceEnums.h")
# target_include_directories(OpenZVOL PUBLIC "${WDK_ROOT}/Include/wdf/kmdf/1.9/" )

 file(GLOB KMDF_INCLUDE_DIRS
	"${WDK_ROOT}/Include/wdf/kmdf/*"
)

if (NOT KMDF_INCLUDE_DIRS)
	message(FATAL_ERROR "Failed to find KMDF include directory under ${WDK_ROOT}/Include/wdf/kmdf")
endif()

list(SORT KMDF_INCLUDE_DIRS)
list(REVERSE KMDF_INCLUDE_DIRS)
list(GET KMDF_INCLUDE_DIRS 0 KMDF_INCLUDE_DIR)

message(STATUS "KMDF include dir: ${KMDF_INCLUDE_DIR}")

target_include_directories(OpenZVOL PUBLIC "${KMDF_INCLUDE_DIR}")
