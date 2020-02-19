/* aes-with-movbe x86_64 GCM implementation */

#if defined(__x86_64__) && defined(HAVE_AVX) && \
    defined(HAVE_AES) && defined(HAVE_PCLMULQDQ) && \
    defined(HAVE_MOVBE)
    
/* defined in two parts because it NEEDS to expand into two separate lines */
# define MOVQ_AND_BYTESWAP_PART1(SRC,DEST) \
    movbeq SRC,DEST
# define MOVQ_AND_BYTESWAP_PART2(SRC,DEST) \
    /* byte-swap is part of movbeq already */
# define ZFS_ENTRY_SYMBOL_RENAME(NAME) NAME##_movbe
# include "aesni-gcm-x86_64.inc.S"

#endif /* defined(__x86_64__) ... */
