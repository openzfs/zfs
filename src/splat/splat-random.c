#include <sys/zfs_context.h>
#include <sys/splat-ctl.h>

#define KZT_SUBSYSTEM_KRNG		0x0300
#define KZT_KRNG_NAME			"krng"
#define KZT_KRNG_DESC			"Kernel Random Number Generator Tests"

#define KZT_KRNG_TEST1_ID		0x0301
#define KZT_KRNG_TEST1_NAME		"freq"
#define KZT_KRNG_TEST1_DESC		"Frequency Test"

#define KRNG_NUM_BITS			1048576
#define KRNG_NUM_BYTES			(KRNG_NUM_BITS >> 3)
#define KRNG_NUM_BITS_DIV2		(KRNG_NUM_BITS >> 1)
#define KRNG_ERROR_RANGE		2097

/* Random Number Generator Tests
   There can be meny more tests on quality of the
   random number generator.  For now we are only
   testing the frequency of particular bits.
   We could also test consecutive sequences,
   randomness within a particular block, etc.
   but is probably not necessary for our purposes */

static int
kzt_krng_test1(struct file *file, void *arg)
{
	uint8_t *buf;
	int i, j, diff, num = 0, rc = 0;

	buf = kmalloc(sizeof(*buf) * KRNG_NUM_BYTES, GFP_KERNEL);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	memset(buf, 0, sizeof(*buf) * KRNG_NUM_BYTES);

	/* Always succeeds */
	random_get_pseudo_bytes(buf, sizeof(uint8_t) * KRNG_NUM_BYTES);

	for (i = 0; i < KRNG_NUM_BYTES; i++) {
		uint8_t tmp = buf[i];
		for (j = 0; j < 8; j++) {
			uint8_t tmp2 = ((tmp >> j) & 0x01);
			if (tmp2 == 1) {
				num++;
			}
		}
	}

	kfree(buf);

	diff = KRNG_NUM_BITS_DIV2 - num;
	if (diff < 0)
		diff *= -1;

	kzt_print(file, "Test 1 Number of ones: %d\n", num);
	kzt_print(file, "Test 1 Difference from expected: %d Allowed: %d\n",
                  diff, KRNG_ERROR_RANGE);

	if (diff > KRNG_ERROR_RANGE)
		rc = -ERANGE;
out:
	return rc;
}

kzt_subsystem_t *
kzt_krng_init(void)
{
        kzt_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, KZT_KRNG_NAME, KZT_NAME_SIZE);
	strncpy(sub->desc.desc, KZT_KRNG_DESC, KZT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = KZT_SUBSYSTEM_KRNG;

        KZT_TEST_INIT(sub, KZT_KRNG_TEST1_NAME, KZT_KRNG_TEST1_DESC,
	              KZT_KRNG_TEST1_ID, kzt_krng_test1);

        return sub;
}

void
kzt_krng_fini(kzt_subsystem_t *sub)
{
        ASSERT(sub);

        KZT_TEST_FINI(sub, KZT_KRNG_TEST1_ID);

        kfree(sub);
}

int
kzt_krng_id(void) {
        return KZT_SUBSYSTEM_KRNG;
}
