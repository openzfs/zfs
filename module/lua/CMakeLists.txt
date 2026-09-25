 
use_clang()

wdk_add_library(luakern
  lapi.c
  lauxlib.c
  lbaselib.c
  #lbitlib.c
  lcode.c
  lcompat.c
  lcorolib.c
  lctype.c
  ldebug.c
  ldo.c
  #ldump.c
  lfunc.c
  lgc.c
  llex.c
  lmem.c
  lobject.c
  lopcodes.c
  lparser.c
  lstate.c
  lstring.c
  lstrlib.c
  ltable.c
  ltablib.c
  ltm.c
  #lundump.c
  lvm.c
  lzio.c

  lapi.h
  lcode.h
  lctype.h
  ldebug.h
  ldo.h
  lfunc.h
  lgc.h
  llex.h
  llimits.h
  lmem.h
  lobject.h
  lopcodes.h
  lparser.h
  lstate.h
  lstring.h
  ltable.h
  ltm.h
  #lundump.h
  lvm.h
  lzio.h
)

if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
	target_sources(luakern PRIVATE setjmp/setjmp_x86_64.S)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
	target_sources(luakern PRIVATE setjmp/setjmp_aarch64.S)
endif()

target_link_libraries(luakern PRIVATE splkern)
