#include <sys/sysmacros.h>
#include <sys/vmsystm.h>
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <linux/proc_fs.h>
#include <linux/kmod.h>
#include "config.h"

/*
 * Generic support
 */
static char spl_debug_buffer[MAXMSGLEN];
static spinlock_t spl_debug_lock = SPIN_LOCK_UNLOCKED;

unsigned long spl_debug_mask = 0;
unsigned long spl_debug_subsys = 0xff;
unsigned long spl_hostid = 0;
char hw_serial[11] = "<none>";

EXPORT_SYMBOL(spl_debug_mask);
EXPORT_SYMBOL(spl_debug_subsys);
EXPORT_SYMBOL(spl_hostid);
EXPORT_SYMBOL(hw_serial);

static struct proc_dir_entry *spl_proc_root = NULL;
static struct proc_dir_entry *spl_proc_debug_mask = NULL;
static struct proc_dir_entry *spl_proc_debug_subsys = NULL;
static struct proc_dir_entry *spl_proc_hostid = NULL;
static struct proc_dir_entry *spl_proc_hw_serial = NULL;

int p0 = 0;
EXPORT_SYMBOL(p0);

vmem_t *zio_alloc_arena = NULL;
EXPORT_SYMBOL(zio_alloc_arena);


int
highbit(unsigned long i)
{
        register int h = 1;

        if (i == 0)
                return (0);
#if BITS_PER_LONG == 64
        if (i & 0xffffffff00000000ul) {
                h += 32; i >>= 32;
        }
#endif
        if (i & 0xffff0000) {
                h += 16; i >>= 16;
        }
        if (i & 0xff00) {
                h += 8; i >>= 8;
        }
        if (i & 0xf0) {
                h += 4; i >>= 4;
        }
        if (i & 0xc) {
                h += 2; i >>= 2;
        }
        if (i & 0x2) {
                h += 1;
        }
        return (h);
}
EXPORT_SYMBOL(highbit);

int
ddi_strtoul(const char *str, char **nptr, int base, unsigned long *result)
{
        char *end;
        return (*result = simple_strtoul(str, &end, base));
}
EXPORT_SYMBOL(ddi_strtoul);

/* XXX: Not the most efficient debug function ever.  This should be re-done
 * as an internal per-cpu in-memory debug log accessable via /proc/.  Not as
 * a shared global buffer everything gets serialize though.  That said I'll
 * worry about performance considerations once I've dealt with correctness.
 */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
        char *sfp, *start, *ptr;
        struct timeval tv;
	unsigned long flags;
        va_list ap;

	start = ptr = spl_debug_buffer;
        sfp = strrchr(file, '/');
        do_gettimeofday(&tv);

	/* XXX: This is particularly bad for performance, but we need to
	 * disable irqs here or two __dprintf()'s may deadlock on each
	 * other if one if called from an irq handler.  This is yet another
	 * reason why we really, really, need an internal debug log.
	 */
	spin_lock_irqsave(&spl_debug_lock, flags);
        ptr += snprintf(ptr, MAXMSGLEN - 1,
                        "spl: %lu.%06lu:%d:%u:%s:%d:%s(): ",
                        tv.tv_sec, tv.tv_usec, current->pid,
                        smp_processor_id(),
                        sfp == NULL ? file : sfp + 1,
                        line, func);

        va_start(ap, fmt);
        ptr += vsnprintf(ptr, MAXMSGLEN - (ptr - start) - 1, fmt, ap);
        va_end(ap);

        printk("%s", start);
	spin_unlock_irqrestore(&spl_debug_lock, flags);
}
EXPORT_SYMBOL(__dprintf);

static int
spl_proc_rd_generic_ul(char *page, char **start, off_t off,
		       int count, int *eof, unsigned long val)
{
        *start = page;
        *eof = 1;

	if (off || count > PAGE_SIZE)
		return 0;

	return snprintf(page, PAGE_SIZE, "0x%lx\n", val & 0xffffffff);
}

static int
spl_proc_rd_debug_mask(char *page, char **start, off_t off,
                       int count, int *eof, void *data)
{
	int rc;

	spin_lock(&spl_debug_lock);
	rc = spl_proc_rd_generic_ul(page, start, off, count,
	                            eof, spl_debug_mask);
	spin_unlock(&spl_debug_lock);

	return rc;
}

static int
spl_proc_rd_debug_subsys(char *page, char **start, off_t off,
                         int count, int *eof, void *data)
{
	int rc;

	spin_lock(&spl_debug_lock);
	rc = spl_proc_rd_generic_ul(page, start, off, count,
		                    eof, spl_debug_subsys);
	spin_unlock(&spl_debug_lock);

	return rc;
}

static int
spl_proc_rd_hostid(char *page, char **start, off_t off,
                   int count, int *eof, void *data)
{
        *start = page;
        *eof = 1;

	if (off || count > PAGE_SIZE)
		return 0;

	return snprintf(page, PAGE_SIZE, "%lx\n", spl_hostid & 0xffffffff);
}

static int
spl_proc_rd_hw_serial(char *page, char **start, off_t off,
                      int count, int *eof, void *data)
{
        *start = page;
        *eof = 1;

	if (off || count > PAGE_SIZE)
		return 0;

	strncpy(page, hw_serial, 11);
	return strlen(page);
}

static int
spl_proc_wr_generic_ul(const char *ubuf, unsigned long count,
                       unsigned long *val, int base)
{
	char *end, kbuf[32];

	if (count >= sizeof(kbuf))
		return -EOVERFLOW;

	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	*val = (int)simple_strtoul(kbuf, &end, base);
	if (kbuf == end)
		return -EINVAL;

	return 0;
}

static int
spl_proc_wr_debug_mask(struct file *file, const char *ubuf,
                       unsigned long count, void *data, int mode)
{
	unsigned long val;
	int rc;

	rc = spl_proc_wr_generic_ul(ubuf, count, &val, 16);
	if (rc)
		return rc;

	spin_lock(&spl_debug_lock);
	spl_debug_mask = val;
	spin_unlock(&spl_debug_lock);

	return count;
}

static int
spl_proc_wr_debug_subsys(struct file *file, const char *ubuf,
                         unsigned long count, void *data, int mode)
{
	unsigned long val;
	int rc;

	rc = spl_proc_wr_generic_ul(ubuf, count, &val, 16);
	if (rc)
		return rc;

	spin_lock(&spl_debug_lock);
	spl_debug_subsys = val;
	spin_unlock(&spl_debug_lock);

	return count;
}

static int
spl_proc_wr_hostid(struct file *file, const char *ubuf,
                   unsigned long count, void *data, int mode)
{
	unsigned long val;
	int rc;

	rc = spl_proc_wr_generic_ul(ubuf, count, &val, 16);
	if (rc)
		return rc;

	spl_hostid = val;
	sprintf(hw_serial, "%lu\n", ((long)val >= 0) ? val : -val);

	return count;
}

static struct proc_dir_entry *
spl_register_proc_entry(const char *name, mode_t mode,
                        struct proc_dir_entry *parent, void *data,
                        void *read_proc, void *write_proc)
{
        struct proc_dir_entry *entry;

        entry = create_proc_entry(name, mode, parent);
        if (!entry)
                return ERR_PTR(-EINVAL);

        entry->data = data;
        entry->read_proc = read_proc;
        entry->write_proc = write_proc;

        return entry;
} /* register_proc_entry() */

void spl_set_debug_mask(unsigned long mask) {
	spin_lock(&spl_debug_lock);
	spl_debug_mask = mask;
	spin_unlock(&spl_debug_lock);
}
EXPORT_SYMBOL(spl_set_debug_mask);

void spl_set_debug_subsys(unsigned long mask) {
	spin_lock(&spl_debug_lock);
	spl_debug_subsys = mask;
	spin_unlock(&spl_debug_lock);
}
EXPORT_SYMBOL(spl_set_debug_subsys);

static int __init spl_init(void)
{
	int rc = 0;
	char sh_path[] = "/bin/sh";
	char *argv[] = { sh_path,
	                 "-c",
	                 "/usr/bin/hostid >/proc/spl/hostid",
	                 NULL };
	char *envp[] = { "HOME=/",
	                 "TERM=linux",
	                 "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
	                 NULL };

        spl_proc_root = proc_mkdir("spl", NULL);
        if (!spl_proc_root) {
                printk("spl: Error unable to create /proc/spl/ directory\n");
		return -EINVAL;
        }

	spl_proc_debug_mask = spl_register_proc_entry("debug_mask", 0644,
	                                          spl_proc_root, NULL,
	                                          spl_proc_rd_debug_mask,
	                                          spl_proc_wr_debug_mask);
	if (IS_ERR(spl_proc_debug_mask)) {
		rc = PTR_ERR(spl_proc_debug_mask);
		goto out;
	}

	spl_proc_debug_subsys = spl_register_proc_entry("debug_subsys", 0644,
	                                            spl_proc_root, NULL,
	                                            spl_proc_rd_debug_subsys,
	                                            spl_proc_wr_debug_subsys);
	if (IS_ERR(spl_proc_debug_subsys)) {
		rc = PTR_ERR(spl_proc_debug_subsys);
		goto out2;
	}

	spl_proc_hostid = spl_register_proc_entry("hostid", 0644,
	                                          spl_proc_root, NULL,
	                                          spl_proc_rd_hostid,
	                                          spl_proc_wr_hostid);
	if (IS_ERR(spl_proc_hostid)) {
		rc = PTR_ERR(spl_proc_hostid);
		goto out3;
	}

	spl_proc_hw_serial = spl_register_proc_entry("hw_serial", 0444,
	                                          spl_proc_root, NULL,
	                                          spl_proc_rd_hw_serial,
	                                          NULL);
	if (IS_ERR(spl_proc_hw_serial)) {
		rc = PTR_ERR(spl_proc_hw_serial);
		goto out4;
	}

	if ((rc = kmem_init()))
		goto out4;

	if ((rc = vn_init()))
		goto out4;

	/* Doing address resolution in the kernel is tricky and just
	 * not a good idea in general.  So to set the proper 'hw_serial'
	 * use the usermodehelper support to ask '/bin/sh' to run
	 * '/usr/bin/hostid' and redirect the result to /proc/spl/hostid
	 * for us to use.  It's a horific solution but it will do.
	 */
	if ((rc = call_usermodehelper(sh_path, argv, envp, 1)))
		goto out4;

        printk("spl: Loaded Solaris Porting Layer v%s\n", VERSION);

	return 0;

out4:
	if (spl_proc_hw_serial)
		remove_proc_entry("hw_serial", spl_proc_root);
out3:
	if (spl_proc_hostid)
		remove_proc_entry("hostid", spl_proc_root);
out2:
	if (spl_proc_debug_mask)
		remove_proc_entry("debug_mask", spl_proc_root);

	if (spl_proc_debug_subsys)
		remove_proc_entry("debug_subsys", spl_proc_root);
out:
        remove_proc_entry("spl", NULL);

	return rc;
}

static void spl_fini(void)
{
	vn_fini();
	kmem_fini();

	remove_proc_entry("hw_serial", spl_proc_root);
	remove_proc_entry("hostid", spl_proc_root);
	remove_proc_entry("debug_subsys", spl_proc_root);
	remove_proc_entry("debug_mask", spl_proc_root);
        remove_proc_entry("spl", NULL);

	return;
}

module_init(spl_init);
module_exit(spl_fini);

MODULE_AUTHOR("Lawrence Livermore National Labs");
MODULE_DESCRIPTION("Solaris Porting Layer");
MODULE_LICENSE("GPL");
