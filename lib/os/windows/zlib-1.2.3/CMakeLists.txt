
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

set(ZLIB_SOURCES
  adler32.c
  compress.c
  crc32.c
  deflate.c
  gzio.c
  infback.c
  inffast.c
  inflate.c
  inftrees.c
  trees.c
  uncompr.c
  zutil.c
  contrib/masmx64/inffas8664.c
  contrib/masmx86/gvmat32c.c
  contrib/minizip/ioapi.c
  contrib/minizip/unzip.c
  contrib/minizip/zip.c
)

wdk_add_library(zlibkern ${ZLIB_SOURCES})
target_include_directories(zlibkern PUBLIC "")
target_link_libraries(zlibkern PRIVATE splkern)
target_compile_definitions(zlibkern PUBLIC Z_PREFIX MY_ZCALLOC _KERNEL)

add_library(zlib ${ZLIB_SOURCES})
target_include_directories(zlib PUBLIC "")
