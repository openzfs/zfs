
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

wdk_add_library(icpkern
  algs/aes/aes_impl_generic.c
  algs/aes/aes_impl.c
  algs/aes/aes_impl_x86-64.c
  algs/aes/aes_modes.c
  algs/edonr/edonr.c
  algs/modes/cbc.c
  algs/modes/ccm.c
  algs/modes/ctr.c
  algs/modes/ecb.c
  algs/modes/gcm.c
  algs/modes/gcm_generic.c
  algs/modes/modes.c
  algs/sha1/sha1.c
  algs/sha2/sha2.c
  algs/skein/skein.c
  algs/skein/skein_block.c
  algs/skein/skein_iv.c
  algs/edonr/edonr_byteorder.h
  algs/skein/skein_impl.h
  algs/skein/skein_port.h
  api/kcf_cipher.c
  api/kcf_ctxops.c
  api/kcf_digest.c
  api/kcf_mac.c
  api/kcf_miscapi.c
  asm-x86_64/aes/aeskey.c
  asm-x86_64/aes/aes_aesni.S
  asm-x86_64/aes/aes_amd64.S
  asm-x86_64/modes/aesni-gcm-x86_64.S
  asm-x86_64/modes/gcm_pclmulqdq.S
  asm-x86_64/modes/ghash-x86_64.S
  asm-x86_64/sha1/sha1-x86_64.S
  asm-x86_64/sha2/sha256_impl.S
  asm-x86_64/sha2/sha512_impl.S
  core/kcf_callprov.c
  core/kcf_mech_tabs.c
  core/kcf_prov_lib.c
  core/kcf_prov_tabs.c
  core/kcf_sched.c
  illumos-crypto.c
  io/aes.c
  io/sha1_mod.c
  io/sha2_mod.c
  io/edonr_mod.c
  io/skein_mod.c
  #os/bitmap_arch.c
  os/modconf.c
  os/modhash.c
  spi/kcf_spi.c
)

target_link_libraries(icpkern PRIVATE splkern)
target_include_directories(icpkern BEFORE PUBLIC "include")
