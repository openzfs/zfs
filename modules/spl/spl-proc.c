#include <linux/proc_fs.h>
#include <linux/kmod.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include "config.h"

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_PROC

static struct ctl_table_header *spl_header = NULL;
static unsigned long table_min = 0;
static unsigned long table_max = ~0;

#define CTL_SPL 0x87
enum {
        CTL_DEBUG_SUBSYS = 1,     /* Debug subsystem */
        CTL_DEBUG_MASK,           /* Debug mask */
        CTL_DEBUG_PRINTK,         /* Force all messages to console */
        CTL_DEBUG_MB,             /* Debug buffer size */
        CTL_DEBUG_BINARY,         /* Include binary data in buffer */
        CTL_DEBUG_CATASTROPHE,    /* Set if we have BUG'd or panic'd */
        CTL_DEBUG_PANIC_ON_BUG,   /* Set if we should panic on BUG */
        CTL_DEBUG_PATH,           /* Dump log location */
        CTL_DEBUG_DUMP,           /* Dump debug buffer to file */
        CTL_DEBUG_FORCE_BUG,      /* Hook to force a BUG */
        CTL_CONSOLE_RATELIMIT,    /* Ratelimit console messages */
        CTL_CONSOLE_MAX_DELAY_CS, /* Max delay at which we skip messages */
        CTL_CONSOLE_MIN_DELAY_CS, /* Init delay at which we skip messages */
        CTL_CONSOLE_BACKOFF,      /* Delay increase factor */
        CTL_STACK_SIZE,           /* Max observed stack size */
#ifdef DEBUG_KMEM
        CTL_KMEM_KMEMUSED,        /* Crrently alloc'd kmem bytes */
        CTL_KMEM_KMEMMAX,         /* Max alloc'd by kmem bytes */
        CTL_KMEM_VMEMUSED,        /* Currently alloc'd vmem bytes */
        CTL_KMEM_VMEMMAX,         /* Max alloc'd by vmem bytes */
#endif
	CTL_HOSTID,               /* Host id reported by /usr/bin/hostid */
	CTL_HW_SERIAL,            /* Hardware serial number from hostid */
};

static int
proc_copyin_string(char *kbuffer, int kbuffer_size,
                   const char *ubuffer, int ubuffer_size)
{
        int size;

        if (ubuffer_size > kbuffer_size)
                return -EOVERFLOW;

        if (copy_from_user((void *)kbuffer, (void *)ubuffer, ubuffer_size))
                return -EFAULT;

        /* strip trailing whitespace */
        size = strnlen(kbuffer, ubuffer_size);
        while (size-- >= 0)
                if (!isspace(kbuffer[size]))
                        break;

        /* empty string */
        if (size < 0)
                return -EINVAL;

        /* no space to terminate */
        if (size == kbuffer_size)
                return -EOVERFLOW;

        kbuffer[size + 1] = 0;
        return 0;
}

static int
proc_copyout_string(char *ubuffer, int ubuffer_size,
                    const char *kbuffer, char *append)
{
        /* NB if 'append' != NULL, it's a single character to append to the
         * copied out string - usually "\n", for /proc entries and
         * (i.e. a terminating zero byte) for sysctl entries
         */
        int size = MIN(strlen(kbuffer), ubuffer_size);

        if (copy_to_user(ubuffer, kbuffer, size))
                return -EFAULT;

        if (append != NULL && size < ubuffer_size) {
                if (copy_to_user(ubuffer + size, append, 1))
                        return -EFAULT;

                size++;
        }

        return size;
}

static int
proc_dobitmasks(struct ctl_table *table, int write, struct file *filp,
                void __user *buffer, size_t *lenp, loff_t *ppos)
{
        unsigned long *mask = table->data;
        int is_subsys = (mask == &spl_debug_subsys) ? 1 : 0;
        int is_printk = (mask == &spl_debug_printk) ? 1 : 0;
        int size = 512, rc;
        char *str;
        ENTRY;

        str = kmem_alloc(size, KM_SLEEP);
        if (str == NULL)
                RETURN(-ENOMEM);

        if (write) {
                rc = proc_copyin_string(str, size, buffer, *lenp);
                if (rc < 0)
                        RETURN(rc);

                rc = spl_debug_str2mask(mask, str, is_subsys);
                /* Always print BUG/ASSERT to console, so keep this mask */
                if (is_printk)
                        *mask |= D_EMERG;

                *ppos += *lenp;
        } else {
                rc = spl_debug_mask2str(str, size, *mask, is_subsys);
                if (*ppos >= rc)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer, *lenp,
                                                 str + *ppos, "\n");
                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        kmem_free(str, size);
        RETURN(rc);
}

static int
proc_debug_mb(struct ctl_table *table, int write, struct file *filp,
              void __user *buffer, size_t *lenp, loff_t *ppos)
{
        char str[32];
        int rc, len;
        ENTRY;

        if (write) {
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        RETURN(rc);

                rc = spl_debug_set_mb(simple_strtoul(str, NULL, 0));
                *ppos += *lenp;
        } else {
                len = snprintf(str, sizeof(str), "%d", spl_debug_get_mb());
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer, *lenp, str + *ppos, "\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        RETURN(rc);
}

static int
proc_dump_kernel(struct ctl_table *table, int write, struct file *filp,
                 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	ENTRY;

        if (write) {
               spl_debug_dumplog();
                *ppos += *lenp;
        } else {
                *lenp = 0;
        }

        RETURN(0);
}

static int
proc_force_bug(struct ctl_table *table, int write, struct file *filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
{
	ENTRY;

        if (write) {
               CERROR("Crashing due to forced SBUG\n");
               SBUG();
	       /* Unreachable */
        } else {
                *lenp = 0;
	}

	RETURN(0);
}

static int
proc_console_max_delay_cs(struct ctl_table *table, int write, struct file *filp,
                          void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc, max_delay_cs;
        struct ctl_table dummy = *table;
        long d;
	ENTRY;

        dummy.data = &max_delay_cs;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                max_delay_cs = 0;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
                if (rc < 0)
                        RETURN(rc);

                if (max_delay_cs <= 0)
                        RETURN(-EINVAL);

                d = (max_delay_cs * HZ) / 100;
                if (d == 0 || d < spl_console_min_delay)
                        RETURN(-EINVAL);

                spl_console_max_delay = d;
        } else {
                max_delay_cs = (spl_console_max_delay * 100) / HZ;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
        }

        RETURN(rc);
}

static int
proc_console_min_delay_cs(struct ctl_table *table, int write, struct file *filp,
                          void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc, min_delay_cs;
        struct ctl_table dummy = *table;
        long d;
	ENTRY;

        dummy.data = &min_delay_cs;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                min_delay_cs = 0;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
                if (rc < 0)
                        RETURN(rc);

                if (min_delay_cs <= 0)
                        RETURN(-EINVAL);

                d = (min_delay_cs * HZ) / 100;
                if (d == 0 || d > spl_console_max_delay)
                        RETURN(-EINVAL);

                spl_console_min_delay = d;
        } else {
                min_delay_cs = (spl_console_min_delay * 100) / HZ;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
        }

        RETURN(rc);
}

static int
proc_console_backoff(struct ctl_table *table, int write, struct file *filp,
                     void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc, backoff;
        struct ctl_table dummy = *table;
	ENTRY;

        dummy.data = &backoff;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                backoff = 0;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
                if (rc < 0)
                        RETURN(rc);

                if (backoff <= 0)
                        RETURN(-EINVAL);

                spl_console_backoff = backoff;
        } else {
                backoff = spl_console_backoff;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
        }

        RETURN(rc);
}

static int
proc_doatomic64(struct ctl_table *table, int write, struct file *filp,
                void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc = 0;
        unsigned long min = 0, max = ~0, val;
        struct ctl_table dummy = *table;
	ENTRY;

        dummy.data = &val;
        dummy.proc_handler = &proc_dointvec;
        dummy.extra1 = &min;
        dummy.extra2 = &max;

        if (write) {
                *ppos += *lenp;
        } else {
                val = atomic_read((atomic64_t *)table->data);
                rc = proc_doulongvec_minmax(&dummy, write, filp,
                                            buffer, lenp, ppos);
        }

        RETURN(rc);
}

static int
proc_dohostid(struct ctl_table *table, int write, struct file *filp,
              void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int len, rc = 0;
        unsigned long val;
        char *end, str[32];
	ENTRY;

        if (write) {
		/* We can't use proc_doulongvec_minmax() in the write
		 * case hear because hostid while a hex value has no
		 * leading 0x which confuses the helper function. */
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        RETURN(rc);

                val = simple_strtoul(str, &end, 16);
		if (str == end)
			RETURN(-EINVAL);

		spl_hostid = val;
                sprintf(spl_hw_serial, "%lu", ((long)val >= 0) ? val : -val);
                *ppos += *lenp;
        } else {
                len = snprintf(str, sizeof(str), "%lx", spl_hostid);
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer, *lenp, str + *ppos, "\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        RETURN(rc);
}

static struct ctl_table spl_table[] = {
        /* NB No .strategy entries have been provided since
         * sysctl(8) prefers to go via /proc for portability.
         */
        {
                .ctl_name = CTL_DEBUG_SUBSYS,
                .procname = "debug_subsystem",
                .data     = &spl_debug_subsys,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                .ctl_name = CTL_DEBUG_MASK,
                .procname = "debug_mask",
                .data     = &spl_debug_mask,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                .ctl_name = CTL_DEBUG_PRINTK,
                .procname = "debug_printk",
                .data     = &spl_debug_printk,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                .ctl_name = CTL_DEBUG_MB,
                .procname = "debug_mb",
                .mode     = 0644,
                .proc_handler = &proc_debug_mb,
        },
        {
                .ctl_name = CTL_DEBUG_BINARY,
                .procname = "debug_binary",
                .data     = &spl_debug_binary,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_DEBUG_CATASTROPHE,
                .procname = "catastrophe",
                .data     = &spl_debug_catastrophe,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_DEBUG_PANIC_ON_BUG,
                .procname = "panic_on_bug",
                .data     = &spl_debug_panic_on_bug,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = CTL_DEBUG_PATH,
                .procname = "debug_path",
                .data     = spl_debug_file_path,
                .maxlen   = sizeof(spl_debug_file_path),
                .mode     = 0644,
                .proc_handler = &proc_dostring,
        },
        {
                .ctl_name = CTL_DEBUG_DUMP,
                .procname = "debug_dump",
                .mode     = 0200,
                .proc_handler = &proc_dump_kernel,
        },
        {       .ctl_name = CTL_DEBUG_FORCE_BUG,
                .procname = "force_bug",
                .mode     = 0200,
                .proc_handler = &proc_force_bug,
        },
        {
                .ctl_name = CTL_CONSOLE_RATELIMIT,
                .procname = "console_ratelimit",
                .data     = &spl_console_ratelimit,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_CONSOLE_MAX_DELAY_CS,
                .procname = "console_max_delay_centisecs",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_max_delay_cs,
        },
        {
                .ctl_name = CTL_CONSOLE_MIN_DELAY_CS,
                .procname = "console_min_delay_centisecs",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_min_delay_cs,
        },
        {
                .ctl_name = CTL_CONSOLE_BACKOFF,
                .procname = "console_backoff",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_backoff,
        },
        {
                .ctl_name = CTL_STACK_SIZE,
                .procname = "stack_max",
                .data     = &spl_debug_stack,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
#ifdef DEBUG_KMEM
        {
                .ctl_name = CTL_KMEM_KMEMUSED,
                .procname = "kmem_used",
                .data     = &kmem_alloc_used,
                .maxlen   = sizeof(atomic64_t),
                .mode     = 0444,
                .proc_handler = &proc_doatomic64,
        },
        {
                .ctl_name = CTL_KMEM_KMEMMAX,
                .procname = "kmem_max",
                .data     = &kmem_alloc_max,
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doulongvec_minmax,
        },
        {
                .ctl_name = CTL_KMEM_VMEMUSED,
                .procname = "vmem_used",
                .data     = &vmem_alloc_used,
                .maxlen   = sizeof(atomic64_t),
                .mode     = 0444,
                .proc_handler = &proc_doatomic64,
        },
        {
                .ctl_name = CTL_KMEM_VMEMMAX,
                .procname = "vmem_max",
                .data     = &vmem_alloc_max,
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doulongvec_minmax,
        },
#endif
        {
                .ctl_name = CTL_HOSTID,
                .procname = "hostid",
                .data     = &spl_hostid,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dohostid,
        },
        {
                .ctl_name = CTL_HW_SERIAL,
                .procname = "hw_serial",
                .data     = spl_hw_serial,
                .maxlen   = sizeof(spl_hw_serial),
                .mode     = 0444,
                .proc_handler = &proc_dostring,
        },
        { 0 },
};

static struct ctl_table spl_dir_table[] = {
        {
                .ctl_name = CTL_SPL,
                .procname = "spl",
                .mode     = 0555,
                .child    = spl_table,
        },
        {0}
};

int
proc_init(void)
{
        ENTRY;

#ifdef CONFIG_SYSCTL
        spl_header = register_sysctl_table(spl_dir_table, 0);
	if (spl_header == NULL)
		RETURN(-EUNATCH);
#endif
        RETURN(0);
}

void
proc_fini(void)
{
        ENTRY;

#ifdef CONFIG_SYSCTL
        ASSERT(spl_header != NULL);
        unregister_sysctl_table(spl_header);
#endif
        EXIT;
}
