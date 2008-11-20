/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)zdump.c	1.15	06/12/06 SMI"

/*
 * zdump 7.24
 * Taken from elsie.nci.nih.gov to replace the existing Solaris zdump,
 * which was based on an earlier version of the elsie code.
 *
 * For zdump 7.24, the following changes were made to the elsie code:
 *   locale/textdomain/messages to match existing Solaris style.
 *   Solaris verbose mode is documented to display the current time first.
 *   cstyle cleaned code.
 *   removed old locale/textdomain code.
 */

static char	elsieid[] = "@(#)zdump.c	7.74";

/*
 * This code has been made independent of the rest of the time
 * conversion package to increase confidence in the verification it provides.
 * You can use this code to help in verifying other implementations.
 */

#include "stdio.h"	/* for stdout, stderr, perror */
#include "string.h"	/* for strcpy */
#include "sys/types.h"	/* for time_t */
#include "time.h"	/* for struct tm */
#include "stdlib.h"	/* for exit, malloc, atoi */
#include "locale.h"	/* for setlocale, textdomain */
#include "libintl.h"
#include <ctype.h>
#include "tzfile.h"	/* for defines */
#include <limits.h>

#ifndef ZDUMP_LO_YEAR
#define	ZDUMP_LO_YEAR	(-500)
#endif /* !defined ZDUMP_LO_YEAR */

#ifndef ZDUMP_HI_YEAR
#define	ZDUMP_HI_YEAR	2500
#endif /* !defined ZDUMP_HI_YEAR */

#ifndef MAX_STRING_LENGTH
#define	MAX_STRING_LENGTH	1024
#endif /* !defined MAX_STRING_LENGTH */

#ifndef TRUE
#define	TRUE		1
#endif /* !defined TRUE */

#ifndef FALSE
#define	FALSE		0
#endif /* !defined FALSE */

#ifndef isleap_sum
/*
 * See tzfile.h for details on isleap_sum.
 */
#define	isleap_sum(a, b)	isleap((a) % 400 + (b) % 400)
#endif /* !defined isleap_sum */

#ifndef SECSPERDAY
#define	SECSPERDAY	((long)SECSPERHOUR * HOURSPERDAY)
#endif
#define	SECSPERNYEAR	(SECSPERDAY * DAYSPERNYEAR)
#define	SECSPERLYEAR	(SECSPERNYEAR + SECSPERDAY)

#ifndef GNUC_or_lint
#ifdef lint
#define	GNUC_or_lint
#else /* !defined lint */
#ifdef __GNUC__
#define	GNUC_or_lint
#endif /* defined __GNUC__ */
#endif /* !defined lint */
#endif /* !defined GNUC_or_lint */

#ifndef INITIALIZE
#ifdef	GNUC_or_lint
#define	INITIALIZE(x)	((x) = 0)
#else /* !defined GNUC_or_lint */
#define	INITIALIZE(x)
#endif /* !defined GNUC_or_lint */
#endif /* !defined INITIALIZE */

static time_t	absolute_min_time;
static time_t	absolute_max_time;
static size_t	longest;
static char	*progname;
static int	warned;

static char	*abbr(struct tm *);
static void	abbrok(const char *, const char *);
static long	delta(struct tm *, struct tm *);
static void	dumptime(const struct tm *);
static time_t	hunt(char *, time_t, time_t);
static void	setabsolutes(void);
static void	show(char *, time_t, int);
static void	usage(void);
static const char	*tformat(void);
static time_t	yeartot(long y);

#ifndef TYPECHECK
#define	my_localtime	localtime
#else /* !defined TYPECHECK */
static struct tm *
my_localtime(tp)
time_t *tp;
{
	register struct tm *tmp;

	tmp = localtime(tp);
	if (tp != NULL && tmp != NULL) {
		struct tm	tm;
		register time_t	t;

		tm = *tmp;
		t = mktime(&tm);
		if (t - *tp >= 1 || *tp - t >= 1) {
			(void) fflush(stdout);
			(void) fprintf(stderr, "\n%s: ", progname);
			(void) fprintf(stderr, tformat(), *tp);
			(void) fprintf(stderr, " ->");
			(void) fprintf(stderr, " year=%d", tmp->tm_year);
			(void) fprintf(stderr, " mon=%d", tmp->tm_mon);
			(void) fprintf(stderr, " mday=%d", tmp->tm_mday);
			(void) fprintf(stderr, " hour=%d", tmp->tm_hour);
			(void) fprintf(stderr, " min=%d", tmp->tm_min);
			(void) fprintf(stderr, " sec=%d", tmp->tm_sec);
			(void) fprintf(stderr, " isdst=%d", tmp->tm_isdst);
			(void) fprintf(stderr, " -> ");
			(void) fprintf(stderr, tformat(), t);
			(void) fprintf(stderr, "\n");
		}
	}
	return (tmp);
}
#endif /* !defined TYPECHECK */

static void
abbrok(abbrp, zone)
const char * const	abbrp;
const char * const	zone;
{
	register const char *cp;
	int error = 0;

	if (warned)
		return;
	cp = abbrp;
	while (isascii(*cp) && isalpha((unsigned char)*cp))
		++cp;
	(void) fflush(stdout);
	if (cp - abbrp == 0) {
		/*
		 * TRANSLATION_NOTE
		 * The first %s prints for the program name (zdump),
		 * the second %s prints the timezone name, the third
		 * %s prints the timezone abbreviation (tzname[0] or
		 * tzname[1]).
		 */
		(void) fprintf(stderr, gettext("%s: warning: zone \"%s\" "
		    "abbreviation \"%s\" lacks alphabetic at start\n"),
		    progname, zone, abbrp);
		error = 1;
	} else if (cp - abbrp < 3) {
		(void) fprintf(stderr, gettext("%s: warning: zone \"%s\" "
		    "abbreviation \"%s\" has fewer than 3 alphabetics\n"),
		    progname, zone, abbrp);
		error = 1;
	} else if (cp - abbrp > 6) {
		(void) fprintf(stderr, gettext("%s: warning: zone \"%s\" "
		    "abbreviation \"%s\" has more than 6 alphabetics\n"),
		    progname, zone, abbrp);
		error = 1;
	}
	if (error == 0 && (*cp == '+' || *cp == '-')) {
		++cp;
		if (isascii(*cp) && isdigit((unsigned char)*cp))
			if (*cp++ == '1' && *cp >= '0' && *cp <= '4')
				++cp;
		if (*cp != '\0') {
			(void) fprintf(stderr, gettext("%s: warning: "
			    "zone \"%s\" abbreviation \"%s\" differs from "
			    "POSIX standard\n"), progname, zone, abbrp);
			error = 1;
		}
	}
	if (error)
		warned = TRUE;
}

int
main(argc, argv)
int	argc;
char	*argv[];
{
	register int		i;
	register int		c;
	register int		vflag;
	register char		*cutarg;
	register long		cutloyear = ZDUMP_LO_YEAR;
	register long		cuthiyear = ZDUMP_HI_YEAR;
	register time_t		cutlotime;
	register time_t		cuthitime;
	time_t			now;
	time_t			t;
	time_t			newt;
	struct tm		tm;
	struct tm		newtm;
	register struct tm	*tmp;
	register struct tm	*newtmp;

	INITIALIZE(cutlotime);
	INITIALIZE(cuthitime);

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = argv[0];
	for (i = 1; i < argc; ++i)
		if (strcmp(argv[i], "--version") == 0) {
			(void) printf("%s\n", elsieid);
			exit(EXIT_SUCCESS);
		}
	vflag = 0;
	cutarg = NULL;
	while ((c = getopt(argc, argv, "c:v")) == 'c' || c == 'v')
		if (c == 'v')
			vflag = 1;
		else	cutarg = optarg;
	if (c != EOF ||
		(optind == argc - 1 && strcmp(argv[optind], "=") == 0)) {
			usage();
			/* NOTREACHED */
	}
	if (vflag) {
		if (cutarg != NULL) {
			long	lo;
			long	hi;
			char	dummy;

			if (sscanf(cutarg, "%ld%c", &hi, &dummy) == 1) {
				cuthiyear = hi;
			} else if (sscanf(cutarg, "%ld,%ld%c",
				&lo, &hi, &dummy) == 2) {
					cutloyear = lo;
					cuthiyear = hi;
			} else {
(void) fprintf(stderr, gettext("%s: wild -c argument %s\n"),
					progname, cutarg);
				exit(EXIT_FAILURE);
			}
		}
		setabsolutes();
		cutlotime = yeartot(cutloyear);
		cuthitime = yeartot(cuthiyear);
	}
	(void) time(&now);
	longest = 0;
	for (i = optind; i < argc; ++i)
		if (strlen(argv[i]) > longest)
			longest = strlen(argv[i]);

	for (i = optind; i < argc; ++i) {
		static char	buf[MAX_STRING_LENGTH];
		static char	*tzp = NULL;

		(void) unsetenv("TZ");
		if (tzp != NULL)
			free(tzp);
		if ((tzp = malloc(3 + strlen(argv[i]) + 1)) == NULL) {
			perror(progname);
			exit(EXIT_FAILURE);
		}
		(void) strcpy(tzp, "TZ=");
		(void) strcat(tzp, argv[i]);
		if (putenv(tzp) != 0) {
			perror(progname);
			exit(EXIT_FAILURE);
		}
		if (!vflag) {
			show(argv[i], now, FALSE);
			continue;
		}

#if defined(sun)
		/*
		 * We show the current time first, probably because we froze
		 * the behavior of zdump some time ago and then it got
		 * changed.
		 */
		show(argv[i], now, TRUE);
#endif
		warned = FALSE;
		t = absolute_min_time;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		if (t < cutlotime)
			t = cutlotime;
		tmp = my_localtime(&t);
		if (tmp != NULL) {
			tm = *tmp;
			(void) strncpy(buf, abbr(&tm), sizeof (buf) - 1);
		}
		for (;;) {
			if (t >= cuthitime)
				break;
			/* check if newt will overrun maximum time_t value */
			if (t > LONG_MAX - (SECSPERHOUR * 12))
				break;
			newt = t + SECSPERHOUR * 12;
			if (newt >= cuthitime)
				break;
			newtmp = localtime(&newt);
			if (newtmp != NULL)
				newtm = *newtmp;
			if ((tmp == NULL || newtmp == NULL) ? (tmp != newtmp) :
				(delta(&newtm, &tm) != (newt - t) ||
				newtm.tm_isdst != tm.tm_isdst ||
				strcmp(abbr(&newtm), buf) != 0)) {
					newt = hunt(argv[i], t, newt);
					newtmp = localtime(&newt);
					if (newtmp != NULL) {
						newtm = *newtmp;
						(void) strncpy(buf,
							abbr(&newtm),
							sizeof (buf) - 1);
					}
			}
			t = newt;
			tm = newtm;
			tmp = newtmp;
		}
		t = absolute_max_time;
#if defined(sun)
		show(argv[i], t, TRUE);
		t -= SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
#else /* !defined(sun) */
		t -= SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
#endif /* !defined(sun) */
	}
	if (fflush(stdout) || ferror(stdout)) {
		(void) fprintf(stderr, "%s: ", progname);
		(void) perror(gettext("Error writing standard output"));
		exit(EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static void
setabsolutes()
{
#if defined(sun)
	absolute_min_time = LONG_MIN;
	absolute_max_time = LONG_MAX;
#else
	if (0.5 == (time_t)0.5) {
		/*
		 * time_t is floating.
		 */
		if (sizeof (time_t) == sizeof (float)) {
			absolute_min_time = (time_t)-FLT_MAX;
			absolute_max_time = (time_t)FLT_MAX;
		} else if (sizeof (time_t) == sizeof (double)) {
			absolute_min_time = (time_t)-DBL_MAX;
			absolute_max_time = (time_t)DBL_MAX;
		} else {
			(void) fprintf(stderr, gettext("%s: use of -v on "
			    "system with floating time_t other than float "
			    "or double\n"), progname);
			exit(EXIT_FAILURE);
		}
	} else
	/*CONSTANTCONDITION*/
	if (0 > (time_t)-1) {
		/*
		 * time_t is signed.
		 */
		register time_t	hibit;

		for (hibit = 1; (hibit * 2) != 0; hibit *= 2)
			continue;
		absolute_min_time = hibit;
		absolute_max_time = -(hibit + 1);
	} else {
		/*
		 * time_t is unsigned.
		 */
		absolute_min_time = 0;
		absolute_max_time = absolute_min_time - 1;
	}
#endif
}

static time_t
yeartot(y)
const long	y;
{
	register long	myy;
	register long	seconds;
	register time_t	t;

	myy = EPOCH_YEAR;
	t = 0;
	while (myy != y) {
		if (myy < y) {
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			++myy;
			if (t > absolute_max_time - seconds) {
				t = absolute_max_time;
				break;
			}
			t += seconds;
		} else {
			--myy;
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			if (t < absolute_min_time + seconds) {
				t = absolute_min_time;
				break;
			}
			t -= seconds;
		}
	}
	return (t);
}

static time_t
hunt(name, lot, hit)
char	*name;
time_t	lot;
time_t	hit;
{
	time_t			t;
	long			diff;
	struct tm		lotm;
	register struct tm	*lotmp;
	struct tm		tm;
	register struct tm	*tmp;
	char			loab[MAX_STRING_LENGTH];

	lotmp = my_localtime(&lot);
	if (lotmp != NULL) {
		lotm = *lotmp;
		(void) strncpy(loab, abbr(&lotm), sizeof (loab) - 1);
	}
	for (;;) {
		diff = (long)(hit - lot);
		if (diff < 2)
			break;
		t = lot;
		t += diff / 2;
		if (t <= lot)
			++t;
		else if (t >= hit)
			--t;
		tmp = my_localtime(&t);
		if (tmp != NULL)
			tm = *tmp;
		if ((lotmp == NULL || tmp == NULL) ? (lotmp == tmp) :
			(delta(&tm, &lotm) == (t - lot) &&
			tm.tm_isdst == lotm.tm_isdst &&
			strcmp(abbr(&tm), loab) == 0)) {
				lot = t;
				lotm = tm;
				lotmp = tmp;
		} else	hit = t;
	}
	show(name, lot, TRUE);
	show(name, hit, TRUE);
	return (hit);
}

/*
 * Thanks to Paul Eggert for logic used in delta.
 */

static long
delta(newp, oldp)
struct tm	*newp;
struct tm	*oldp;
{
	register long	result;
	register int	tmy;

	if (newp->tm_year < oldp->tm_year)
		return (-delta(oldp, newp));
	result = 0;
	for (tmy = oldp->tm_year; tmy < newp->tm_year; ++tmy)
		result += DAYSPERNYEAR + isleap_sum(tmy, TM_YEAR_BASE);
	result += newp->tm_yday - oldp->tm_yday;
	result *= HOURSPERDAY;
	result += newp->tm_hour - oldp->tm_hour;
	result *= MINSPERHOUR;
	result += newp->tm_min - oldp->tm_min;
	result *= SECSPERMIN;
	result += newp->tm_sec - oldp->tm_sec;
	return (result);
}

static void
show(zone, t, v)
char	*zone;
time_t	t;
int	v;
{
	register struct tm	*tmp;

	(void) printf("%-*s  ", (int)longest, zone);
	if (v) {
		tmp = gmtime(&t);
		if (tmp == NULL) {
			(void) printf(tformat(), t);
		} else {
			dumptime(tmp);
			(void) printf(" UTC");
		}
		(void) printf(" = ");
	}
	tmp = my_localtime(&t);
	dumptime(tmp);
	if (tmp != NULL) {
		if (*abbr(tmp) != '\0')
			(void) printf(" %s", abbr(tmp));
		if (v) {
			(void) printf(" isdst=%d", tmp->tm_isdst);
#ifdef TM_GMTOFF
			(void) printf(" gmtoff=%ld", tmp->TM_GMTOFF);
#endif /* defined TM_GMTOFF */
		}
	}
	(void) printf("\n");
	if (tmp != NULL && *abbr(tmp) != '\0')
		abbrok(abbr(tmp), zone);
}

static char *
abbr(tmp)
struct tm	*tmp;
{
	register char	*result;
	static char	nada;

	if (tmp->tm_isdst != 0 && tmp->tm_isdst != 1)
		return (&nada);
	result = tzname[tmp->tm_isdst];
	return ((result == NULL) ? &nada : result);
}

/*
 * The code below can fail on certain theoretical systems;
 * it works on all known real-world systems as of 2004-12-30.
 */

static const char *
tformat()
{
#if defined(sun)
	/* time_t is signed long */
	return ("%ld");
#else
	/*CONSTANTCONDITION*/
	if (0.5 == (time_t)0.5) {	/* floating */
		/*CONSTANTCONDITION*/
		if (sizeof (time_t) > sizeof (double))
			return ("%Lg");
		return ("%g");
	}
	/*CONSTANTCONDITION*/
	if (0 > (time_t)-1) {		/* signed */
		/*CONSTANTCONDITION*/
		if (sizeof (time_t) > sizeof (long))
			return ("%lld");
		/*CONSTANTCONDITION*/
		if (sizeof (time_t) > sizeof (int))
			return ("%ld");
		return ("%d");
	}
	/*CONSTANTCONDITION*/
	if (sizeof (time_t) > sizeof (unsigned long))
		return ("%llu");
	/*CONSTANTCONDITION*/
	if (sizeof (time_t) > sizeof (unsigned int))
		return ("%lu");
	return ("%u");
#endif
}

static void
dumptime(timeptr)
register const struct tm	*timeptr;
{
	static const char	wday_name[][3] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char	mon_name[][3] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	register const char	*wn;
	register const char	*mn;
	register int		lead;
	register int		trail;

	if (timeptr == NULL) {
		(void) printf("NULL");
		return;
	}
	/*
	 * The packaged versions of localtime and gmtime never put out-of-range
	 * values in tm_wday or tm_mon, but since this code might be compiled
	 * with other (perhaps experimental) versions, paranoia is in order.
	 */
	if (timeptr->tm_wday < 0 || timeptr->tm_wday >=
		(int)(sizeof (wday_name) / sizeof (wday_name[0])))
			wn = "???";
	else		wn = wday_name[timeptr->tm_wday];
	if (timeptr->tm_mon < 0 || timeptr->tm_mon >=
		(int)(sizeof (mon_name) / sizeof (mon_name[0])))
			mn = "???";
	else		mn = mon_name[timeptr->tm_mon];
	(void) printf("%.3s %.3s%3d %.2d:%.2d:%.2d ",
		wn, mn,
		timeptr->tm_mday, timeptr->tm_hour,
		timeptr->tm_min, timeptr->tm_sec);
#define	DIVISOR	10
	trail = timeptr->tm_year % DIVISOR + TM_YEAR_BASE % DIVISOR;
	lead = timeptr->tm_year / DIVISOR + TM_YEAR_BASE / DIVISOR +
		trail / DIVISOR;
	trail %= DIVISOR;
	if (trail < 0 && lead > 0) {
		trail += DIVISOR;
		--lead;
	} else if (lead < 0 && trail > 0) {
		trail -= DIVISOR;
		++lead;
	}
	if (lead == 0)
		(void) printf("%d", trail);
	else
		(void) printf("%d%d", lead, ((trail < 0) ? -trail : trail));
}

static void
usage()
{
	(void) fprintf(stderr, gettext(
	    "%s: [ --version ] [ -v ] [ -c [loyear,]hiyear ] zonename ...\n"),
		progname);
	exit(EXIT_FAILURE);
	/* NOTREACHED */
}
