/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2023, 2024, Klara Inc.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/param.h>
#include <stdlib.h>

/*
 * This tests the vdev_disk page alignment check callback
 * vdev_disk_check_pages_cb(). For now, this test includes a copy of that
 * function from module/os/linux/zfs/vdev_disk.c. If you change it here,
 * remember to change it there too, and add tests data here to validate the
 * change you're making.
 */

struct page;

typedef struct {
	uint32_t  bmask;
	uint32_t  npages;
	uint32_t  end;
} vdev_disk_check_pages_t;

static int
vdev_disk_check_pages_cb(struct page *page, size_t off, size_t len, void *priv)
{
	(void) page;
	vdev_disk_check_pages_t *s = priv;

	/*
	 * If we didn't finish on a block size boundary last time, then there
	 * would be a gap if we tried to use this ABD as-is, so abort.
	 */
	if (s->end != 0)
		return (1);

	/*
	 * Note if we're taking less than a full block, so we can check it
	 * above on the next call.
	 */
	s->end = (off+len) & s->bmask;

	/* All blocks after the first must start on a block size boundary. */
	if (s->npages != 0 && (off & s->bmask) != 0)
		return (1);

	s->npages++;
	return (0);
}

typedef struct {
	/* test name */
	const char	*name;

	/* blocks size mask */
	uint32_t	mask;

	/* amount of data to take */
	size_t		size;

	/* [start offset in page, len to end of page or size] */
	size_t		pages[16][2];
} page_test_t;

static const page_test_t valid_tests[] = {
	/* 512B block tests */
	{
		"512B blocks, 4K single page",
		0x1ff, 0x1000, {
			{ 0x0, 0x1000 },
		},
	}, {
		"512B blocks, 1K at start of page",
		0x1ff, 0x400, {
			{ 0x0, 0x1000 },
		},
	}, {
		"512B blocks, 1K at end of page",
		0x1ff, 0x400, {
			{ 0x0c00, 0x0400 },
		},
	}, {
		"512B blocks, 1K within page, 512B start offset",
		0x1ff, 0x400, {
			{ 0x0200, 0x0e00 },
		},
	}, {
		"512B blocks, 8K across 2x4K pages",
		0x1ff, 0x2000, {
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
		},
	}, {
		"512B blocks, 4K across two pages, 2K start offset",
		0x1ff, 0x1000, {
			{ 0x0800, 0x0800 },
			{ 0x0,    0x0800 },
		},
	}, {
		"512B blocks, 16K across 5x4K pages, 512B start offset",
		0x1ff, 0x4000, {
			{ 0x0200, 0x0e00 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0200 },
		},
	}, {
		"512B blocks, 64K data, 8x8K compound pages",
		0x1ff, 0x10000, {
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
		},
	}, {
		"512B blocks, 64K data, 9x8K compound pages, 512B start offset",
		0x1ff, 0x10000, {
			{ 0x0200, 0x1e00 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x0200 },
		},
	}, {
		"512B blocks, 64K data, 2x16K compound pages, 8x4K pages",
		0x1ff, 0x10000, {
			{ 0x0, 0x8000 },
			{ 0x0, 0x8000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
		},
	}, {
		"512B blocks, 64K data, mixed 4K/8K/16K pages",
		0x1ff, 0x10000, {
			{ 0x0, 0x1000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x8000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x2000 },
		},
	}, {
		"512B blocks, 64K data, mixed 4K/8K/16K pages, 1K start offset",
		0x1ff, 0x10000, {
			{ 0x0400, 0x0c00 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x8000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0400 },
		},
	},

	/* 4K block tests */
	{
		"4K blocks, 4K single page",
		0xfff, 0x1000, {
			{ 0x0, 0x1000 },
		},
	}, {
		"4K blocks, 1K at start of page",
		0xfff, 0x400, {
			{ 0x0, 0x1000 },
		},
	}, {
		"4K blocks, 1K at end of page",
		0xfff, 0x400, {
			{ 0x0c00, 0x0400 },
		},
	}, {
		"4K blocks, 1K within page, 512B start offset",
		0xfff, 0x400, {
			{ 0x0200, 0x0e00 },
		},
	}, {
		"4K blocks, 8K across 2x4K pages",
		0xfff, 0x2000, {
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
		},
	}, {
		"4K blocks, 4K across two pages, 2K start offset",
		0xfff, 0x1000, {
			{ 0x0800, 0x0800 },
			{ 0x0,    0x0800 },
		},
	}, {
		"4K blocks, 16K across 5x4K pages, 512B start offset",
		0xfff, 0x4000, {
			{ 0x0200, 0x0e00 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0200 },
		},
	}, {
		"4K blocks, 64K data, 8x8K compound pages",
		0xfff, 0x10000, {
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x2000 },
		},
	}, {
		"4K blocks, 64K data, 9x8K compound pages, 512B start offset",
		0xfff, 0x10000, {
			{ 0x0200, 0x1e00 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x0200 },
		},
	}, {
		"4K blocks, 64K data, 2x16K compound pages, 8x4K pages",
		0xfff, 0x10000, {
			{ 0x0, 0x8000 },
			{ 0x0, 0x8000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
		},
	}, {
		"4K blocks, 64K data, mixed 4K/8K/16K pages",
		0xfff, 0x10000, {
			{ 0x0, 0x1000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x8000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x2000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x2000 },
		},
	}, {
		"4K blocks, 64K data, mixed 4K/8K/16K pages, 1K start offset",
		0xfff, 0x10000, {
			{ 0x0400, 0x0c00 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x2000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x8000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0400 },
		},
	},

	{ 0 },
};

static const page_test_t invalid_tests[] = {
	{
		"512B blocks, 16K data, 512 leader (gang block simulation)",
		0x1ff, 0x8000, {
			{ 0x0, 0x0200 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x0c00 },
		},
	}, {
		"4K blocks, 32K data, 2 incompatible spans "
		"(gang abd simulation)",
		0xfff, 0x8000, {
			{ 0x0800, 0x0800 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0800 },
			{ 0x0800, 0x0800 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0800 },
		},
	},
	{ 0 },
};

static bool
run_test(const page_test_t *test, bool verbose)
{
	size_t rem = test->size;

	vdev_disk_check_pages_t s = {
	    .bmask = 0xfff,
	    .npages = 0,
	    .end = 0,
	};

	for (int i = 0; test->pages[i][1] > 0; i++) {
		size_t off = test->pages[i][0];
		size_t len = test->pages[i][1];

		size_t take = MIN(rem, len);

		if (verbose)
			printf("  page %d [off %lx len %lx], "
			    "rem %lx, take %lx\n",
			    i, off, len, rem, take);

		if (vdev_disk_check_pages_cb(NULL, off, take, &s)) {
			if (verbose)
				printf("  ABORT: misalignment detected, "
				    "rem %lx\n", rem);
			return (false);
		}

		rem -= take;
		if (rem == 0)
			break;
	}

	if (rem > 0) {
		if (verbose)
			printf("  ABORT: ran out of pages, rem %lx\n", rem);
		return (false);
	}

	return (true);
}

static void
run_test_set(const page_test_t *tests, bool want, int *ntests, int *npassed)
{
	for (const page_test_t *test = &tests[0]; test->name; test++) {
		bool pass = (run_test(test, false) == want);
		if (pass) {
			printf("%s: PASS\n", test->name);
			(*npassed)++;
		} else {
			printf("%s: FAIL [expected %s, got %s]\n", test->name,
			    want ? "VALID" : "INVALID",
			    want ? "INVALID" : "VALID");
			run_test(test, true);
		}
		(*ntests)++;
	}
}

int main(void) {
	int ntests = 0, npassed = 0;

	run_test_set(valid_tests, true, &ntests, &npassed);
	run_test_set(invalid_tests, false, &ntests, &npassed);

	printf("\n%d/%d tests passed\n", npassed, ntests);

	return (ntests == npassed ? 0 : 1);
}
