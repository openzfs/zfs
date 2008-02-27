#ifndef _LINUX_KSTAT_H
#define _LINUX_KSTAT_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux-types.h>

/* XXX - The minimum functionality here is stubbed out but nothing works. */

#define KSTAT_STRLEN    31      /* 30 chars + NULL; must be 16 * n - 1 */

#define KSTAT_TYPE_RAW          0       /* can be anything */
                                        /* ks_ndata >= 1 */
#define KSTAT_TYPE_NAMED        1       /* name/value pair */
                                        /* ks_ndata >= 1 */
#define KSTAT_TYPE_INTR         2       /* interrupt statistics */
                                        /* ks_ndata == 1 */
#define KSTAT_TYPE_IO           3       /* I/O statistics */
                                        /* ks_ndata == 1 */
#define KSTAT_TYPE_TIMER        4       /* event timer */
                                        /* ks_ndata >= 1 */

#define KSTAT_NUM_TYPES         5


#define KSTAT_DATA_CHAR         0
#define KSTAT_DATA_INT32        1
#define KSTAT_DATA_UINT32       2
#define KSTAT_DATA_INT64        3
#define KSTAT_DATA_UINT64       4


#define KSTAT_FLAG_VIRTUAL              0x01
#define KSTAT_FLAG_VAR_SIZE             0x02
#define KSTAT_FLAG_WRITABLE             0x04
#define KSTAT_FLAG_PERSISTENT           0x08
#define KSTAT_FLAG_DORMANT              0x10
#define KSTAT_FLAG_INVALID              0x2


typedef int     kid_t;          /* unique kstat id */

typedef struct kstat_s {
        /*
         * Fields relevant to both kernel and user
         */
        hrtime_t        ks_crtime;      /* creation time (from gethrtime()) */
        struct kstat_s  *ks_next;       /* kstat chain linkage */
        kid_t           ks_kid;         /* unique kstat ID */
        char            ks_module[KSTAT_STRLEN]; /* provider module name */
        uchar_t         ks_resv;        /* reserved, currently just padding */
        int             ks_instance;    /* provider module's instance */
        char            ks_name[KSTAT_STRLEN]; /* kstat name */
        uchar_t         ks_type;        /* kstat data type */
        char            ks_class[KSTAT_STRLEN]; /* kstat class */
        uchar_t         ks_flags;       /* kstat flags */
        void            *ks_data;       /* kstat type-specific data */
        uint_t          ks_ndata;       /* # of type-specific data records */
        size_t          ks_data_size;   /* total size of kstat data section */
        hrtime_t        ks_snaptime;    /* time of last data shapshot */
        /*
         * Fields relevant to kernel only
         */
        int             (*ks_update)(struct kstat *, int); /* dynamic update */
        void            *ks_private;    /* arbitrary provider-private data */
        int             (*ks_snapshot)(struct kstat *, void *, int);
        void            *ks_lock;       /* protects this kstat's data */
} kstat_t;

typedef struct kstat_named_s {
        char    name[KSTAT_STRLEN];     /* name of counter */
        uchar_t data_type;              /* data type */
        union {
                char            c[16];  /* enough for 128-bit ints */
                int32_t         i32;
                uint32_t        ui32;
                struct {
                        union {
                                char            *ptr;   /* NULL-term string */
                                char            __pad[8]; /* 64-bit padding */
                        } addr;
                        uint32_t        len;    /* # bytes for strlen + '\0' */
                } str;
/*
 * The int64_t and uint64_t types are not valid for a maximally conformant
 * 32-bit compilation environment (cc -Xc) using compilers prior to the
 * introduction of C99 conforming compiler (reference ISO/IEC 9899:1990).
 * In these cases, the visibility of i64 and ui64 is only permitted for
 * 64-bit compilation environments or 32-bit non-maximally conformant
 * C89 or C90 ANSI C compilation environments (cc -Xt and cc -Xa). In the
 * C99 ANSI C compilation environment, the long long type is supported.
 * The _INT64_TYPE is defined by the implementation (see sys/int_types.h).
 */
                int64_t         i64;
                uint64_t        ui64;
                long            l;
                ulong_t         ul;

                /* These structure members are obsolete */

                longlong_t      ll;
                u_longlong_t    ull;
                float           f;
                double          d;
        } value;                        /* value of counter */
} kstat_named_t;


static __inline__ kstat_t *
kstat_create(const char *ks_module, int ks_instance, const char *ks_name, 
             const char *ks_class, uchar_t ks_type, uint_t ks_ndata,
             uchar_t ks_flags)
{
	return NULL;
}

static __inline__ void
kstat_install(kstat_t *ksp)
{
	return;
}

static __inline__ void
kstat_delete(kstat_t *ksp)
{
	return;
}

#ifdef  __cplusplus
}
#endif

#endif  /* _LINUX_KSTAT_H */

