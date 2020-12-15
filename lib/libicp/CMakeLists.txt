
use_clang()

set(ICP_MODULE_DIR "../../module/icp")

add_library(libicp
    "${ICP_MODULE_DIR}/algs/aes/aes_impl_aesni.c"
    "${ICP_MODULE_DIR}/algs/aes/aes_impl_generic.c"
    "${ICP_MODULE_DIR}/algs/aes/aes_impl_x86-64.c"
    "${ICP_MODULE_DIR}/algs/aes/aes_impl.c"
    "${ICP_MODULE_DIR}/algs/aes/aes_modes.c"
    "${ICP_MODULE_DIR}/algs/edonr/edonr.c"
    "${ICP_MODULE_DIR}/algs/modes/cbc.c"
    "${ICP_MODULE_DIR}/algs/modes/ccm.c"
    "${ICP_MODULE_DIR}/algs/modes/ctr.c"
    "${ICP_MODULE_DIR}/algs/modes/ecb.c"
    "${ICP_MODULE_DIR}/algs/modes/gcm.c"
    "${ICP_MODULE_DIR}/algs/modes/gcm_generic.c"
    "${ICP_MODULE_DIR}/algs/modes/gcm_pclmulqdq.c"
    "${ICP_MODULE_DIR}/algs/modes/modes.c"
    "${ICP_MODULE_DIR}/algs/sha1/sha1.c"
    "${ICP_MODULE_DIR}/algs/sha2/sha2.c"
    "${ICP_MODULE_DIR}/algs/skein/skein.c"
    "${ICP_MODULE_DIR}/algs/skein/skein_block.c"
    "${ICP_MODULE_DIR}/algs/skein/skein_iv.c"
    "${ICP_MODULE_DIR}/api/kcf_cipher.c"
    "${ICP_MODULE_DIR}/api/kcf_ctxops.c"
    "${ICP_MODULE_DIR}/api/kcf_digest.c"
    "${ICP_MODULE_DIR}/api/kcf_mac.c"
    "${ICP_MODULE_DIR}/api/kcf_miscapi.c"
    "${ICP_MODULE_DIR}/core/kcf_callprov.c"
    "${ICP_MODULE_DIR}/core/kcf_mech_tabs.c"
    "${ICP_MODULE_DIR}/core/kcf_prov_lib.c"
    "${ICP_MODULE_DIR}/core/kcf_prov_tabs.c"
    "${ICP_MODULE_DIR}/core/kcf_sched.c"
    "${ICP_MODULE_DIR}/illumos-crypto.c"
    "${ICP_MODULE_DIR}/io/aes.c"
    "${ICP_MODULE_DIR}/io/edonr_mod.c"
    "${ICP_MODULE_DIR}/io/sha1_mod.c"
    "${ICP_MODULE_DIR}/io/sha2_mod.c"
    "${ICP_MODULE_DIR}/io/skein_mod.c"
    "${ICP_MODULE_DIR}/os/modconf.c"
    "${ICP_MODULE_DIR}/os/modhash.c"
    "${ICP_MODULE_DIR}/spi/kcf_spi.c"
)

# Add windows/assembler sources here too.


target_include_directories(libicp PRIVATE "${ICP_MODULE_DIR}/include")
target_link_libraries(libicp PUBLIC libspl libpthread)
