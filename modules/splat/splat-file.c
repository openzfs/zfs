#include "splat-internal.h"

#define SPLAT_SUBSYSTEM_FILE		0x0b00
#define SPLAT_FILE_NAME			"file"
#define SPLAT_FILE_DESC			"Kernel File Tests"

#define SPLAT_FILE_TEST1_ID		0x0b01
#define SPLAT_FILE_TEST1_NAME		"getf"
#define SPLAT_FILE_TEST1_DESC		"File getf/releasef Test"

static int
splat_file_test1(struct file *file, void *arg)
{
	splat_vprint(file, SPLAT_FILE_TEST1_NAME, "WRITE A TEST, %d\n", 0);

        return 0;
} /* splat_file_test1() */


splat_subsystem_t *
splat_file_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_FILE_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_FILE_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_FILE;

        SPLAT_TEST_INIT(sub, SPLAT_FILE_TEST1_NAME, SPLAT_FILE_TEST1_DESC,
	              SPLAT_FILE_TEST1_ID, splat_file_test1);

        return sub;
} /* splat_file_init() */

void
splat_file_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_FILE_TEST1_ID);

        kfree(sub);
} /* splat_file_fini() */

int
splat_file_id(void)
{
        return SPLAT_SUBSYSTEM_FILE;
} /* splat_file_id() */
