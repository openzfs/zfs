/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "splat-internal.h"

#define SPLAT_SUBSYSTEM_GENERIC		0x0d00
#define SPLAT_GENERIC_NAME		"generic"
#define SPLAT_GENERIC_DESC		"Kernel Generic Tests"

#define SPLAT_GENERIC_TEST1_ID		0x0d01
#define SPLAT_GENERIC_TEST1_NAME	"ddi_strtoul"
#define SPLAT_GENERIC_TEST1_DESC	"ddi_strtoul Test"

#define SPLAT_GENERIC_TEST2_ID		0x0d02
#define SPLAT_GENERIC_TEST2_NAME	"ddi_strtol"
#define SPLAT_GENERIC_TEST2_DESC	"ddi_strtol Test"

#define SPLAT_GENERIC_TEST3_ID		0x0d03
#define SPLAT_GENERIC_TEST3_NAME	"ddi_strtoull"
#define SPLAT_GENERIC_TEST3_DESC	"ddi_strtoull Test"

#define SPLAT_GENERIC_TEST4_ID		0x0d04
#define SPLAT_GENERIC_TEST4_NAME	"ddi_strtoll"
#define SPLAT_GENERIC_TEST4_DESC	"ddi_strtoll Test"

#define STR_POS				"123456789"
#define STR_NEG				"-123456789"
#define STR_BASE			"0xabcdef"
#define STR_RANGE_MAX			"10000000000000000"
#define STR_RANGE_MIN			"-10000000000000000"
#define STR_INVAL1			"12345U"
#define STR_INVAL2			"invald"

#define VAL_POS				123456789
#define VAL_NEG				-123456789
#define VAL_BASE			0xabcdef
#define VAL_INVAL1			12345U

#define define_generic_msg_strtox(type, valtype)			\
static void								\
generic_msg_strto##type(struct file *file, char *msg, int rc, int *err, \
			const char *s, valtype d, char *endptr)		\
{									\
	splat_vprint(file, SPLAT_GENERIC_TEST1_NAME,			\
		     "%s (%d) %s: %s == %lld, 0x%p\n",			\
		     rc ? "Fail" : "Pass", *err, msg, s,		\
		     (unsigned long long)d, endptr);			\
	*err = rc;							\
}

define_generic_msg_strtox(ul, unsigned long);
define_generic_msg_strtox(l, long);
define_generic_msg_strtox(ull, unsigned long long);
define_generic_msg_strtox(ll, long long);

#define define_splat_generic_test_strtox(type, valtype)			\
static int								\
splat_generic_test_strto##type(struct file *file, void *arg)		\
{									\
	int rc, rc1, rc2, rc3, rc4, rc5, rc6, rc7;			\
	char str[20], *endptr;						\
	valtype r;							\
									\
	/* Positive value: expect success */				\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc1 = ddi_strto##type(STR_POS, &endptr, 10, &r);		\
	if (rc1 == 0 && r == VAL_POS && endptr && *endptr == '\0')	\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "positive", rc , &rc1,		\
				STR_POS, r, endptr);			\
									\
	/* Negative value: expect success */				\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	strcpy(str, STR_NEG);						\
	rc2 = ddi_strto##type(str, &endptr, 10, &r);			\
	if (#type[0] == 'u') {						\
		if (rc2 == 0 && r == 0 && endptr == str)		\
			rc = 0;						\
	} else {							\
		if (rc2 == 0 && r == VAL_NEG &&				\
		    endptr && *endptr == '\0')				\
			rc = 0;						\
	}								\
									\
	generic_msg_strto##type(file, "negative", rc, &rc2,		\
				STR_NEG, r, endptr);			\
									\
	/* Non decimal base: expect sucess */				\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc3 = ddi_strto##type(STR_BASE, &endptr, 0, &r);		\
	if (rc3 == 0 && r == VAL_BASE && endptr && *endptr == '\0')	\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "base", rc, &rc3,			\
				STR_BASE, r, endptr);			\
									\
	/* Max out of range: failure expected, r unchanged */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc4 = ddi_strto##type(STR_RANGE_MAX, &endptr, 16, &r);		\
	if (rc4 == ERANGE && r == 0 && endptr == NULL)			\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "max", rc, &rc4,			\
				STR_RANGE_MAX, r, endptr);		\
									\
	/* Min out of range: failure expected, r unchanged */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	strcpy(str, STR_RANGE_MIN);					\
	rc5 = ddi_strto##type(str, &endptr, 16, &r);			\
	if (#type[0] == 'u') {						\
		if (rc5 == 0 && r == 0 && endptr == str)		\
			rc = 0;						\
	} else {							\
		if (rc5 == ERANGE && r == 0 && endptr == NULL)		\
			rc = 0;						\
	}								\
									\
	generic_msg_strto##type(file, "min", rc, &rc5,			\
				STR_RANGE_MIN, r, endptr);		\
									\
	/* Invalid string: success expected, endptr == 'U' */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc6 = ddi_strto##type(STR_INVAL1, &endptr, 10, &r);		\
	if (rc6 == 0 && r == VAL_INVAL1 && endptr && *endptr == 'U')	\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "invalid", rc, &rc6,		\
				STR_INVAL1, r, endptr);			\
									\
	/* Invalid string: failure expected, endptr == str */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	strcpy(str, STR_INVAL2);					\
	rc7 = ddi_strto##type(str, &endptr, 10, &r);			\
	if (rc7 == 0 && r == 0 && endptr == str)			\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "invalid", rc, &rc7,		\
				STR_INVAL2, r, endptr);			\
									\
        return (rc1 || rc2 || rc3 || rc4 || rc5 || rc6 || rc7) ?	\
		-EINVAL : 0;						\
}

define_splat_generic_test_strtox(ul, unsigned long);
define_splat_generic_test_strtox(l, long);
define_splat_generic_test_strtox(ull, unsigned long long);
define_splat_generic_test_strtox(ll, long long);

splat_subsystem_t *
splat_generic_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_GENERIC_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_GENERIC_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_GENERIC;

        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST1_NAME, SPLAT_GENERIC_TEST1_DESC,
	                SPLAT_GENERIC_TEST1_ID, splat_generic_test_strtoul);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST2_NAME, SPLAT_GENERIC_TEST2_DESC,
	                SPLAT_GENERIC_TEST2_ID, splat_generic_test_strtol);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST3_NAME, SPLAT_GENERIC_TEST3_DESC,
	                SPLAT_GENERIC_TEST3_ID, splat_generic_test_strtoull);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST4_NAME, SPLAT_GENERIC_TEST4_DESC,
	                SPLAT_GENERIC_TEST4_ID, splat_generic_test_strtoll);

        return sub;
}

void
splat_generic_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST4_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST1_ID);

        kfree(sub);
}

int
splat_generic_id(void)
{
        return SPLAT_SUBSYSTEM_GENERIC;
}
