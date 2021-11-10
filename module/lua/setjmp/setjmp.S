#if defined(__x86_64__)
#include "setjmp_x86_64.S"
#elif defined(__i386__)
#include "setjmp_i386.S"
#elif defined(__aarch64__)
#include "setjmp_aarch64.S"
#elif defined(__arm__)
#include "setjmp_arm.S"
#elif defined(__sparc__) && defined(__arch64__)
#include "setjmp_sparc64.S"
#elif defined(__powerpc__)
#include "setjmp_ppc.S"
#elif defined(__mips__)
#include "setjmp_mips.S"
#elif defined(__s390x__)
#include "setjmp_s390x.S"
#elif defined(__riscv)
#include "setjmp_rv64g.S"
#endif
