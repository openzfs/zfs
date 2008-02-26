#ifndef _SPLAT_H
#define _SPLAT_H

#include "list.h"
#include "splat-ctl.h"

#define DEV_NAME			"/dev/kztctl"
#define COLOR_BLACK			"\033[0;30m"
#define COLOR_DK_GRAY			"\033[1;30m"
#define COLOR_BLUE			"\033[0;34m"
#define COLOR_LT_BLUE			"\033[1;34m"
#define COLOR_GREEN			"\033[0;32m"
#define COLOR_LT_GREEN			"\033[1;32m"
#define COLOR_CYAN			"\033[0;36m"
#define COLOR_LT_CYAN			"\033[1;36m"
#define COLOR_RED			"\033[0;31m"
#define COLOR_LT_RED			"\033[1;31m"
#define COLOR_PURPLE			"\033[0;35m"
#define COLOR_LT_PURPLE			"\033[1;35m"
#define COLOR_BROWN			"\033[0;33m"
#define COLOR_YELLOW			"\033[1;33m"
#define COLOR_LT_GRAY			"\033[0;37m"
#define COLOR_WHITE			"\033[1;37m"
#define COLOR_RESET			"\033[0m"

typedef struct subsystem {
	kzt_user_t sub_desc;		/* Subsystem description */
	List sub_tests;			/* Assocated subsystem tests list */
} subsystem_t;

typedef struct test {
	kzt_user_t test_desc;		/* Test description */
	subsystem_t *test_sub;		/* Parent subsystem */
} test_t;

typedef struct cmd_args {
	int args_verbose;		/* Verbose flag */
	int args_do_list;		/* Display all tests flag */
	int args_do_all;		/* Run all tests flag */
	int args_do_color;		/* Colorize output */
	int args_exit_on_error;		/* Exit on first error flag */
	List args_tests;		/* Requested subsystems/tests */
} cmd_args_t;

#endif /* _SPLAT_H */

