
use_clang()

add_definitions(-DWITH_SSL -DINET6)

add_library(libfetch
	common.c
	fetch.c
#	ftp.c
	file.c
	http.c
)

target_include_directories(libfetch BEFORE PRIVATE "${OPENSSL_INCLUDE_DIR}")

if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x86")
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
	find_library(SSL_EAY_RELEASE
		NAMES
		  libssl_static
		NAMES_PER_DIR
		HINTS
		  "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MTd"
		PATH_SUFFIXES
		  lib
		REQUIRED
	  )
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
	find_library(SSL_EAY_RELEASE
		NAMES
		  libssl_static
		NAMES_PER_DIR
		HINTS
		  "C:/Program Files/OpenSSL-Win64-ARM/lib/VC/arm64/MTd"
		PATH_SUFFIXES
		  lib
		REQUIRED
	  )
endif()

target_link_libraries(libfetch PRIVATE Crypt32.lib ${SSL_EAY_RELEASE})
