#ifndef _ZILPMEM_TEST_H_
#define _ZILPMEM_TEST_H_

#include <stdio.h>
#include <stdlib.h>

typedef struct zilpmem_test_subcommand {
	const char *name;
	int (*func)(int argc, char *argv[]);
} zilpmem_test_subcommand_t;

int
zilpmem_test_subcommand_dispatch(const zilpmem_test_subcommand_t *scs,
    int argc, char *argv[]);

#endif /* _ZILPMEM_TEST_H_ */
