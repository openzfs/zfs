
/*
 * Copyright (c) 1999-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/gmon.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#ifdef __APPLE__
#include <mach/machine/vm_param.h>
#include <mach/machine/vm_types.h>
#include <mach/mach_types.h>
#else
#include <vm/vm_param.h>
#endif /* __APPLE__ */

#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/resource.h>
#include <err.h>


#pragma GCC diagnostic push
#pragma GCC diagnostic \
	ignored "-Wincompatible-pointer-types-discards-qualifiers"

struct ctlname topname[] = CTL_NAMES;
struct ctlname kernname[] = CTL_KERN_NAMES;
struct ctlname vmname[] = CTL_VM_NAMES;
struct ctlname hwname[] = CTL_HW_NAMES;
struct ctlname username[] = CTL_USER_NAMES;
struct ctlname debugname[CTL_DEBUG_MAXID];
struct ctlname *vfsname;
#ifdef CTL_MACHDEP_NAMES
struct ctlname machdepname[] = (char *)CTL_MACHDEP_NAMES;
#endif

#pragma GCC diagnostic pop


char names[BUFSIZ];
int lastused;

struct list {
    struct	ctlname *list;
    int	size;
};
struct list toplist = { topname, CTL_MAXID };
struct list secondlevel[] = {
	{ 0, 0 },			/* CTL_UNSPEC */
	{ kernname, KERN_MAXID },	/* CTL_KERN */
	{ vmname, VM_MAXID },		/* CTL_VM */
	{ 0, 0 },			/* CTL_VFS */
	{ 0, 0 },			/* CTL_NET */
	{ 0, CTL_DEBUG_MAXID },		/* CTL_DEBUG */
	{ hwname, HW_MAXID },		/* CTL_HW */
#ifdef CTL_MACHDEP_NAMES
	{ machdepname, CPU_MAXID },	/* CTL_MACHDEP */
#else
	{ 0, 0 },			/* CTL_MACHDEP */
#endif
	{ username, USER_MAXID },	/* CTL_USER_NAMES */
};

static const char *conffile;

static int	Aflag, aflag, bflag, hflag, nflag, wflag, Xflag;
static int	foundSome = 0;
static int	invalid_name_used = 0;

void listall(char *prefix, struct list *lp);
void old_parse(char *string, int flags);
void debuginit(void);
void vfsinit(void);
int  findname(char *string, char *level, char **bufp, struct list *namelist);
void usage(void);

static void 	parse(char *string, int flags);
static int	parsefile(const char *);
static int	oidfmt(int *, int, char *, uint_t *);
static int	show_var(int *, int, int);
static int	sysctl_all(int *oid, int len);
static int	name2oid(char *, int *);

/*
 * Variables requiring special processing.
 */
#define	CLOCK		0x00000001
#define	BOOTTIME	0x00000002
#define	CONSDEV		0x00000004

int
main(argc, argv)
int argc;
char *argv[];
{
	//	extern char *optarg;   // unused
    extern int optind;
    int ch, lvl1;

    while ((ch = getopt(argc, argv, "Aabf:nwX")) != EOF) {
		switch (ch) {
			case 'A': Aflag = 1; break;
			case 'a': aflag = 1; break;
			case 'b': bflag = 1; break;
			case 'h': hflag = 1; break;
			case 'n': nflag = 1; break;
			case 'w': wflag = 1; break;
			case 'f': wflag = 1; conffile = optarg; break;
			case 'X': Xflag = Aflag = 1; break;
			default: usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0 && (Aflag || aflag)) {
		debuginit();
		vfsinit();
		for (lvl1 = 1; lvl1 < CTL_MAXID; lvl1++)
			listall(topname[lvl1].ctl_name, &secondlevel[lvl1]);
		exit(sysctl_all(0, 0));
	}

	if (conffile != NULL) {
		exit(parsefile(conffile));
	}

	if (argc == 0)
		usage();

	for (; *argv != NULL; ++argv)
		parse(*argv, 1);


	exit(invalid_name_used ? 1 : 0);
}

/*
 * List all variables known to the system.
 */
void
listall(prefix, lp)
char *prefix;
struct list *lp;
{
	int lvl2;
	char *cp, name[BUFSIZ];

    if (lp->list == 0)
		return;
	strcpy(name, prefix);
	cp = &name[strlen(name)];
	*cp++ = '.';
	for (lvl2 = 0; lvl2 < lp->size; lvl2++) {
		if (lp->list[lvl2].ctl_name == 0)
			continue;
		strcpy(cp, lp->list[lvl2].ctl_name);
		old_parse(name, Aflag);
	}
}

/*
 * Parse a name into a MIB entry.
 * Lookup and print out the MIB entry if it exists.
 * Set a new value if requested.
 */
void
old_parse(string, flags)
char *string;
int flags;
{
	int indx, type, len;
	size_t size;
	int special = 0;
	void *newval = 0;
	int intval, newsize = 0;
	unsigned int uintval;
	int useUnsignedInt = 0;
	quad_t quadval;
	struct list *lp;
	struct vfsconf vfc;
	int mib[CTL_MAXNAME];
	char *cp, *bufp, buf[BUFSIZ];

	bufp = buf;
	snprintf(buf, BUFSIZ, "%s", string);
	if ((cp = strchr(string, '=')) != NULL) {
		if (!wflag) {
			fprintf(stderr, "Must specify -w to set variables\n");
			exit(2);
		}
		*strchr(buf, '=') = '\0';
		*cp++ = '\0';
		while (isspace(*cp))
			cp++;
		newval = cp;
		newsize = strlen(cp);
	}
	if ((indx = findname(string, (char *)"top", &bufp, &toplist)) == -1)
		return;
	mib[0] = indx;
	if (indx == CTL_VFS)
		vfsinit();
	if (indx == CTL_DEBUG)
		debuginit();
	lp = &secondlevel[indx];
	if (lp->list == 0) {
		if (!foundSome)
			fprintf(stderr, "%s: class is not implemented\n",
			    topname[indx].ctl_name);
		return;
	}
	if (bufp == NULL) {
		listall(topname[indx].ctl_name, lp);
		return;
	}
	if ((indx = findname(string, (char *)"second", &bufp, lp)) == -1)
		return;
	mib[1] = indx;
	type = lp->list[indx].ctl_type;
	len = 2;
	switch (mib[0]) {

		case CTL_KERN:
			switch (mib[1]) {
				case KERN_PROF:
					return;
				case KERN_VNODE:
				case KERN_FILE:
					if (flags == 0)
						return;
				fprintf(stderr,
				    "Use pstat to view %s information\n",
				    string);
					return;
				case KERN_PROC:
					if (flags == 0)
						return;
					fprintf(stderr,
					    "Use ps to view %s information\n",
					    string);
					return;
				case KERN_CLOCKRATE:
					special |= CLOCK;
					break;
				case KERN_BOOTTIME:
					special |= BOOTTIME;
					break;
				case KERN_HOSTID:
					useUnsignedInt = 1;
					break;
			}
			break;

		case CTL_HW:
			useUnsignedInt = 1;
			break;

		case CTL_VM: break;

		case CTL_DEBUG:
			mib[2] = CTL_DEBUG_VALUE;
			len = 3;
			break;

		case CTL_MACHDEP:
#ifdef CPU_CONSDEV
			if (mib[1] == CPU_CONSDEV)
				special |= CONSDEV;
#endif
			break;

		case CTL_VFS:
			mib[3] = mib[1];
			mib[1] = VFS_GENERIC;
			mib[2] = VFS_CONF;
			len = 4;
			size = sizeof (vfc);
			if (sysctl(mib, 4, &vfc, &size, (void *)0,
			    (size_t)0) < 0) {
				perror("vfs print");
				return;
			}
			if (flags == 0 && vfc.vfc_refcount == 0)
				return;
			if (!nflag)
				fprintf(stdout,
				    "%s has %d mounted instance%s\n",
				    string, vfc.vfc_refcount,
				    vfc.vfc_refcount != 1 ? "s" : "");
			else
				fprintf(stdout, "%d\n", vfc.vfc_refcount);
			return;

		case CTL_USER:
			break;

		default:
			fprintf(stderr, "Illegal top level value: %d\n",
			    mib[0]);
			return;

	}
	if (bufp) {
		fprintf(stderr, "name %s in %s is unknown\n", bufp, string);
		return;
	}
	if (newsize > 0) {
		switch (type) {
			case CTLTYPE_INT:
				if (useUnsignedInt) {
					uintval = strtoul(newval, NULL, 0);
					if ((uintval == 0) &&
					    (errno == EINVAL)) {
						fprintf(stderr,
						    "invalid argument: %s\n",
						    (char *)newval);
						return;
					}
					newval = &uintval;
					newsize = sizeof (uintval);
				} else {
					intval = strtol(newval, NULL,  0);
					if ((intval == 0) &&
					    (errno == EINVAL)) {
						fprintf(stderr,
						    "invalid argument: %s\n",
						    (char *)newval);
						return;
					}
					newval = &intval;
					newsize = sizeof (intval);
				}
				break;

			case CTLTYPE_QUAD:
				quadval = strtoq(newval, NULL, 0);
				if ((quadval == 0) && (errno == EINVAL)) {
					fprintf(stderr,
					    "invalid argument: %s\n",
					    (char *)newval);
					return;
				}
				newval = &quadval;
				newsize = sizeof (quadval);
				break;
		}
	}
	size = BUFSIZ;
	if (sysctl(mib, len, buf, &size, newsize ? newval : 0, newsize) == -1) {
		if (flags == 0)
			return;
		switch (errno) {
			case ENOTSUP:
				fprintf(stderr, "%s: value is not available\n",
				    string);
				return;
			case ENOTDIR:
				fprintf(stderr,
				    "%s: specification is incomplete\n",
				    string);
				return;
			case ENOMEM:
				fprintf(stderr,
				    "%s: type is unknown to this program\n",
				    string);
				return;
			case ENOENT:
				fprintf(stderr, "%s: no such MIB\n",
				    string);
				return;
			default:
				perror(string);
				return;
		}
	}
	if (special & CLOCK) {
		struct clockinfo *clkp = (struct clockinfo *)buf;

		if (!nflag)
			fprintf(stdout, "%s: ", string);
		fprintf(stdout,
		    "hz = %d, tick = %d, profhz = %d, stathz = %d\n",
		    clkp->hz, clkp->tick, clkp->profhz, clkp->stathz);
		return;
	}
	if (special & BOOTTIME) {
		struct timeval *btp = (struct timeval *)buf;

		if (!nflag)
			fprintf(stdout, "%s = %s\n", string,
			    ctime((time_t *)&btp->tv_sec));
		else
			fprintf(stdout, "%ld\n", btp->tv_sec);
		return;
	}
	if (special & CONSDEV) {
		dev_t dev = *(dev_t *)buf;

		if (!nflag)
			fprintf(stdout, "%s = %s\n", string,
			    devname(dev, S_IFCHR));
		else
			fprintf(stdout, "0x%x\n", dev);
		return;
	}
	switch (type) {
		case CTLTYPE_INT:
			if (newsize == 0) {
				if (!nflag)
					fprintf(stdout, "%s = ", string);
				fprintf(stdout, useUnsignedInt ? "%u\n" :
				    "%d\n", *(int *)buf);
			} else {
				if (!nflag)
					fprintf(stdout, useUnsignedInt ?
					    "%s: %u -> " : "%s: %d -> ",
					    string, *(int *)buf);
				fprintf(stdout, useUnsignedInt ? "%u\n" :
				    "%d\n", *(int *)newval);
			}
			return;

		case CTLTYPE_STRING:
			if (newsize == 0) {
				if (!nflag)
					fprintf(stdout, "%s = ", string);
				fprintf(stdout, "%s\n", buf);
			} else {
				if (!nflag)
					fprintf(stdout, "%s: %s -> ",
					    string, buf);
				fprintf(stdout, "%s\n", (char *)newval);
			}
			return;

		case CTLTYPE_QUAD:
			if (newsize == 0) {
				if (!nflag)
					fprintf(stdout, "%s = ", string);
				fprintf(stdout, "%qd\n", *(quad_t *)buf);
			} else {
				if (!nflag)
					fprintf(stdout, "%s: %qd -> ", string,
					    *(quad_t *)buf);
				fprintf(stdout, "%qd\n", *(quad_t *)newval);
			}
			return;

		case CTLTYPE_NODE:
		case CTLTYPE_STRUCT:
			return;

		default:
			fprintf(stderr, "%s: unknown type returned\n",
			    string);
			return;
	}
}

/*
 * Initialize the set of debugging names
 */
void
debuginit()
{
	int mib[3], loc, i;
	size_t size;

	if (secondlevel[CTL_DEBUG].list != 0)
		return;
	secondlevel[CTL_DEBUG].list = debugname;
	mib[0] = CTL_DEBUG;
	mib[2] = CTL_DEBUG_NAME;
	for (loc = lastused, i = 0; i < CTL_DEBUG_MAXID; i++) {
		mib[1] = i;
		size = BUFSIZ - loc;
		if (sysctl(mib, 3, &names[loc], &size, NULL, 0) == -1)
			continue;
		debugname[i].ctl_name = &names[loc];
		debugname[i].ctl_type = CTLTYPE_INT;
		loc += size;
	}
	lastused = loc;
}

/*
 * Initialize the set of filesystem names
 */
void
vfsinit()
{
	int mib[4], maxtypenum, cnt, loc, size;
	struct vfsconf vfc;
	size_t buflen;

	if (secondlevel[CTL_VFS].list != 0)
		return;
	mib[0] = CTL_VFS;
	mib[1] = VFS_GENERIC;
	mib[2] = VFS_MAXTYPENUM;
	buflen = 4;
	if (sysctl(mib, 3, &maxtypenum, &buflen, (void *)0, (size_t)0) < 0)
		return;
	if ((vfsname = malloc(maxtypenum * sizeof (*vfsname))) == 0)
		return;
	memset(vfsname, 0, maxtypenum * sizeof (*vfsname));
	mib[2] = VFS_CONF;
	buflen = sizeof (vfc);
	for (loc = lastused, cnt = 0; cnt < maxtypenum; cnt++) {
		mib[3] = cnt;
		if (sysctl(mib, 4, &vfc, &buflen, (void *)0, (size_t)0) < 0) {
			if (errno == ENOTSUP)
				continue;
			perror("vfsinit");
			free(vfsname);
			return;
		}
		strcat(&names[loc], vfc.vfc_name);
		vfsname[cnt].ctl_name = &names[loc];
		vfsname[cnt].ctl_type = CTLTYPE_INT;
		size = strlen(vfc.vfc_name) + 1;
		loc += size;
	}
	lastused = loc;
	secondlevel[CTL_VFS].list = vfsname;
	secondlevel[CTL_VFS].size = maxtypenum;
}

/*
 * Scan a list of names searching for a particular name.
 */
int
findname(string, level, bufp, namelist)
char *string;
char *level;
char **bufp;
struct list *namelist;
{
	char *name;
	int i;

	/* Make 'sysctl kern.' style behave the same as 'sysctl kern' 3360872 */
	if (bufp[0][strlen(*bufp)-1] == '.')
		bufp[0][strlen(*bufp)-1] = '\0';
	if (namelist->list == 0 || (name = strsep(bufp, ".")) == NULL) {
		if (!foundSome) {
			fprintf(stderr, "%s: incomplete specification\n",
			    string);
			invalid_name_used = 1;
		}
		return (-1);
	}
	for (i = 0; i < namelist->size; i++)
		if (namelist->list[i].ctl_name != NULL &&
			strcmp(name, namelist->list[i].ctl_name) == 0)
			break;
	if (i == namelist->size) {
		if (!foundSome) {
			fprintf(stderr, "%s level name %s in %s is invalid\n",
			    level, name, string);
			invalid_name_used = 1;
		}
		return (-1);
	}
	return (i);
}

void
usage()
{
	(void) fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n",
	    "usage: zsysctl [-bn] variable ...",
	    "       zsysctl [-bn] -w variable=value ...",
	    "       zsysctl [-bn] -a",
	    "       zsysctl [-bn] -A",
	    "       zsysctl [-bn] -X",
	    "       zsysctl -f [conffile]");
	exit(1);
}

/*
 * Parse a name into a MIB entry.
 * Lookup and print out the MIB entry if it exists.
 * Set a new value if requested.
 */
static void
parse(char *string, int flags)
{
	int len, i, j;
	void *newval = 0;
	int intval, newsize = 0;
	unsigned int uintval;
	quad_t quadval;
	int mib[CTL_MAXNAME];
	char *cp, *bufp, buf[BUFSIZ], fmt[BUFSIZ];
	uint_t kind;

	bufp = buf;
	if (snprintf(buf, BUFSIZ, "%s", string) >= BUFSIZ)
		errx(1, "MIB too long");
	snprintf(buf, BUFSIZ, "%s", string);
	if ((cp = strchr(string, '=')) != NULL) {
		if (!wflag)
			errx(2, "must specify -w to set variables");
		*strchr(buf, '=') = '\0';
		*cp++ = '\0';
		while (isspace(*cp))
			cp++;
		newval = cp;
		newsize = strlen(cp);
	} else {
		if (wflag)
			usage();
	}
	len = name2oid(bufp, mib);

	if (len < 0) {
		if (cp != NULL) {
			while (*cp != '\0') cp--;
			*cp = '=';
		}
		old_parse(string, flags);
		return;
	}

	/*
	 * An non-zero return here is an OID space containing parameters which
	 * needs to be ignored in the interests of backward compatibility with
	 * pre-newsysctl sysctls.
	 */
	if (oidfmt(mib, len, fmt, &kind))
		return;

	if (!wflag) {
		if ((kind & CTLTYPE) == CTLTYPE_NODE) {
			sysctl_all(mib, len);
			foundSome = 1;
			old_parse(string, flags);
		} else {
			i = show_var(mib, len, 1);
			if (!i && !bflag)
				putchar('\n');
		}
	} else {
		if ((kind & CTLTYPE) == CTLTYPE_NODE)
			errx(1, "oid '%s' isn't a leaf node", bufp);

		if (!(kind&CTLFLAG_WR))
			errx(1, "oid '%s' is read only", bufp);

		switch (kind & CTLTYPE) {
			case CTLTYPE_INT:
				if ((*fmt == 'I') && (*(fmt + 1) == 'U')) {
					uintval = (unsigned int) strtoul(newval,
					    NULL, 0);
					if ((uintval == 0) &&
					    (errno == EINVAL)) {
						errx(1, "invalid argument: %s",
						    (char *)newval);
						return;
					}
					newval = &uintval;
					newsize = sizeof (uintval);
				} else {
					intval = (int)strtol(newval, NULL, 0);
					if ((intval == 0) &&
					    (errno == EINVAL)) {
						errx(1, "invalid argument: %s",
						    (char *)newval);
						return;
					}
					newval = &intval;
					newsize = sizeof (intval);
				}
				break;
			case CTLTYPE_STRING:
				break;
			case CTLTYPE_QUAD:
				quadval = strtoq(newval, NULL, 0);
				if ((quadval == 0) && (errno == EINVAL)) {
					errx(1, "invalid argument %s",
					    (char *)newval);
					return;
				}
				newval = &quadval;
				newsize = sizeof (quadval);
				break;
			default:
				errx(1, "oid '%s' is type %d,"
				    " cannot set that", bufp,
				    kind & CTLTYPE);
		}

		i = show_var(mib, len, 1);
		if (sysctl(mib, len, 0, 0, newval, newsize) == -1) {
			if (!i && !bflag)
				putchar('\n');
			switch (errno) {
				case ENOTSUP:
				errx(1, "%s: value is not available",
				    string);
				case ENOTDIR:
				errx(1,
				    "%s: specification is incomplete",
				    string);
				case ENOMEM:
			errx(1,
			    "%s: type is unknown to this program",
			    string);
				default:
					warn("%s", string);
					return;
			}
		}
		if (!bflag)
			printf(" -> ");
		i = nflag;
		nflag = 1;
		j = show_var(mib, len, 1);
		if (!j && !bflag)
			putchar('\n');
		nflag = i;
	}
}

static int
parsefile(const char *filename)
{
	FILE *file;
	char line[BUFSIZ], *p, *pq, *pdq;
	int warncount = 0, lineno = 0;

	file = fopen(filename, "r");
	if (file == NULL)
		err(1, "%s", filename);
	while (fgets(line, sizeof (line), file) != NULL) {
		lineno++;
		p = line;
		pq = strchr(line, '\'');
		pdq = strchr(line, '\"');
		/* Replace the first # with \0. */
		while ((p = strchr(p, '#')) != NULL) {
			if (pq != NULL && p > pq) {
				if ((p = strchr(pq+1, '\'')) != NULL)
					*(++p) = '\0';
				break;
			} else if (pdq != NULL && p > pdq) {
				if ((p = strchr(pdq+1, '\"')) != NULL)
					*(++p) = '\0';
				break;
			} else if (p == line || *(p-1) != '\\') {
				*p = '\0';
				break;
			}
			p++;
		}
		/* Trim spaces */
		p = line + strlen(line) - 1;
		while (p >= line && isspace((int)*p)) {
			*p = '\0';
			p--;
		}
		p = line;
		while (isspace((int)*p))
			p++;
		if (*p == '\0')
			continue;
		else
			/* warncount += */parse(p, 1); // lineno);
	}
	fclose(file);

	return (warncount);
}

/* These functions will dump out various interesting structures. */

static int
S_clockinfo(int l2, void *p)
{
	struct clockinfo *ci = (struct clockinfo *)p;

	if (l2 != sizeof (*ci)) {
		warnx("S_clockinfo %d != %ld", l2, sizeof (*ci));
		return (1);
	}
	printf(hflag ? "{ hz = %'d, tick = %'d, tickadj = %'d,"
	    " profhz = %'d, stathz = %'d }" :
	    "{ hz = %d, tick = %d, tickadj = %d, profhz = %d, stathz = %d }",
	    ci->hz, ci->tick, ci->tickadj, ci->profhz, ci->stathz);
	return (0);
}

static int
S_loadavg(int l2, void *p)
{
	struct loadavg *tv = (struct loadavg *)p;

	if (l2 != sizeof (*tv)) {
		warnx("S_loadavg %d != %ld", l2, sizeof (*tv));
		return (1);
	}
	printf(hflag ? "{ %'.2f %'.2f %'.2f }" : "{ %.2f %.2f %.2f }",
	    (double)tv->ldavg[0]/(double)tv->fscale,
	    (double)tv->ldavg[1]/(double)tv->fscale,
	    (double)tv->ldavg[2]/(double)tv->fscale);
	return (0);
}

static int
S_timeval(int l2, void *p)
{
	struct timeval *tv = (struct timeval *)p;
	time_t tv_sec;
	char *p1, *p2;

	if (l2 != sizeof (*tv)) {
		warnx("S_timeval %d != %ld", l2, sizeof (*tv));
		return (1);
	}
	printf(hflag ? "{ sec = %'jd, usec = %'ld } " :
	    "{ sec = %jd, usec = %ld } ",
	    (intmax_t)tv->tv_sec, (long)tv->tv_usec);
	tv_sec = tv->tv_sec;
	p1 = strdup(ctime(&tv_sec));
	for (p2 = p1; *p2; p2++)
		if (*p2 == '\n')
			*p2 = '\0';
	fputs(p1, stdout);
	free(p1);
	return (0);
}

static int
S_xswusage(int l2, void *p)
{
	struct xsw_usage *xsu = (struct xsw_usage *)p;

	if (l2 != sizeof (*xsu)) {
		warnx("S_xswusage %d != %ld", l2, sizeof (*xsu));
		return (1);
	}
	fprintf(stdout,
	    "total = %.2fM  used = %.2fM  free = %.2fM  %s",
	    ((double)xsu->xsu_total) / (1024.0 * 1024.0),
	    ((double)xsu->xsu_used) / (1024.0 * 1024.0),
	    ((double)xsu->xsu_avail) / (1024.0 * 1024.0),
	    xsu->xsu_encrypted ? "(encrypted)" : "");
	return (0);
}

static int
T_dev_t(int l2, void *p)
{
	dev_t *d = (dev_t *)p;

	if (l2 != sizeof (*d)) {
		warnx("T_dev_T %d != %ld", l2, sizeof (*d));
		return (1);
	}
	if ((int)(*d) != -1) {
		if (minor(*d) > 255 || minor(*d) < 0)
			printf("{ major = %d, minor = 0x%x }",
			    major(*d), minor(*d));
		else
			printf("{ major = %d, minor = %d }",
			    major(*d), minor(*d));
	}
	return (0);
}

/*
 * These functions uses a presently undocumented interface to the kernel
 * to walk the tree and get the type so it can print the value.
 * This interface is under work and consideration, and should probably
 * be killed with a big axe by the first person who can find the time.
 * (be aware though, that the proper interface isn't as obvious as it
 * may seem, there are various conflicting requirements.
 */

static int
name2oid(char *name, int *oidp)
{
	int oid[2];
	int i;
	size_t j;

	oid[0] = 0;
	oid[1] = 3;

	j = CTL_MAXNAME * sizeof (int);
	i = sysctl(oid, 2, oidp, &j, name, strlen(name));
	if (i < 0)
		return (i);
	j /= sizeof (int);
	return (j);
}

static int
oidfmt(int *oid, int len, char *fmt, uint_t *kind)
{
	int qoid[CTL_MAXNAME+2];
	uchar_t buf[BUFSIZ];
	int i;
	size_t j;

	qoid[0] = 0;
	qoid[1] = 4;
	memcpy(qoid + 2, oid, len * sizeof (int));

	j = sizeof (buf);
	i = sysctl(qoid, len + 2, buf, &j, 0, 0);
	if (i) {
		/*
		 * An ENOENT error return indicates that the OID in question
		 * is a node OID followed not by additional OID elements, but
		 * by integer parameters.  We really do not want to support
		 * this type of thing going forward, but we alow it here for
		 * historical compatibility.  Eventually, this will go away.
		 */
		if (errno == ENOENT)
			return (ENOENT);
		err(1, "sysctl fmt %d %ld %d", i, j, errno);
	}

	if (kind)
		*kind = *(uint_t *)buf;

	if (fmt)
		strcpy(fmt, (char *)(buf + sizeof (uint_t)));
	return (0);
}

/*
 * This formats and outputs the value of one variable
 *
 * Returns zero if anything was actually output.
 * Returns one if didn't know what to do with this.
 * Return minus one if we had errors.
 */

static int
show_var(int *oid, int nlen, int show_masked)
{
	uchar_t buf[BUFSIZ], *val, *mval, *p;
	char name[BUFSIZ], /* descr[BUFSIZ], */ *fmt;
	int qoid[CTL_MAXNAME+2];
	int i;
	int retval;
	size_t j, len;
	uint_t kind;
	int (*func)(int, void *) = 0;

	qoid[0] = 0;
	memcpy(qoid + 2, oid, nlen * sizeof (int));

	qoid[1] = 1;
	j = sizeof (name);
	i = sysctl(qoid, nlen + 2, name, &j, 0, 0);
	if (i || !j)
		err(1, "sysctl name %d %ld %d", i, j, errno);

	/* find an estimate of how much we need for this var */
	j = 0;
	i = sysctl(oid, nlen, 0, &j, 0, 0);
	j += j; /* we want to be sure :-) */

	val = mval = malloc(j);
	len = j;
	i = sysctl(oid, nlen, val, &len, 0, 0);
	if (i || !len) {
		retval = 1;
		goto RETURN;
	}

	if (bflag) {
		fwrite(val, 1, len, stdout);
		retval = 0;
		goto RETURN;
	}

	qoid[1] = 4;
	j = sizeof (buf);
	i = sysctl(qoid, nlen + 2, buf, &j, 0, 0);
	/*
	 * An ENOENT error return indicates that the OID in question
	 * is a node OID followed not by additional OID elements, but
	 * by integer parameters.  We really do not want to support
	 * this type of thing going forward, but we alow it here for
	 * historical compatibility.  Eventially, this will go away.
	 */
	if (i && errno == ENOENT) {
		retval = 1;
		goto RETURN;
	}

	if (i || !j)
		err(1, "sysctl fmt %d %ld %d", i, j, errno);

	kind = *(uint_t *)buf;
	if (!show_masked && (kind & CTLFLAG_MASKED)) {
		retval = 1;
		goto RETURN;
	}

	fmt = (char *)(buf + sizeof (uint_t));

	p = val;
	switch (*fmt) {
		case '-':
			/* deprecated, do not print */
			retval = 0;
			goto RETURN;

		case 'A':
			if (!nflag)
				printf("%s: ", name);
			printf("%s", p);
			retval = 0;
			goto RETURN;

		case 'I':
			if (!nflag)
				printf("%s: ", name);
			fmt++;
			val = (unsigned char *)"";
			while (len >= sizeof (int)) {
				if (*fmt == 'U')
					printf("%s%u", val, *(unsigned int *)p);
				else
					printf("%s%d", val, *(int *)p);
				val = (unsigned char *)" ";
				len -= sizeof (int);
				p += sizeof (int);
			}
			retval = 0;
			goto RETURN;

		case 'L':
			if (!nflag)
				printf("%s: ", name);
			fmt++;
			val = (unsigned char *)"";
			while (len >= sizeof (long)) {
				if (*fmt == 'U')
					printf("%s%lu", val,
					    *(unsigned long *)p);
				else
					printf("%s%ld", val, *(long *)p);
				val = (unsigned char *)" ";
				len -= sizeof (long);
				p += sizeof (long);
			}
			retval = 0;
			goto RETURN;

		case 'P':
			if (!nflag)
				printf("%s: ", name);
			printf("%p", *(void **)p);
			retval = 0;
			goto RETURN;

		case 'Q':
			if (!nflag)
				printf("%s: ", name);
			fmt++;
			val = (unsigned char *)"";
			while (len >= sizeof (long long)) {
				if (*fmt == 'U')
					printf("%s%llu", val,
					    *(unsigned long long *)p);
				else
					printf("%s%lld", val, *(long long *)p);
				val = (unsigned char *)" ";
				len -= sizeof (long long);
				p += sizeof (long long);
			}
			retval = 0;
			goto RETURN;

		case 'T':
		case 'S':
			i = 0;
			if (strcmp(fmt, "S,clockinfo") == 0)
				func = S_clockinfo;
			else if (strcmp(fmt, "S,timeval") == 0)
				func = S_timeval;
			else if (strcmp(fmt, "S,loadavg") == 0)
				func = S_loadavg;
			else if (strcmp(fmt, "S,xsw_usage") == 0)
				func = S_xswusage;
			else if (strcmp(fmt, "T,dev_t") == 0)
				func = T_dev_t;
			if (func) {
				if (!nflag)
					printf("%s: ", name);
				retval = (*func)(len, p);
				goto RETURN;
			}
			/* FALL THROUGH */
			zfs_fallthrough;
		default:
			if (!Aflag) {
				retval = 1;
				goto RETURN;
			}
			if (!nflag)
				printf("%s: ", name);
			printf("Format:%s Length:%ld Dump:0x", fmt, len);
			while (len--) {
				printf("%02x", *p++);
				if (Xflag || p < val+16)
					continue;
				printf("...");
				break;
			}
			retval = 0;
			goto RETURN;
	}

	retval = 1;
RETURN:
	free(mval);
	return (retval);
}

static int
sysctl_all(int *oid, int len)
{
	int name1[22], name2[22];
	int i, j;
	size_t l1, l2;

	name1[0] = 0;
	name1[1] = 2;
	l1 = 2;
	if (len) {
		memcpy(name1+2, oid, len*sizeof (int));
		l1 += len;
	} else {
		name1[2] = 1;
		l1++;
	}
	while (1) {
		l2 = sizeof (name2);
		j = sysctl(name1, l1, name2, &l2, 0, 0);
		if (j < 0) {
			if (errno == ENOENT)
				return (0);
			else
				err(1, "sysctl(getnext) %d %ld", j, l2);
		}

		l2 /= sizeof (int);

		if (l2 < len)
			return (0);

		for (i = 0; i < len; i++)
			if (name2[i] != oid[i])
				return (0);

		i = show_var(name2, l2, 0);
		if (!i && !bflag)
			putchar('\n');

		memcpy(name1+2, name2, l2*sizeof (int));
		l1 = 2 + l2;
	}
}
