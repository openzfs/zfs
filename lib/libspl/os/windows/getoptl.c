
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

char *optarg;
int optind, opterr, optopt;
static struct getopt_private_state {
	const char *optptr;
	const char *last_optstring;
	char *const *last_argv;
} pvt;

static inline const char *option_matches(const char *arg_str,
	const char *opt_name)
{
	while (*arg_str != '\0' && *arg_str != '=') {
		if (*arg_str++ != *opt_name++)
			return NULL;
	}

	if (*opt_name)
		return NULL;

	return arg_str;
}

int getopt_long(int argc, char *const *argv, const char *optstring,
	const struct option *longopts, int *longindex)
{
	const char *carg;
	const char *osptr;
	int opt;

	/* getopt() relies on a number of different global state
	variables, which can make this really confusing if there is
	more than one use of getopt() in the same program.  This
	attempts to detect that situation by detecting if the
	"optstring" or "argv" argument have changed since last time
	we were called; if so, reinitialize the query state. */

	if (optstring != pvt.last_optstring || argv != pvt.last_argv ||
		optind < 1 || optind > argc) {
		/* optind doesn't match the current query */
		pvt.last_optstring = optstring;
		pvt.last_argv = argv;
		optind = 1;
		pvt.optptr = NULL;
	}

	carg = argv[optind];

	/* First, eliminate all non-option cases */

	if (!carg || carg[0] != '-' || !carg[1])
		return -1;

	if (carg[1] == '-') {
		const struct option *lo;
		const char *opt_end = NULL;

		optind++;

		/* Either it's a long option, or it's -- */
		if (!carg[2]) {
			/* It's -- */
			return -1;
		}

		for (lo = longopts; lo->name; lo++) {
			if ((opt_end = option_matches(carg + 2, lo->name)))
				break;
		}
		if (!opt_end)
			return '?';

		if (longindex)
			*longindex = lo - longopts;

		if (*opt_end == '=') {
			if (lo->has_arg)
				optarg = (char *)opt_end + 1;
			else
				return '?';
		} else if (lo->has_arg == 1) {
			if (!(optarg = argv[optind]))
				return '?';
			optind++;
		}

		if (lo->flag) {
			*lo->flag = lo->val;
			return 0;
		} else {
			return lo->val;
		}
	}

	if ((uintptr_t)(pvt.optptr - carg) > (uintptr_t)strlen(carg)) {
		/* Someone frobbed optind, change to new opt. */
		pvt.optptr = carg + 1;
	}

	opt = *pvt.optptr++;

	if (opt != ':' && (osptr = strchr(optstring, opt))) {
		if (osptr[1] == ':') {
			if (*pvt.optptr) {
				/* Argument-taking option with attached
				argument */
				optarg = (char *)pvt.optptr;
				optind++;
			} else {
				/* Argument-taking option with non-attached
				argument */
				if (argv[optind + 1]) {
					optarg = (char *)argv[optind + 1];
					optind += 2;
				} else {
					/* Missing argument */
					optind++;
					return (optstring[0] == ':')
						? ':' : '?';
				}
			}
			return opt;
		} else {
			/* Non-argument-taking option */
			/* pvt.optptr will remember the exact position to
			resume at */
			if (!*pvt.optptr)
				optind++;
			return opt;
		}
	} else {
		/* Unknown option */
		optopt = opt;
		if (!*pvt.optptr)
			optind++;
		return '?';
	}
}
