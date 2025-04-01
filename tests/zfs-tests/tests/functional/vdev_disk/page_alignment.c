// SPDX-License-Identifier: CDDL-1.0
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
 * vdev_disk_check_alignment_cb(). For now, this test includes a copy of that
 * function from module/os/linux/zfs/vdev_disk.c. If you change it here,
 * remember to change it there too, and add tests data here to validate the
 * change you're making.
 */

struct page;

/*
 * This is spl_pagesize() in userspace, which requires linking libspl, but
 * would also then use the platform page size, which isn't what we want for
 * a test. To keep the check callback the same as the real one, we just
 * redefine it.
 */
#undef	PAGESIZE
#define	PAGESIZE	(4096)

typedef struct {
	size_t	blocksize;
	int	seen_first;
	int	seen_last;
} vdev_disk_check_alignment_t;

static int
vdev_disk_check_alignment_cb(struct page *page, size_t off, size_t len,
    void *priv)
{
	(void) page;
	vdev_disk_check_alignment_t *s = priv;

	/*
	 * The cardinal rule: a single on-disk block must never cross an
	 * physical (order-0) page boundary, as the kernel expects to be able
	 * to split at both LBS and page boundaries.
	 *
	 * This implies various alignment rules for the blocks in this
	 * (possibly compound) page, which we can check for.
	 */

	/*
	 * If the previous page did not end on a page boundary, then we
	 * can't proceed without creating a hole.
	 */
	if (s->seen_last)
		return (1);

	/* This page must contain only whole LBS-sized blocks. */
	if (!IS_P2ALIGNED(len, s->blocksize))
		return (1);

	/*
	 * If this is not the first page in the ABD, then the data must start
	 * on a page-aligned boundary (so the kernel can split on page
	 * boundaries without having to deal with a hole). If it is, then
	 * it can start on LBS-alignment.
	 */
	if (s->seen_first) {
		if (!IS_P2ALIGNED(off, PAGESIZE))
			return (1);
	} else {
		if (!IS_P2ALIGNED(off, s->blocksize))
			return (1);
		s->seen_first = 1;
	}

	/*
	 * If this data does not end on a page-aligned boundary, then this
	 * must be the last page in the ABD, for the same reason.
	 */
	s->seen_last = !IS_P2ALIGNED(off+len, PAGESIZE);

	return (0);
}

typedef struct {
	/* test name */
	const char	*name;

	/* stored block size */
	uint32_t	blocksize;

	/* amount of data to take */
	size_t		size;

	/* [start offset in page, len to end of page or size] */
	size_t		pages[16][2];
} page_test_t;

static const page_test_t valid_tests[] = {
	/* 512B block tests */
	{
		"512B blocks, 4K single page",
		512, 0x1000, {
			{ 0x0, 0x1000 },
		},
	}, {
		"512B blocks, 1K at start of page",
		512, 0x400, {
			{ 0x0, 0x1000 },
		},
	}, {
		"512B blocks, 1K at end of page",
		512, 0x400, {
			{ 0x0c00, 0x0400 },
		},
	}, {
		"512B blocks, 1K within page, 512B start offset",
		512, 0x400, {
			{ 0x0200, 0x0e00 },
		},
	}, {
		"512B blocks, 8K across 2x4K pages",
		512, 0x2000, {
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
		},
	}, {
		"512B blocks, 4K across two pages, 2K start offset",
		512, 0x1000, {
			{ 0x0800, 0x0800 },
			{ 0x0,    0x0800 },
		},
	}, {
		"512B blocks, 16K across 5x4K pages, 512B start offset",
		512, 0x4000, {
			{ 0x0200, 0x0e00 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0200 },
		},
	}, {
		"512B blocks, 64K data, 8x8K compound pages",
		512, 0x10000, {
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
		512, 0x10000, {
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
		512, 0x10000, {
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
		512, 0x10000, {
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
		512, 0x10000, {
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
		4096, 0x1000, {
			{ 0x0, 0x1000 },
		},
	}, {
		"4K blocks, 8K across 2x4K pages",
		4096, 0x2000, {
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
		},
	}, {
		"4K blocks, 64K data, 8x8K compound pages",
		4096, 0x10000, {
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
		"4K blocks, 64K data, 2x16K compound pages, 8x4K pages",
		4096, 0x10000, {
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
		4096, 0x10000, {
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
	},

	{ 0 },
};

static const page_test_t invalid_tests[] = {
	/*
	 * Gang tests. Composed of lots of smaller allocations, rarely properly
	 * aligned.
	 */
	{
		"512B blocks, 16K data, 512 leader (gang block simulation)",
		512, 0x8000, {
			{ 0x0, 0x0200 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x1000 },
			{ 0x0, 0x0c00 },
		},
	}, {
		"4K blocks, 32K data, 2 incompatible spans "
		"(gang abd simulation)",
		4096, 0x8000, {
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

	/*
	 * Blocks must not span multiple physical pages. These tests used to
	 * be considered valid, but were since found to be invalid and were
	 * moved here.
	 */
	{
		"4K blocks, 4K across two pages, 2K start offset",
		4096, 0x1000, {
			{ 0x0800, 0x0800 },
			{ 0x0,    0x0800 },
		},
	}, {
		"4K blocks, 16K across 5x4K pages, 512B start offset",
		4096, 0x4000, {
			{ 0x0200, 0x0e00 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x1000 },
			{ 0x0,    0x0200 },
		},
	}, {
		"4K blocks, 64K data, 9x8K compound pages, 512B start offset",
		4096, 0x10000, {
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
		"4K blocks, 64K data, mixed 4K/8K/16K pages, 1K start offset",
		4096, 0x10000, {
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

	/*
	 * This is the very typical case of a 4K block being allocated from
	 * the middle of a mixed-used slab backed by a higher-order compound
	 * page.
	 */
	{
		"4K blocks, 4K data from compound slab, 2K-align offset",
		4096, 0x1000, {
			{ 0x1800, 0x6800 }
		}
	},

	/*
	 * Blocks smaller than LBS should never be possible, but used to be by
	 * accident (see GH#16990). We test for and reject them just to be
	 * sure.
	 */
	{
		"4K blocks, 1K at end of page",
		4096, 0x400, {
			{ 0x0c00, 0x0400 },
		},
	}, {
		"4K blocks, 1K at start of page",
		4096, 0x400, {
			{ 0x0, 0x1000 },
		},
	}, {
		"4K blocks, 1K within page, 512B start offset",
		4096, 0x400, {
			{ 0x0200, 0x0e00 },
		},
	},

	{ 0 },
};

static bool
run_test(const page_test_t *test, bool verbose)
{
	size_t rem = test->size;

	vdev_disk_check_alignment_t s = {
	    .blocksize = test->blocksize,
	};

	for (int i = 0; test->pages[i][1] > 0; i++) {
		size_t off = test->pages[i][0];
		size_t len = test->pages[i][1];

		size_t take = MIN(rem, len);

		if (verbose)
			printf("  page %d [off %zx len %zx], "
			    "rem %zx, take %zx\n",
			    i, off, len, rem, take);

		if (vdev_disk_check_alignment_cb(NULL, off, take, &s)) {
			if (verbose)
				printf("  ABORT: misalignment detected, "
				    "rem %zx\n", rem);
			return (false);
		}

		rem -= take;
		if (rem == 0)
			break;
	}

	if (rem > 0) {
		if (verbose)
			printf("  ABORT: ran out of pages, rem %zx\n", rem);
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
			printf("%c %s: PASS\n", want ? '+' : '-', test->name);
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
