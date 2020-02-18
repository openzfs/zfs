/* aes-with-movbe x86_64 GCM implementation */

#if defined(__x86_64__) && defined(HAVE_AVX) && \
    defined(HAVE_AES) && defined(HAVE_PCLMULQDQ) && \
    defined(HAVE_MOVBE)
    
# define MOVQ_AND_BYTESWAP(SRC,DEST) \
    movbeq SRC,DEST
# define ZFS_ENTRY_SYMBOL_RENAME(NAME) NAME##_movbe
# include "asm-x86_64/modes/aesni-gcm-x86_64.S.inc"

#endif /* defined(__x86_64__) ... */
