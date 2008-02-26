#include <sys/zfs_context.h>
#include <sys/splat-ctl.h>

#define KZT_SUBSYSTEM_TIME		0x0800
#define KZT_TIME_NAME			"time"
#define KZT_TIME_DESC			"Kernel Time Tests"

#define KZT_TIME_TEST1_ID		0x0801
#define KZT_TIME_TEST1_NAME		"time1"
#define KZT_TIME_TEST1_DESC		"HZ Test"

#define KZT_TIME_TEST2_ID		0x0802
#define KZT_TIME_TEST2_NAME		"time2"
#define KZT_TIME_TEST2_DESC		"Monotonic Test"

static int
kzt_time_test1(struct file *file, void *arg)
{
	int myhz = hz;
	kzt_vprint(file, KZT_TIME_TEST1_NAME, "hz is %d\n", myhz);
        return 0;
}

static int
kzt_time_test2(struct file *file, void *arg)
{
        hrtime_t tm1, tm2;
	int i;

        tm1 = gethrtime();
        kzt_vprint(file, KZT_TIME_TEST2_NAME, "time is %lld\n", tm1);

        for(i = 0; i < 100; i++) {
                tm2 = gethrtime();
                kzt_vprint(file, KZT_TIME_TEST2_NAME, "time is %lld\n", tm2);

                if(tm1 > tm2) {
                        kzt_print(file, "%s: gethrtime() is not giving monotonically increasing values\n", KZT_TIME_TEST2_NAME);
                        return 1;
                }
                tm1 = tm2;

                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(10);
        }

        return 0;
}

kzt_subsystem_t *
kzt_time_init(void)
{
        kzt_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, KZT_TIME_NAME, KZT_NAME_SIZE);
	strncpy(sub->desc.desc, KZT_TIME_DESC, KZT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = KZT_SUBSYSTEM_TIME;

        KZT_TEST_INIT(sub, KZT_TIME_TEST1_NAME, KZT_TIME_TEST1_DESC,
	              KZT_TIME_TEST1_ID, kzt_time_test1);
        KZT_TEST_INIT(sub, KZT_TIME_TEST2_NAME, KZT_TIME_TEST2_DESC,
	              KZT_TIME_TEST2_ID, kzt_time_test2);

        return sub;
}

void
kzt_time_fini(kzt_subsystem_t *sub)
{
        ASSERT(sub);

        KZT_TEST_FINI(sub, KZT_TIME_TEST2_ID);
        KZT_TEST_FINI(sub, KZT_TIME_TEST1_ID);

        kfree(sub);
}

int
kzt_time_id(void)
{
        return KZT_SUBSYSTEM_TIME;
}
