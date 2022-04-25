libicp_la_CCASFLAGS = $(AM_CCASFLAGS)
libicp_la_CFLAGS    = $(AM_CFLAGS) $(KERNEL_CFLAGS) $(LIBRARY_CFLAGS)

noinst_LTLIBRARIES += libicp.la

nodist_libicp_la_SOURCES = \
	module/icp/spi/kcf_spi.c \
	module/icp/api/kcf_ctxops.c \
	module/icp/api/kcf_cipher.c \
	module/icp/api/kcf_mac.c \
	module/icp/algs/aes/aes_impl_aesni.c \
	module/icp/algs/aes/aes_impl_generic.c \
	module/icp/algs/aes/aes_impl_x86-64.c \
	module/icp/algs/aes/aes_impl.c \
	module/icp/algs/aes/aes_modes.c \
	module/icp/algs/edonr/edonr.c \
	module/icp/algs/modes/modes.c \
	module/icp/algs/modes/cbc.c \
	module/icp/algs/modes/gcm_generic.c \
	module/icp/algs/modes/gcm_pclmulqdq.c \
	module/icp/algs/modes/gcm.c \
	module/icp/algs/modes/ctr.c \
	module/icp/algs/modes/ccm.c \
	module/icp/algs/modes/ecb.c \
	module/icp/algs/sha2/sha2.c \
	module/icp/algs/skein/skein.c \
	module/icp/algs/skein/skein_block.c \
	module/icp/algs/skein/skein_iv.c \
	module/icp/illumos-crypto.c \
	module/icp/io/aes.c \
	module/icp/io/sha2_mod.c \
	module/icp/io/skein_mod.c \
	module/icp/core/kcf_sched.c \
	module/icp/core/kcf_prov_lib.c \
	module/icp/core/kcf_callprov.c \
	module/icp/core/kcf_mech_tabs.c \
	module/icp/core/kcf_prov_tabs.c

if TARGET_CPU_X86_64
nodist_libicp_la_SOURCES += \
	module/icp/asm-x86_64/aes/aeskey.c
nodist_libicp_la_SOURCES += \
	module/icp/asm-x86_64/aes/aes_amd64.S \
	module/icp/asm-x86_64/aes/aes_aesni.S \
	module/icp/asm-x86_64/modes/gcm_pclmulqdq.S \
	module/icp/asm-x86_64/modes/aesni-gcm-x86_64.S \
	module/icp/asm-x86_64/modes/ghash-x86_64.S \
	module/icp/asm-x86_64/sha2/sha256_impl.S \
	module/icp/asm-x86_64/sha2/sha512_impl.S
endif
