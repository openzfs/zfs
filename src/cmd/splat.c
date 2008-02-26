/* Kernel ZFS Test (KZT) user space command interface */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <fcntl.h>
#include <libuutil.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "splat.h"

#undef ioctl

static const char shortOpts[] = "hvlat:xc";
static const struct option longOpts[] = {
	{ "help",            no_argument,       0, 'h' },
	{ "verbose",         no_argument,       0, 'v' },
	{ "list",            no_argument,       0, 'l' },
	{ "all",             no_argument,       0, 'a' },
	{ "test",            required_argument, 0, 't' },
	{ "exit",            no_argument,       0, 'x' },
	{ "nocolor",         no_argument,       0, 'c' },
	{ 0,                 0,                 0, 0   }
};

static uu_list_t *subsystems;			/* Subsystem/tests */
static uu_list_pool_t *subsystem_pool;		/* Subsystem pool */
static uu_list_pool_t *test_pool;		/* Test pool */
static int kztctl_fd;				/* Control file descriptor */
static char kzt_version[KZT_VERSION_SIZE];	/* Kernel version string */
static char *kzt_buffer = NULL;			/* Scratch space area */
static int kzt_buffer_size = 0;			/* Scratch space size */


static void test_list(uu_list_t *, int);
static int dev_clear(void);


static int usage(void) {
	fprintf(stderr, "usage: kzt [hvla] [-t <subsystem:<tests>>]\n");
	fprintf(stderr,
	"  --help      -h               This help\n"
	"  --verbose   -v               Increase verbosity\n"
	"  --list      -l               List all tests in all subsystems\n"
	"  --all       -a               Run all tests in all subsystems\n"
	"  --test      -t <sub:test>    Run 'test' in subsystem 'sub'\n"
	"  --exit      -x               Exit on first test error\n"
	"  --nocolor   -c               Do not colorize output\n");
	fprintf(stderr, "\n"
	"Examples:\n"
	"  kzt -t kmem:all     # Runs all kmem tests\n"
	"  kzt -t taskq:0x201  # Run taskq test 0x201\n");

	return 0;
}

static subsystem_t *subsystem_init(kzt_user_t *desc)
{
	subsystem_t *sub;

	sub = (subsystem_t *)malloc(sizeof(*sub));
	if (sub == NULL)
		return NULL;

	memcpy(&sub->sub_desc, desc, sizeof(*desc));
	uu_list_node_init(sub, &sub->sub_node, subsystem_pool);

	sub->sub_tests = uu_list_create(test_pool, NULL, 0);
	if (sub->sub_tests == NULL) {
		free(sub);
		return NULL;
	}

	return sub;
}

static void subsystem_fini(subsystem_t *sub)
{
	assert(sub != NULL);

	uu_list_node_fini(sub, &sub->sub_node, subsystem_pool);
	free(sub);
}

static int subsystem_setup(void)
{
	kzt_cfg_t *cfg;
	int i, rc, size, cfg_size;
	subsystem_t *sub;
	kzt_user_t *desc;

	/* Aquire the number of registered subsystems */
	cfg_size = sizeof(*cfg);
	cfg = (kzt_cfg_t *)malloc(cfg_size);
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, cfg_size);
	cfg->cfg_magic = KZT_CFG_MAGIC;
        cfg->cfg_cmd   = KZT_CFG_SUBSYSTEM_COUNT;

	rc = ioctl(kztctl_fd, KZT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) KZT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	size = cfg->cfg_rc1;
	free(cfg);

	/* Based on the newly aquired number of subsystems allocate enough
	 * memory to get the descriptive information for them all. */
	cfg_size = sizeof(*cfg) + size * sizeof(kzt_user_t);
	cfg = (kzt_cfg_t *)malloc(cfg_size);
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, cfg_size);
	cfg->cfg_magic = KZT_CFG_MAGIC;
	cfg->cfg_cmd   = KZT_CFG_SUBSYSTEM_LIST;
	cfg->cfg_data.kzt_subsystems.size = size;

	rc = ioctl(kztctl_fd, KZT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) KZT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	/* Add the new subsystems in to the global list */
	size = cfg->cfg_rc1;
	for (i = 0; i < size; i++) {
		desc = &(cfg->cfg_data.kzt_subsystems.descs[i]);

		sub = subsystem_init(desc);
		if (sub == NULL) {
			fprintf(stderr, "Error initializing subsystem: %s\n",
			        desc->name);
			free(cfg);
			return -ENOMEM;
		}

		uu_list_insert(subsystems, sub, 0);
	}

	free(cfg);
	return 0;
}

static int subsystem_compare(const void *l_arg, const void *r_arg, void *private)
{
	const subsystem_t *l = l_arg;
	const subsystem_t *r = r_arg;

	if (l->sub_desc.id > r->sub_desc.id)
		return 1;

	if (l->sub_desc.id < r->sub_desc.id)
		return -1;

	return 0;
}

static void subsystem_list(uu_list_t *list, int indent)
{
	subsystem_t *sub;

	fprintf(stdout,
	        "------------------------------- "
	        "Available KZT Tests "
	        "-------------------------------\n");

	for (sub = uu_list_first(list); sub != NULL;
	     sub = uu_list_next(list, sub)) {
		fprintf(stdout, "%*s0x%0*x %-*s ---- %s ----\n",
		        indent, "",
		        4, sub->sub_desc.id,
		        KZT_NAME_SIZE + 7, sub->sub_desc.name,
		        sub->sub_desc.desc);
		test_list(sub->sub_tests, indent + 7);
	}
}

static test_t *test_init(subsystem_t *sub, kzt_user_t *desc)
{
	test_t *test;

	test = (test_t *)malloc(sizeof(*test));
	if (test == NULL)
		return NULL;

	test->test_sub = sub;
	memcpy(&test->test_desc, desc, sizeof(*desc));
	uu_list_node_init(test, &test->test_node, test_pool);

	return test;
}

static void test_fini(test_t *test)
{
	assert(test != NULL);

	uu_list_node_fini(test, &test->test_node, test_pool);
	free(test);
}

static int test_setup(subsystem_t *sub)
{
	kzt_cfg_t *cfg;
	int i, rc, size;
	test_t *test;
	kzt_user_t *desc;

	/* Aquire the number of registered tests for the give subsystem */
	cfg = (kzt_cfg_t *)malloc(sizeof(*cfg));
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, sizeof(*cfg));
	cfg->cfg_magic = KZT_CFG_MAGIC;
        cfg->cfg_cmd   = KZT_CFG_TEST_COUNT;
	cfg->cfg_arg1  = sub->sub_desc.id; /* Subsystem of interest */

	rc = ioctl(kztctl_fd, KZT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) KZT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	size = cfg->cfg_rc1;
	free(cfg);

	/* Based on the newly aquired number of tests allocate enough
	 * memory to get the descriptive information for them all. */
	cfg = (kzt_cfg_t *)malloc(sizeof(*cfg) + size * sizeof(kzt_user_t));
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, sizeof(*cfg) + size * sizeof(kzt_user_t));
	cfg->cfg_magic = KZT_CFG_MAGIC;
	cfg->cfg_cmd   = KZT_CFG_TEST_LIST;
	cfg->cfg_arg1  = sub->sub_desc.id; /* Subsystem of interest */
	cfg->cfg_data.kzt_tests.size = size;

	rc = ioctl(kztctl_fd, KZT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) KZT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	/* Add the new tests in to the relevant subsystems */
	size = cfg->cfg_rc1;
	for (i = 0; i < size; i++) {
		desc = &(cfg->cfg_data.kzt_tests.descs[i]);

		test = test_init(sub, desc);
		if (test == NULL) {
			fprintf(stderr, "Error initializing test: %s\n",
			        desc->name);
			free(cfg);
			return -ENOMEM;
		}

		uu_list_insert(sub->sub_tests, test, 0);
	}

	free(cfg);
	return 0;
}

static int test_compare(const void *l_arg, const void *r_arg, void *private)
{
	const test_t *l = l_arg;
	const test_t *r = r_arg;

	if (l->test_desc.id > r->test_desc.id)
		return 1;

	if (l->test_desc.id < r->test_desc.id)
		return -1;

	return 0;
}

static test_t *test_copy(test_t *test)
{
	return test_init(test->test_sub, &test->test_desc);
}

static void test_list(uu_list_t *list, int indent)
{
	test_t *test;

	for (test = uu_list_first(list); test != NULL;
	     test = uu_list_next(list, test))
		fprintf(stdout, "%*s0x%0*x %-*s %-*s\n",
		        indent, "",
		        04, test->test_desc.id,
		        KZT_NAME_SIZE, test->test_desc.name,
		        KZT_DESC_SIZE, test->test_desc.desc);
}

static test_t *test_find(char *sub_str, char *test_str)
{
	subsystem_t *sub;
	test_t *test;
	int sub_num, test_num;

	/* No error checking here because it may not be a number, it's
	 * perfectly OK for it to be a string.  Since we're just using
	 * it for comparison purposes this is all very safe.
	 */
	sub_num = strtol(sub_str, NULL, 0);
	test_num = strtol(test_str, NULL, 0);

	for (sub = uu_list_first(subsystems); sub != NULL;
	     sub = uu_list_next(subsystems, sub)) {

		if (strncmp(sub->sub_desc.name, sub_str, KZT_NAME_SIZE) &&
		    sub->sub_desc.id != sub_num)
			continue;

		for (test = uu_list_first(sub->sub_tests); test != NULL;
		     test = uu_list_next(sub->sub_tests, test)) {

			if (!strncmp(test->test_desc.name, test_str,
		            KZT_NAME_SIZE) || test->test_desc.id == test_num)
				return test;
		}
        }

	return NULL;
}

static int test_add(cmd_args_t *args, test_t *test)
{
	test_t *tmp;

	tmp = test_copy(test);
	if (tmp == NULL)
		return -ENOMEM;

	uu_list_insert(args->args_tests, tmp, 0);
	return 0;
}

static int test_add_all(cmd_args_t *args)
{
	subsystem_t *sub;
	test_t *test;
	int rc;

	for (sub = uu_list_first(subsystems); sub != NULL;
	     sub = uu_list_next(subsystems, sub)) {

		for (test = uu_list_first(sub->sub_tests); test != NULL;
		     test = uu_list_next(sub->sub_tests, test)) {

			if (rc = test_add(args, test))
				return rc;
		}
        }

	return 0;
}

static int test_run(cmd_args_t *args, test_t *test)
{
	subsystem_t *sub = test->test_sub;
	kzt_cmd_t *cmd;
	int rc, cmd_size;

	dev_clear();

	cmd_size = sizeof(*cmd);
	cmd = (kzt_cmd_t *)malloc(cmd_size);
	if (cmd == NULL)
		return -ENOMEM;

	memset(cmd, 0, cmd_size);
	cmd->cmd_magic = KZT_CMD_MAGIC;
        cmd->cmd_subsystem = sub->sub_desc.id;
	cmd->cmd_test = test->test_desc.id;
	cmd->cmd_data_size = 0; /* Unused feature */

	fprintf(stdout, "%*s:%-*s ",
	        KZT_NAME_SIZE, sub->sub_desc.name,
	        KZT_NAME_SIZE, test->test_desc.name);
	fflush(stdout);
	rc = ioctl(kztctl_fd, KZT_CMD, cmd);
	if (args->args_do_color) {
		fprintf(stdout, "%s  %s\n", rc ?
		        COLOR_RED "Fail" COLOR_RESET :
		        COLOR_GREEN "Pass" COLOR_RESET,
			rc ? strerror(errno) : "");
	} else {
		fprintf(stdout, "%s  %s\n", rc ?
		        "Fail" : "Pass",
			rc ? strerror(errno) : "");
	}
	fflush(stdout);
	free(cmd);

	if (args->args_verbose) {
		if ((rc = read(kztctl_fd, kzt_buffer, kzt_buffer_size - 1)) < 0) {
			fprintf(stdout, "Error reading results: %d\n", rc);
		} else {
			fprintf(stdout, "\n%s\n", kzt_buffer);
			fflush(stdout);
		}
	}

	return rc;
}

static int tests_run(cmd_args_t *args)
{
	test_t *test;
	int rc;

	fprintf(stdout,
	        "------------------------------- "
	        "Running KZT Tests "
	        "-------------------------------\n");

	for (test = uu_list_first(args->args_tests); test != NULL;
	     test = uu_list_next(args->args_tests, test)) {

		rc = test_run(args, test);
		if (rc && args->args_exit_on_error)
			return rc;
	}

	return 0;
}

static int args_parse_test(cmd_args_t *args, char *str)
{
	subsystem_t *s;
	test_t *t;
	char *sub_str, *test_str;
	int sub_num, test_num;
	int sub_all = 0, test_all = 0;
	int rc, flag = 0;

	test_str = strchr(str, ':');
	if (test_str == NULL) {
		fprintf(stderr, "Test must be of the "
		        "form <subsystem:test>\n");
		return -EINVAL;
	}

	sub_str = str;
	test_str[0] = '\0';
	test_str = test_str + 1;

	sub_num = strtol(sub_str, NULL, 0);
	test_num = strtol(test_str, NULL, 0);

	if (!strncasecmp(sub_str, "all", strlen(sub_str)) || (sub_num == -1))
		sub_all = 1;

	if (!strncasecmp(test_str, "all", strlen(test_str)) || (test_num == -1))
		test_all = 1;

	if (sub_all) {
		if (test_all) {
			/* Add all tests from all subsystems */
			for (s = uu_list_first(subsystems); s != NULL;
			     s = uu_list_next(subsystems, s))
				for (t = uu_list_first(s->sub_tests);t != NULL;
				     t = uu_list_next(s->sub_tests, t))
					if (rc = test_add(args, t))
						goto error_run;
		} else {
			/* Add a specific test from all subsystems */
			for (s = uu_list_first(subsystems); s != NULL;
			     s = uu_list_next(subsystems, s)) {
				if (t = test_find(s->sub_desc.name,test_str)) {
					if (rc = test_add(args, t))
						goto error_run;

					flag = 1;
				}
			}

			if (!flag)
				fprintf(stderr, "No tests '%s:%s' could be "
				        "found\n", sub_str, test_str);
		}
	} else {
		if (test_all) {
			/* Add all tests from a specific subsystem */
			for (s = uu_list_first(subsystems); s != NULL;
			     s = uu_list_next(subsystems, s)) {
				if (strncasecmp(sub_str, s->sub_desc.name,
				    strlen(sub_str)))
					continue;

				for (t = uu_list_first(s->sub_tests);t != NULL;
				     t = uu_list_next(s->sub_tests, t))
					if (rc = test_add(args, t))
						goto error_run;
			}
		} else {
			/* Add a specific test from a specific subsystem */
			if (t = test_find(sub_str, test_str)) {
				if (rc = test_add(args, t))
					goto error_run;
			} else {
				fprintf(stderr, "Test '%s:%s' could not be "
				        "found\n", sub_str, test_str);
				return -EINVAL;
			}
		}
	}

	return 0;

error_run:
	fprintf(stderr, "Test '%s:%s' not added to run list: %d\n",
	        sub_str, test_str, rc);
	return rc;
}

static void args_fini(cmd_args_t *args)
{
	struct cmd_test *ptr1, *ptr2;

	assert(args != NULL);



	if (args->args_tests != NULL) {
		uu_list_destroy(args->args_tests);
	}

	free(args);
}

static cmd_args_t *
args_init(int argc, char **argv)
{
	cmd_args_t *args;
	int c, rc;

	if (argc == 1) {
		usage();
		return (cmd_args_t *) NULL;
	}

	/* Configure and populate the args structures */
	args = malloc(sizeof(*args));
	if (args == NULL)
		return NULL;

	memset(args, 0, sizeof(*args));
	args->args_verbose = 0;
	args->args_do_list = 0;
	args->args_do_all  = 0;
	args->args_do_color = 1;
	args->args_exit_on_error = 0;
	args->args_tests = uu_list_create(test_pool, NULL, 0);
	if (args->args_tests == NULL) {
		args_fini(args);
		return NULL;
	}

	while ((c = getopt_long(argc, argv, shortOpts, longOpts, NULL)) != -1){
		switch (c) {
		case 'v':  args->args_verbose++;			break;
		case 'l':  args->args_do_list = 1;			break;
		case 'a':  args->args_do_all = 1;			break;
		case 'c':  args->args_do_color = 0;			break;
		case 'x':  args->args_exit_on_error = 1;		break;
		case 't':
			if (args->args_do_all) {
				fprintf(stderr, "Option -t <subsystem:test> is "
				        "useless when used with -a\n");
				args_fini(args);
				return NULL;
			}

			rc = args_parse_test(args, argv[optind - 1]);
			if (rc) {
				args_fini(args);
				return NULL;
			}
			break;
		case 'h':
		case '?':
			usage();
			args_fini(args);
			return NULL;
		default:
			fprintf(stderr, "Unknown option '%s'\n",
			        argv[optind - 1]);
			break;
		}
	}

	return args;
}

static int
dev_clear(void)
{
	kzt_cfg_t cfg;
	int rc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_magic = KZT_CFG_MAGIC;
        cfg.cfg_cmd   = KZT_CFG_BUFFER_CLEAR;
	cfg.cfg_arg1  = 0;

	rc = ioctl(kztctl_fd, KZT_CFG, &cfg);
	if (rc)
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) KZT_CFG, cfg.cfg_cmd, errno);

	lseek(kztctl_fd, 0, SEEK_SET);

	return rc;
}

static int
dev_size(int size)
{
	kzt_cfg_t cfg;
	int rc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_magic = KZT_CFG_MAGIC;
        cfg.cfg_cmd   = KZT_CFG_BUFFER_SIZE;
	cfg.cfg_arg1  = size;

	rc = ioctl(kztctl_fd, KZT_CFG, &cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) KZT_CFG, cfg.cfg_cmd, errno);
		return rc;
	}

	return cfg.cfg_rc1;
}

static void
dev_fini(void)
{
	if (kzt_buffer)
		free(kzt_buffer);

	if (kztctl_fd != -1) {
		if (close(kztctl_fd) == -1) {
			fprintf(stderr, "Unable to close %s: %d\n",
		                KZT_DEV, errno);
		}
	}
}

static int
dev_init(void)
{
	subsystem_t *sub;
	int rc;

	kztctl_fd = open(KZT_DEV, O_RDONLY);
	if (kztctl_fd == -1) {
		fprintf(stderr, "Unable to open %s: %d\n"
		        "Is the kzt module loaded?\n", KZT_DEV, errno);
		rc = errno;
		goto error;
	}

	/* Determine kernel module version string */
	memset(kzt_version, 0, KZT_VERSION_SIZE);
	if ((rc = read(kztctl_fd, kzt_version, KZT_VERSION_SIZE - 1)) == -1)
		goto error;

	if (rc = dev_clear())
		goto error;

	if ((rc = dev_size(0)) < 0)
		goto error;

	kzt_buffer_size = rc;
	kzt_buffer = (char *)malloc(kzt_buffer_size);
	if (kzt_buffer == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	memset(kzt_buffer, 0, kzt_buffer_size);

	/* Determine available subsystems */
	if ((rc = subsystem_setup()) != 0)
		goto error;

	/* Determine available tests for all subsystems */
	for (sub = uu_list_first(subsystems); sub != NULL;
	     sub = uu_list_next(subsystems, sub))
		if ((rc = test_setup(sub)) != 0)
			goto error;

	return 0;

error:
	if (kztctl_fd != -1) {
		if (close(kztctl_fd) == -1) {
			fprintf(stderr, "Unable to close %s: %d\n",
		                KZT_DEV, errno);
		}
	}

	return rc;
}

int
init(void)
{
	int rc;

	/* Configure the subsystem pool */
	subsystem_pool = uu_list_pool_create("sub_pool", sizeof(subsystem_t),
			                     offsetof(subsystem_t, sub_node),
			                     subsystem_compare, 0);
	if (subsystem_pool == NULL)
		return -ENOMEM;

	/* Configure the test pool */
	test_pool = uu_list_pool_create("test_pool", sizeof(test_t),
			                offsetof(test_t, test_node),
			                test_compare, 0);
	if (test_pool == NULL) {
		uu_list_pool_destroy(subsystem_pool);
		return -ENOMEM;
	}

	/* Allocate the subsystem list */
	subsystems = uu_list_create(subsystem_pool, NULL, 0);
	if (subsystems == NULL) {
		uu_list_pool_destroy(test_pool);
		uu_list_pool_destroy(subsystem_pool);
		return -ENOMEM;
	}

	return 0;
}

void
fini(void)
{
	/* XXX - Cleanup destroy lists release memory */

	/* XXX - Remove contents of list first */
	uu_list_destroy(subsystems);
}


int
main(int argc, char **argv)
{
	cmd_args_t *args = NULL;
	int rc = 0;

	/* General init */
	if (rc = init())
		return rc;

	/* Device specific init */
	if (rc = dev_init())
		goto out;

	/* Argument init and parsing */
	if ((args = args_init(argc, argv)) == NULL) {
		rc = -1;
		goto out;
	}

	/* Generic kernel version string */
	if (args->args_verbose)
		fprintf(stdout, "%s", kzt_version);

	/* Print the available test list and exit */
	if (args->args_do_list) {
		subsystem_list(subsystems, 0);
		goto out;
	}

	/* Add all available test to the list of tests to run */
	if (args->args_do_all) {
		if (rc = test_add_all(args))
			goto out;
	}

	/* Run all the requested tests */
	if (rc = tests_run(args))
		goto out;

out:
	if (args != NULL)
		args_fini(args);

	dev_fini();
	fini();
	return rc;
}

