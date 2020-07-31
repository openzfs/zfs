include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/module/icp \
	$(top_srcdir)/lib/libicp

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

noinst_LTLIBRARIES = libicp.la

if TARGET_CPU_X86_64
ASM_SOURCES_C = asm-x86_64/aes/aeskey.c
ASM_SOURCES_AS = \
	asm-x86_64/aes/aes_amd64.S \
	asm-x86_64/aes/aes_aesni.S \
	asm-x86_64/modes/gcm_pclmulqdq.S \
	asm-x86_64/modes/aesni-gcm-x86_64.S \
	asm-x86_64/modes/ghash-x86_64.S \
	asm-x86_64/sha1/sha1-x86_64.S \
	asm-x86_64/sha2/sha256_impl.S \
	asm-x86_64/sha2/sha512_impl.S
else
ASM_SOURCES_C =
ASM_SOURCES_AS =
endif

KERNEL_C = \
	spi/kcf_spi.c \
	api/kcf_ctxops.c \
	api/kcf_digest.c \
	api/kcf_cipher.c \
	api/kcf_miscapi.c \
	api/kcf_mac.c \
	algs/aes/aes_impl_aesni.c \
	algs/aes/aes_impl_generic.c \
	algs/aes/aes_impl_x86-64.c \
	algs/aes/aes_impl.c \
	algs/aes/aes_modes.c \
	algs/edonr/edonr.c \
	algs/modes/modes.c \
	algs/modes/cbc.c \
	algs/modes/gcm_generic.c \
	algs/modes/gcm_pclmulqdq.c \
	algs/modes/gcm.c \
	algs/modes/ctr.c \
	algs/modes/ccm.c \
	algs/modes/ecb.c \
	algs/sha1/sha1.c \
	algs/sha2/sha2.c \
	algs/skein/skein.c \
	algs/skein/skein_block.c \
	algs/skein/skein_iv.c \
	illumos-crypto.c \
	io/aes.c \
	io/edonr_mod.c \
	io/sha1_mod.c \
	io/sha2_mod.c \
	io/skein_mod.c \
	os/modhash.c \
	os/modconf.c \
	core/kcf_sched.c \
	core/kcf_prov_lib.c \
	core/kcf_callprov.c \
	core/kcf_mech_tabs.c \
	core/kcf_prov_tabs.c \
	$(ASM_SOURCES_C)

KERNEL_ASM = $(ASM_SOURCES_AS)

nodist_libicp_la_SOURCES = \
	$(KERNEL_C) \
	$(KERNEL_ASM)
