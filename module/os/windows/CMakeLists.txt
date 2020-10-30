
add_subdirectory(spl)
add_subdirectory(zfs)

wdk_add_driver(OpenZFS
  debug.c
  driver.c
  Wpp.c
)

target_link_libraries(OpenZFS PRIVATE
	splkern
	zlibkern
	icpkern
	luakern
	zfskern
	zfskern_os
	zcommonkern
	nvpairkern
	unicodekern
	# WDK libraries
	WDK::WDMSEC
	WDK::STORPORT
	WDK::SCSIWMI
)

set_target_properties(OpenZFS
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
)

configure_file(OpenZFS.inf "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.inf" COPYONLY)

add_custom_command(
	TARGET OpenZFS
	POST_BUILD
	BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	COMMAND "${INF2CAT_PROGRAM}" /verbose "/driver:${CMAKE_CURRENT_BINARY_DIR}/driver" /os:10_19H1_X86,10_19H1_X64,ServerRS5_X64
	COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /t "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /tr "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /t "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZFS>
	COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /tr "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:OpenZFS>
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
	VERBATIM COMMENT "Generating and signing .cat file"
)
	
install(TARGETS OpenZFS RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/driver")
install(FILES 
	"${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.inf"
	"${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.cat"
	DESTINATION "${CMAKE_INSTALL_BINDIR}/driver"
)

install(FILES
	$<TARGET_PDB_FILE:OpenZFS>
	DESTINATION "${CMAKE_INSTALL_BINDIR}/driver"
	OPTIONAL
)
