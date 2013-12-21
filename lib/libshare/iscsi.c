/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2011-2014 Turbo Fredriksson <turbo@bayour.com>, loosely
 * based on nfs.c by Gunnar Beutner.
 *
 * This is an addition to the zfs device driver to retrieve, add and remove
 * iSCSI targets using either the 'ietadm' or 'tgtadm' command to add, remove
 * and modify targets.
 *
 * It (the driver) will automatically calculate the TID and IQN and use only
 * the ZVOL (in this case 'tank/test') in the command lines. Unless the optional
 * file '/etc/iscsi_target_id' exists, in which case the content of that will
 * be used instead for the system part of the IQN.
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stddef.h>
#include <libzfs.h>
#include <libshare.h>
#include <sys/fs/zfs.h>
#include "libshare_impl.h"
#include "iscsi.h"

static boolean_t iscsi_available(void);
static boolean_t iscsi_is_share_active(sa_share_impl_t);

/*
 * See lib/libshare/iscsi_{iet,lio,scst,stgt}.c
 */

extern int iscsi_retrieve_targets_iet(void);
extern int iscsi_enable_share_one_iet(sa_share_impl_t, int);
extern int iscsi_disable_share_one_iet(int);

extern int iscsi_retrieve_targets_lio(void);
extern int iscsi_enable_share_one_lio(sa_share_impl_t, int);
extern int iscsi_disable_share_one_lio(int);

extern int iscsi_retrieve_targets_scst(void);
extern int iscsi_enable_share_one_scst(sa_share_impl_t, int);
extern int iscsi_disable_share_one_scst(int);

extern int iscsi_retrieve_targets_stgt(void);
extern int iscsi_enable_share_one_stgt(sa_share_impl_t, int);
extern int iscsi_disable_share_one_stgt(int);

list_t all_iscsi_targets_list;
sa_fstype_t *iscsi_fstype;

enum {
	ISCSI_IMPL_NONE = 0,
	ISCSI_IMPL_IET,
	ISCSI_IMPL_SCST,
	ISCSI_IMPL_STGT,
	ISCSI_IMPL_LIO
};

/*
 * What iSCSI implementation found
 *  0: none
 *  1: IET found
 *  2: SCST found
 *  3: STGT found
 *  4: LIO found
 */
static int iscsi_implementation;

/*
 * ============================================================
 * Support functions
 */

iscsi_session_t *
iscsi_session_list_alloc(void)
{
	iscsi_session_t *session;

	session = (iscsi_session_t *) malloc(sizeof (iscsi_session_t));
	if (session == NULL)
		return (NULL);

	list_link_init(&session->next);

	return (session);
}

int
iscsi_read_sysfs_value(char *path, char **value)
{
	int rc = SA_SYSTEM_ERR, buffer_len;
	char buffer[255];
	FILE *scst_sysfs_file_fp = NULL;

	/* Make sure that path and value is set */
	assert(path != NULL);
	if (!value)
		return (rc);

	/*
	 * TODO:
	 * If *value is not NULL we might be dropping allocated memory, assert?
	 */
	*value = NULL;

#if DEBUG >= 2
	fprintf(stderr, "iscsi_read_sysfs_value: path=%s", path);
#endif

	scst_sysfs_file_fp = fopen(path, "r");
	if (scst_sysfs_file_fp != NULL) {
		if (fgets(buffer, sizeof (buffer), scst_sysfs_file_fp)
		    != NULL) {
			/* Trim trailing new-line character(s). */
			buffer_len = strlen(buffer);
			while (buffer_len > 0) {
			    buffer_len--;
			    if (buffer[buffer_len] == '\r' ||
				buffer[buffer_len] == '\n') {
				buffer[buffer_len] = 0;
			    } else
				break;
			}

			*value = strdup(buffer);

#if DEBUG >= 2
			fprintf(stderr, ", value=%s", *value);
#endif

			/* Check that strdup() was successful */
			if (*value)
				rc = SA_OK;
		}

		fclose(scst_sysfs_file_fp);
	}

#if DEBUG >= 2
	fprintf(stderr, "\n");
#endif
	return (rc);
}

int
iscsi_write_sysfs_value(char *path, char *value)
{
	int rc = SA_SYSTEM_ERR;
	FILE *scst_sysfs_file_fp = NULL;

	/* Make sure that path and value is set */
	assert(path != NULL);
	assert(value != NULL);

#if DEBUG >= 2
	fprintf(stderr, "iscsi_write_sysfs_value: '%s' => '%s'\n",
		path, value);
#endif

	scst_sysfs_file_fp = fopen(path, "w");
	if (scst_sysfs_file_fp != NULL) {
		if (fputs(value, scst_sysfs_file_fp) != EOF)
			rc = SA_OK;

		fclose(scst_sysfs_file_fp);
	} else
		rc = SA_SYSTEM_ERR;

	return (rc);
}

static iscsi_dirs_t *
iscsi_dirs_list_alloc(void)
{
	static iscsi_dirs_t *entries;

	entries = (iscsi_dirs_t *) malloc(sizeof (iscsi_dirs_t));
	if (entries == NULL)
		return (NULL);

	list_link_init(&entries->next);

	return (entries);
}

list_t *
iscsi_look_for_stuff(char *path, const char *needle, boolean_t match_dir,
		int check_len)
{
	int ret;
	char path2[PATH_MAX], *path3;
	DIR *dir;
	struct dirent *directory;
	struct stat eStat;
	iscsi_dirs_t *entry;
	list_t *entries = malloc(sizeof (list_t));

#if DEBUG >= 2
	fprintf(stderr, "iscsi_look_for_stuff: '%s' (needle='%s') - %s/%d\n",
		path, needle ? needle : "", match_dir ? "Y" : "N",
		check_len);
#endif

	/* Make sure that path is set */
	assert(path != NULL);

	list_create(entries, sizeof (iscsi_dirs_t),
		    offsetof(iscsi_dirs_t, next));

	if ((dir = opendir(path))) {
		while ((directory = readdir(dir))) {
			if (directory->d_name[0] == '.')
				continue;

			path3 = NULL;
			ret = snprintf(path2, sizeof (path2),
					"%s/%s", path, directory->d_name);
			if (ret < 0 || ret >= sizeof (path2))
				/* Error or not enough space in string */
				/* TODO: Decide to continue or break */
				continue;

			if (stat(path2, &eStat) == -1)
				goto look_out;

			if (match_dir && !S_ISDIR(eStat.st_mode))
				continue;

			if (needle != NULL) {
				if (check_len) {
					if (strncmp(directory->d_name,
						    needle, check_len) == 0)
						path3 = strdup(path2);
				} else {
					if (strcmp(directory->d_name, needle)
					    == 0)
						path3 = strdup(path2);
				}
			} else {
				/* Ignore for SCST */
				if (strcmp(directory->d_name, "mgmt") == 0)
					continue;

				/* Ignore for LIO */
				if ((strncmp(directory->d_name, "alua", 4)
				    == 0) ||
				    (strcmp(directory->d_name, "statistics")
				    == 0) ||
				    (strcmp(directory->d_name, "write_protect")
				    == 0))
					continue;

				path3 = strdup(path2);
			}

			if (path3) {
				entry = iscsi_dirs_list_alloc();
				if (entry == NULL) {
					free(path3);
					goto look_out;
				}

				strncpy(entry->path, path3,
					sizeof (entry->path));
				strncpy(entry->entry, directory->d_name,
					sizeof (entry->entry));
				entry->stats = eStat;

#if DEBUG >= 2
				fprintf(stderr, "  %s\n",
					entry->path);
#endif
				list_insert_tail(entries, entry);

				free(path3);
			}
		}

look_out:
		closedir(dir);
	}

	return (entries);
}

/*
 * Generate a target name using the current year and month,
 * the domain name and the path.
 *
 * http://en.wikipedia.org/wiki/ISCSI#Addressing
 *
 * OR: Use information from /etc/iscsi_target_id:
 *     Example: iqn.2012-11.com.bayour
 *
 * => iqn.yyyy-mm.tld.domain:dataset (with . instead of / and _)
 */
int
iscsi_generate_target(const char *dataset, char *iqn, size_t iqn_len)
{
	char tsbuf[8]; /* YYYY-MM */
	char domain[256], revname[256], name[256],
		tmpdom[256], *p, tmp[20][256], *pos,
		buffer[256], file_iqn[223];  /* RFC3720: Max 223 bytes */
	time_t now;
	struct tm *now_local;
	int i, ret;
	FILE *domainname_fp = NULL, *iscsi_target_name_fp = NULL;

	if (dataset == NULL)
		return (SA_SYSTEM_ERR);

	/*
	 * Make sure file_iqn buffer contain zero byte or else strlen() later
	 * can fail.
	 */
	file_iqn[0] = 0;

	iscsi_target_name_fp = fopen(TARGET_NAME_FILE, "r");
	if (iscsi_target_name_fp == NULL) {
		/* Generate a name using domain name and date etc */

		/* Get current time in EPOCH */
		now = time(NULL);
		now_local = localtime(&now);
		if (now_local == NULL)
			return (SA_SYSTEM_ERR);

		/* Parse EPOCH and get YYY-MM */
		if (strftime(tsbuf, sizeof (tsbuf), "%Y-%m", now_local) == 0)
			return (SA_SYSTEM_ERR);

		/*
		 * Make sure domain buffer contain zero byte or else strlen()
		 * later can fail.
		 */
		domain[0] = 0;

#ifdef HAVE_GETDOMAINNAME
		/* Retrieve the domain */
		if (getdomainname(domain, sizeof (domain)) < 0) {
			/* Could not get domain via getdomainname() */
#endif
			if (access(DOMAINNAME_FILE, F_OK) == 0)
				domainname_fp = fopen(DOMAINNAME_FILE, "r");
			else if (access(DOMAINNAME_PROC, F_OK) == 0)
				domainname_fp = fopen(DOMAINNAME_PROC, "r");

			if (domainname_fp == NULL) {
				fprintf(stderr, "ERROR: Can't open %s: %s\n",
					DOMAINNAME_FILE, strerror(errno));
				return (SA_SYSTEM_ERR);
			}

			if (fgets(buffer, sizeof (buffer), domainname_fp)
			    != NULL) {
				strncpy(domain, buffer, sizeof (domain)-1);
				if (domain[strlen(domain)-1] == '\n')
					domain[strlen(domain)-1] = '\0';
			} else
				return (SA_SYSTEM_ERR);

			fclose(domainname_fp);
#ifdef HAVE_GETDOMAINNAME
		}
#endif

		/* Tripple check that we really have a domainname! */
		if ((strlen(domain) == 0) || (strcmp(domain, "(none)") == 0)) {
			fprintf(stderr, "ERROR: Can't retreive domainname!\n");
			return (SA_SYSTEM_ERR);
		}

		/* Reverse the domainname ('bayour.com' => 'com.bayour') */
		strncpy(tmpdom, domain, sizeof (tmpdom));

		i = 0;
		p = strtok(tmpdom, ".");
		while (p != NULL) {
			if (i == 20) {
				/* Reached end of tmp[] */
				/* XXX: print error? */
				return (SA_SYSTEM_ERR);
			}

			strncpy(tmp[i], p, sizeof (tmp[i]));
			p = strtok(NULL, ".");

			i++;
		}
		i--;
		memset(&revname[0], 0, sizeof (revname));
		for (; i >= 0; i--) {
			if (strlen(revname)) {
				ret = snprintf(tmpdom, sizeof (tmpdom),
						"%s.%s", revname, tmp[i]);
				if (ret < 0 || ret >= sizeof (tmpdom)) {
					/* XXX: print error? */
					return (SA_SYSTEM_ERR);
				}

				ret = snprintf(revname, sizeof (revname), "%s",
						tmpdom);
				if (ret < 0 || ret >= sizeof (revname)) {
					/* XXX: print error? */
					return (SA_SYSTEM_ERR);
				}
			} else {
				strncpy(revname, tmp[i], sizeof (revname));
				revname [sizeof (revname)-1] = '\0';
			}
		}
	} else {
		/*
		 * Use the content of file as the IQN
		 *  => "iqn.2012-11.com.bayour"
		 */
		if (fgets(buffer, sizeof (buffer), iscsi_target_name_fp)
		    != NULL) {
			strncpy(file_iqn, buffer, sizeof (file_iqn)-1);
			file_iqn[strlen(file_iqn)-1] = '\0';
		} else
			return (SA_SYSTEM_ERR);

		fclose(iscsi_target_name_fp);
	}

	/* Take the dataset name, replace invalid chars with . */
	strncpy(name, dataset, sizeof (name));
	pos = name;
	while (*pos != '\0') {
		switch (*pos) {
		case '/':
		case '-':
		case '_':
		case ':':
		case ' ':
			*pos = '.';
		default:
			/*
			 * Apparently there's initiator out in the
			 * wild that can't handle mixed case targets.
			 * Set all lower case - violates RFC3720
			 * though..
			 */
			*pos = tolower(*pos);
		}
		++pos;
	}

	/*
	 * Put the whole thing togheter
	 *  => "iqn.2012-11.com.bayour:share.VirtualMachines.Astrix"
	 */
	if (file_iqn[0]) {
		ret = snprintf(iqn, iqn_len, "%s:%s", file_iqn, name);
		if (ret < 0 || ret >= iqn_len) {
			/* XXX: print error? */
			return (SA_SYSTEM_ERR);
		}
	} else {
		ret = snprintf(iqn, iqn_len, "iqn.%s.%s:%s", tsbuf, revname,
				name);
		if (ret < 0 || ret >= iqn_len) {
			/* XXX: print error? */
			return (SA_SYSTEM_ERR);
		}
	}

	return (SA_OK);
}

/* Parse the 'initiator=<initiator>=<ro>;<initiator>=<ro>;.... */
list_t *
iscsi_parse_initiator(iscsi_shareopts_t *opts)
{
	char *tmp, *token, *init, *access_mode = NULL;
	iscsi_initiator_list_t *initiators;
	list_t *initiator_list = malloc(sizeof (list_t));

	/* Make sure that initiator is set */
	assert(opts != NULL);

	list_create(initiator_list, sizeof (iscsi_initiator_list_t),
		    offsetof(iscsi_initiator_list_t, next));

#if DEBUG >= 2
	fprintf(stderr, "iscsi_parse_initiator: %s\n", opts->initiator);
#endif

	/* Get each <initiator>=<ro|rw>, which is separated by ; */
	/* token="iqn.1993-08.org.debian:01:a59a7552c4a=ro" */
	token = strtok(opts->initiator, ";");
	while (token != NULL) {
		if (strchr(token, '=')) {
			tmp = strchr(token, '=');
			if (tmp == NULL)
				return (initiator_list);

			init = token;
			access_mode = tmp + 1;
			*tmp = '\0';
		} else {
			init = strdup(token);
			access_mode = "rw";
		}

		/* Setup list */
		initiators = (iscsi_initiator_list_t *)
			malloc(sizeof (iscsi_initiator_list_t));
		if (initiators == NULL)
			return (initiator_list);
		list_link_init(&initiators->next);

		strncpy(initiators->initiator, init,
			sizeof (initiators->initiator));

		initiators->read_only = B_FALSE;
		if (access_mode != NULL) {
			if (strcmp(access_mode, "ro") == 0)
				initiators->read_only = B_TRUE;
		} else if (strcmp(opts->iomode, "ro") == 0)
			initiators->read_only = B_TRUE;

#if DEBUG >= 2
		fprintf(stderr, "  iscsi_parse_initiator: %s=%d\n",
			initiators->initiator, initiators->read_only);
#endif
		list_insert_tail(initiator_list, initiators);

		token = strtok(NULL, ";");
	}

	return (initiator_list);
}

/*
 * ============================================================
 * Core functions
 */

/*
 * WRAPPER: Depending on iSCSI implementation, call the
 * relevant function but only if we haven't already.
 * TODO: That doesn't work exactly as intended. Threading?
 */
static int
iscsi_retrieve_targets(void)
{
	/*
	 * TODO: Don't seem to work...
	 *	if (!list_is_empty(&all_iscsi_targets_list)) {
	 *		// Try to limit the number of times we do this
	 *		fprintf(stderr, "iscsi_retrieve_targets: "
	 *		    !list_is_empty()\n");
	 *		return (SA_OK);
	 *	}
	 */

	/* Create the global share list  */
	list_create(&all_iscsi_targets_list, sizeof (iscsi_target_t),
		    offsetof(iscsi_target_t, next));

	if (iscsi_implementation == ISCSI_IMPL_IET)
		return (iscsi_retrieve_targets_iet());
	else if (iscsi_implementation == ISCSI_IMPL_SCST)
		return (iscsi_retrieve_targets_scst());
	else if (iscsi_implementation == ISCSI_IMPL_STGT)
		return (iscsi_retrieve_targets_stgt());
	else if (iscsi_implementation == ISCSI_IMPL_LIO)
		return (iscsi_retrieve_targets_lio());
	else
		return (SA_SYSTEM_ERR);
}

/*
 * Validates share option(s).
 */
static int
iscsi_get_shareopts_cb(const char *key, const char *value, void *cookie)
{
	char *dup_value;
	int lun;
	iscsi_shareopts_t *opts = (iscsi_shareopts_t *)cookie;

	if (strcmp(key, "on") == 0)
		return (SA_OK);

	/*
	 * ======
	 * Setup aliases
	 */

	/* iqn is an alias to name */
	if (strcmp(key, "iqn") == 0)
		key = "name";

	/* acl is an alias to initiator - only availible for LIO */
	if (iscsi_implementation == ISCSI_IMPL_LIO &&
		strcmp(key, "acl") == 0)
		key = "initiator";

	/*
	 * iotype is what's used in PROC_IET_VOLUME, but Type
	 * in ietadm and 'type' in shareiscsi option...
	 */
	if (strcmp(key, "iotype") == 0 ||
	    strcmp(key, "Type") == 0)
		key = "type";

	/* STGT calls it 'bstype' */
	if (strcmp(key, "bstype") == 0)
		key = "iomode";

	/* Just for completeness */
	if (strcmp(key, "BlockSize") == 0)
		key = "blocksize";

	/*
	 * ======
	 * Verify all options
	 */

	/* Verify valid options */
	if (strncmp(key, "name", 4) != 0 &&
		strcmp(key, "lun") != 0 &&
		strcmp(key, "type") != 0 &&
		strcmp(key, "iomode") != 0 &&
		strcmp(key, "blocksize") != 0 &&
		strcmp(key, "initiator") != 0 &&
		strcmp(key, "authname") != 0 &&
		strcmp(key, "authpass") != 0) {
			return (SA_SYNTAX_ERR);
	}

	dup_value = strdup(value);
	if (dup_value == NULL)
		return (SA_NO_MEMORY);

	/* Get share option values */
	if (strncmp(key, "name", 4) == 0) {
		strncpy(opts->name, dup_value, sizeof (opts->name));
		opts->name [sizeof (opts->name)-1] = '\0';
	}

	if (strcmp(key, "type") == 0) {
		/* Make sure it's a valid type value */
		if (strcmp(dup_value, "fileio") != 0 &&
		    strcmp(dup_value, "blockio") != 0 &&
		    strcmp(dup_value, "iblock") != 0 && /* LIO only */
		    strcmp(dup_value, "nullio") != 0 &&
		    strcmp(dup_value, "disk") != 0 &&
		    strcmp(dup_value, "tape") != 0 &&
		    strcmp(dup_value, "ssc") != 0 &&
		    strcmp(dup_value, "pt") != 0)
			return (SA_SYNTAX_ERR);

		/*
		 * The *Solaris options 'disk' (and future 'tape')
		 * isn't availible in ietadm. It _seems_ that 'fileio'
		 * is the Linux version.
		 *
		 * NOTE: Only for IET and LIO
		 */
		if ((iscsi_implementation == ISCSI_IMPL_IET ||
			iscsi_implementation == ISCSI_IMPL_LIO) &&
		    (strcmp(dup_value, "disk") == 0 ||
			strcmp(dup_value, "tape") == 0))
			strncpy(dup_value, "fileio", 7);

		/*
		 * The STGT option ssc = tape (=> fileio)
		 */
		if (iscsi_implementation == ISCSI_IMPL_STGT &&
		    strcmp(dup_value, "ssc") == 0)
			strncpy(dup_value, "fileio", 7);

		/*
		 * The blockio option = LIO iblock
		 */
		if (iscsi_implementation == ISCSI_IMPL_LIO) {
			if (strcmp(dup_value, "blockio") == 0)
				strncpy(dup_value, "iblock", 7);
		}

		strncpy(opts->type, dup_value, sizeof (opts->type));
		opts->type [sizeof (opts->type)-1] = '\0';
	}

	if (strcmp(key, "iomode") == 0) {
		/* Make sure it's a valid iomode */
		if (((iscsi_implementation == ISCSI_IMPL_SCST ||
			iscsi_implementation == ISCSI_IMPL_IET) &&
			strcmp(dup_value, "wb") != 0 &&
			strcmp(dup_value, "ro") != 0 &&
			strcmp(dup_value, "wt") != 0) ||
		    (iscsi_implementation == ISCSI_IMPL_STGT &&
			strcmp(dup_value, "rdwr") != 0 &&
			strcmp(dup_value, "aio") != 0 &&
			strcmp(dup_value, "mmap") != 0 &&
			strcmp(dup_value, "sg") != 0 &&
			strcmp(dup_value, "ssc") != 0) ||
		    (iscsi_implementation == ISCSI_IMPL_LIO &&
			strcmp(dup_value, "ro") != 0 &&
			strcmp(dup_value, "rw") != 0))
			return (SA_SYNTAX_ERR);

		if (strcmp(opts->type, "blockio") == 0 &&
		    strcmp(dup_value, "wb") == 0)
			/* Can't do write-back cache with blockio */
			strncpy(dup_value, "wt", 3);

		strncpy(opts->iomode, dup_value, sizeof (opts->iomode));
		opts->iomode [sizeof (opts->iomode)-1] = '\0';
	}

	if (strcmp(key, "lun") == 0) {
		lun = atoi(dup_value);
		if (iscsi_implementation == ISCSI_IMPL_STGT &&
		    lun == 0)
			/*
			 * LUN0 is reserved and it isn't possible
			 * to add a device 'backing store' to it).
			 */
			lun = 1;
		else if (iscsi_implementation == ISCSI_IMPL_LIO)
			/* LIO can only handle LUN >= 255 */
			if (lun >= 0 && lun <= 255)
				opts->lun = lun;
			else
				return (SA_SYNTAX_ERR);
		else {
			if (lun >= 0 && lun <= 16384)
				opts->lun = lun;
			else
				return (SA_SYNTAX_ERR);
		}
	}

	if (strcmp(key, "blocksize") == 0) {
		/* Make sure it's a valid blocksize */
		if (strcmp(dup_value, "512")  != 0 &&
		    strcmp(dup_value, "1024") != 0 &&
		    strcmp(dup_value, "2048") != 0 &&
		    strcmp(dup_value, "4096") != 0)
			return (SA_SYNTAX_ERR);

		opts->blocksize = atoi(dup_value);
	}

	if (strcmp(key, "initiator") == 0) {
		if (iscsi_implementation == ISCSI_IMPL_LIO ||
			iscsi_implementation == ISCSI_IMPL_SCST ||
			iscsi_implementation == ISCSI_IMPL_STGT) {
			strncpy(opts->initiator, dup_value,
				sizeof (opts->initiator));
			opts->initiator [ sizeof (opts->initiator)-1] = '\0';
		} else
			return (SA_SYNTAX_ERR);
	}

	if (strcmp(key, "authname") == 0) {
		strncpy(opts->authname, dup_value, sizeof (opts->authname));
		opts->authname [sizeof (opts->authname)-1] = '\0';
	}

	if (strcmp(key, "authpass") == 0) {
		if (iscsi_implementation == ISCSI_IMPL_SCST) {
			/* Require a password of >= 12 bytes */
			if (strlen(dup_value) < 12) {
				fprintf(stderr, "Password to short - ");
				return (SA_SYNTAX_ERR);
			}
		}
		strncpy(opts->authpass, dup_value, sizeof (opts->authpass));
		opts->authpass [sizeof (opts->authpass)-1] = '\0';
	}

	return (SA_OK);
}

/*
 * Takes a string containing share options (e.g. "name=Whatever,lun=3")
 * and converts them to a NULL-terminated array of options.
 */
int
iscsi_get_shareopts(sa_share_impl_t impl_share, const char *shareopts,
		    iscsi_shareopts_t **opts)
{
	char iqn[223]; /* RFC3720: Max 223 bytes */
	int rc;
	iscsi_shareopts_t *new_opts;
	uint64_t blocksize;
	zfs_handle_t *zhp;

	assert(opts != NULL);
	*opts = NULL;

	new_opts = (iscsi_shareopts_t *) calloc(sizeof (iscsi_shareopts_t), 1);
	if (new_opts == NULL)
		return (SA_NO_MEMORY);

	/* Set defaults */
	if (impl_share && impl_share->dataset) {
		if ((rc = iscsi_generate_target(impl_share->dataset, iqn,
						sizeof (iqn))) != 0)
			return (rc);

		strncpy(new_opts->name, iqn, strlen(new_opts->name));
		new_opts->name [strlen(iqn)+1] = '\0';
	}

	if (impl_share && impl_share->handle &&
	    impl_share->handle->zfs_libhandle) {
		/* Get the volume blocksize */
		zhp = zfs_open(impl_share->handle->zfs_libhandle,
				impl_share->dataset, ZFS_TYPE_VOLUME);

		if (zhp == NULL)
			return (SA_SYSTEM_ERR);

		blocksize = zfs_prop_get_int(zhp, ZFS_PROP_VOLBLOCKSIZE);

		zfs_close(zhp);

		if (blocksize == 512 || blocksize == 1024 ||
		    blocksize == 2048 || blocksize == 4096)
			new_opts->blocksize = blocksize;
		else
			new_opts->blocksize = 4096;
	} else
		new_opts->blocksize = 4096;

	if (iscsi_implementation == ISCSI_IMPL_STGT) {
		strncpy(new_opts->iomode, "rdwr", 5);
		strncpy(new_opts->type, "disk", 6);

		/*
		 * LUN0 is reserved and it isn't possible
		 * to add a device 'backing store' to it).
		 */
		new_opts->lun = 1;
	} else if (iscsi_implementation == ISCSI_IMPL_LIO) {
		strncpy(new_opts->iomode, "rw", 5);
		strncpy(new_opts->type, "iblock", 6);
		new_opts->lun = 0;
	} else {
		strncpy(new_opts->iomode, "wt", 3);
		strncpy(new_opts->type, "blockio", 8);
		new_opts->lun = 0;
	}
	new_opts->iomode [strlen(new_opts->iomode)+1] = '\0';
	new_opts->type [strlen(new_opts->type)+1] = '\0';

	if (iscsi_implementation == ISCSI_IMPL_LIO)
		new_opts->initiator[0] = '\0';
	else if (iscsi_implementation == ISCSI_IMPL_SCST) {
		strncpy(new_opts->initiator, "ALL",
		    strlen(new_opts->initiator));
		new_opts->initiator [strlen(new_opts->initiator)+1]
		    = '\0';
	}

	new_opts->authname[0] = '\0';
	new_opts->authpass[0] = '\0';

	*opts = new_opts;

	rc = foreach_shareopt(shareopts, iscsi_get_shareopts_cb, *opts);
	if (rc != SA_OK) {
		free(*opts);
		*opts = NULL;
	}

	return (rc);
}

/* WRAPPER: Depending on iSCSI implementation, call the relevant function */
static int
iscsi_enable_share_one(sa_share_impl_t impl_share, int tid)
{
	if (iscsi_implementation == ISCSI_IMPL_IET)
		return (iscsi_enable_share_one_iet(impl_share, tid));
	else if (iscsi_implementation == ISCSI_IMPL_SCST)
		return (iscsi_enable_share_one_scst(impl_share, tid));
	else if (iscsi_implementation == ISCSI_IMPL_STGT)
		return (iscsi_enable_share_one_stgt(impl_share, tid));
	else if (iscsi_implementation == ISCSI_IMPL_LIO)
		return (iscsi_enable_share_one_lio(impl_share, tid));
	else
		return (SA_SYSTEM_ERR);
}

static int
iscsi_enable_share(sa_share_impl_t impl_share)
{
	int tid = 0, prev_tid = 0;
	char *shareopts;
	iscsi_target_t *target;

	shareopts = FSINFO(impl_share, iscsi_fstype)->shareopts;
	if (shareopts == NULL) /* on/off */
		return (SA_SYSTEM_ERR);

	if (strcmp(shareopts, "off") == 0)
		return (SA_OK);

	for (target = list_head(&all_iscsi_targets_list);
	    target != NULL;
	    target = list_next(&all_iscsi_targets_list, target)) {
		/*
		 * Catch the fact that IET adds the target in reverse
		 * order (lower TID at the bottom).
		 */
		if (target->tid > prev_tid)
			tid = target->tid;

		prev_tid = tid;
	}
	tid = prev_tid + 1; /* Next TID is/should be availible */

	/* Magic: Enable (i.e., 'create new') share */
	return (iscsi_enable_share_one(impl_share, tid));
}

/* WRAPPER: Depending on iSCSI implementation, call the relevant function */
static int
iscsi_disable_share_one(int tid)
{
	if (iscsi_implementation == ISCSI_IMPL_IET)
		return (iscsi_disable_share_one_iet(tid));
	else if (iscsi_implementation == ISCSI_IMPL_SCST)
		return (iscsi_disable_share_one_scst(tid));
	else if (iscsi_implementation == ISCSI_IMPL_STGT)
		return (iscsi_disable_share_one_stgt(tid));
	else if (iscsi_implementation == ISCSI_IMPL_LIO)
		return (iscsi_disable_share_one_lio(tid));
	else
		return (SA_SYSTEM_ERR);
}

static int
iscsi_disable_share(sa_share_impl_t impl_share)
{
	int ret;
	iscsi_target_t *target;

	if (!iscsi_available())
		return (B_FALSE);

	/* Does this target have active sessions? */
	iscsi_retrieve_targets();
	for (target = list_head(&all_iscsi_targets_list);
	    target != NULL;
	    target = list_next(&all_iscsi_targets_list, target)) {
		if (strcmp(impl_share->sharepath, target->path) == 0) {
#ifdef DEBUG
			fprintf(stderr, "iscsi_disable_share: target=%s, "
				"tid=%d, path=%s\n", target->name,
				target->tid, target->path);
#endif

			if (target->session &&
			    target->session->state) {
				/*
				 * XXX: This will fail twice because
				 *      sa_disable_share is called
				 *      twice - once with correct protocol
				 *      (iscsi) and once with  protocol=NULL
				 */
				fprintf(stderr, "Can't unshare - have active"
					" shares\n");
				return (SA_OK);
			}

			if ((ret = iscsi_disable_share_one(target->tid))
			    == SA_OK)
				list_remove(&all_iscsi_targets_list, target);
			return (ret);
		}
	}

	return (SA_OK);
}

static boolean_t
iscsi_is_share_active(sa_share_impl_t impl_share)
{
	iscsi_target_t *target;

	if (!iscsi_available())
		return (B_FALSE);

	/* Does this target have active sessions? */
	iscsi_retrieve_targets();
	for (target = list_head(&all_iscsi_targets_list);
	    target != NULL;
	    target = list_next(&all_iscsi_targets_list, target)) {
#ifdef DEBUG
		fprintf(stderr, "iscsi_is_share_active: %s ?? %s\n",
			target->path, impl_share->sharepath);
#endif

		if (strcmp(target->path, impl_share->sharepath) == 0) {
#ifdef DEBUG
			fprintf(stderr, "=> %s is active\n", target->name);
#endif
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

static int
iscsi_validate_shareopts(const char *shareopts)
{
	iscsi_shareopts_t *opts;
	int rc = SA_OK;

	rc = iscsi_get_shareopts(NULL, shareopts, &opts);

	free(opts);
	return (rc);
}

static int
iscsi_update_shareopts(sa_share_impl_t impl_share, const char *resource,
    const char *shareopts)
{
	int ret;
	char iqn[223]; /* RFC3720: Max 223 bytes */
	char *shareopts_dup, *old_shareopts, tmp_opts[255];
	boolean_t needs_reshare = B_FALSE, have_active_sessions = B_FALSE;
	iscsi_target_t *target;
	iscsi_shareopts_t *opts;

	if (impl_share->dataset == NULL)
		return (B_FALSE);

	for (target = list_head(&all_iscsi_targets_list);
	    target != NULL;
	    target = list_next(&all_iscsi_targets_list, target)) {
		if ((strcmp(impl_share->sharepath, target->path) == 0) &&
		    target->session && target->session->state) {
			have_active_sessions = B_TRUE;

			break;
		}
	}

	/* Is the share active (i.e., shared */
	FSINFO(impl_share, iscsi_fstype)->active =
		iscsi_is_share_active(impl_share);

	/* Get old share opts */
	old_shareopts = FSINFO(impl_share, iscsi_fstype)->shareopts;

	if (strcmp(shareopts, "on") == 0 ||
	    (strncmp(shareopts, "name=", 5) != 0 &&
	    strncmp(shareopts, "iqn=",  4) != 0)) {
		/*
		 * Force a IQN value. This so that the iqn doesn't change
		 * 'next month' (when it's regenerated again) .
		 * NOTE: Does not change shareiscsi option, only sharetab!
		 */
		opts = (iscsi_shareopts_t *) malloc(sizeof (iscsi_shareopts_t));
		if (opts == NULL)
			return (SA_NO_MEMORY);

		ret = iscsi_get_shareopts(impl_share, old_shareopts, &opts);
		if (ret < 0) {
			free(opts);
			return (SA_SYSTEM_ERR);
		}

		if (opts->name != NULL) {
			if (iscsi_generate_target(impl_share->dataset, iqn,
					    sizeof (iqn)) == SA_OK) {
				ret = snprintf(tmp_opts, sizeof (tmp_opts),
					    "name=%s,%s", iqn, shareopts);
				if (ret < 0 || ret >= sizeof (tmp_opts))
					return (SA_SYSTEM_ERR);
			}
		} else {
			ret = snprintf(tmp_opts, sizeof (tmp_opts),
				    "name=%s,%s", opts->name, shareopts);
			if (ret < 0 || ret >= sizeof (tmp_opts))
				return (SA_SYSTEM_ERR);
		}

		shareopts = tmp_opts;
	}

#ifdef DEBUG
	fprintf(stderr, "iscsi_update_shareopts: share=%s;%s,"
		" active=%d, have_active_sessions=%d, new_shareopts=%s, "
		"old_shareopts=%s\n",
		impl_share->dataset, impl_share->sharepath,
		FSINFO(impl_share, iscsi_fstype)->active, have_active_sessions,
		shareopts,
		FSINFO(impl_share, iscsi_fstype)->shareopts ?
		FSINFO(impl_share, iscsi_fstype)->shareopts : "null");
#endif

	/*
	 * RESHARE if:
	 *  is active
	 *  have old shareopts
	 *  old shareopts != shareopts
	 *  no active sessions
	 */
	if (FSINFO(impl_share, iscsi_fstype)->active && old_shareopts != NULL &&
	    strcmp(old_shareopts, shareopts) != 0 && !have_active_sessions) {
		needs_reshare = B_TRUE;
		iscsi_disable_share(impl_share);
	}

	shareopts_dup = strdup(shareopts);

	if (shareopts_dup == NULL)
		return (SA_NO_MEMORY);

	if (old_shareopts != NULL)
		free(old_shareopts);

	FSINFO(impl_share, iscsi_fstype)->shareopts = shareopts_dup;

	if (needs_reshare)
		iscsi_enable_share(impl_share);

	return (SA_OK);
}

static void
iscsi_clear_shareopts(sa_share_impl_t impl_share)
{
	free(FSINFO(impl_share, iscsi_fstype)->shareopts);
	FSINFO(impl_share, iscsi_fstype)->shareopts = NULL;
}

static const sa_share_ops_t iscsi_shareops = {
	.enable_share = iscsi_enable_share,
	.disable_share = iscsi_disable_share,

	.validate_shareopts = iscsi_validate_shareopts,
	.update_shareopts = iscsi_update_shareopts,
	.clear_shareopts = iscsi_clear_shareopts,
};

/*
 * Provides a convenient wrapper for determing iscsi availability
 */
static boolean_t
iscsi_available(void)
{
	struct stat eStat;

	iscsi_implementation = ISCSI_IMPL_NONE;

	if (access(PROC_IET_VOLUME, F_OK) == 0 &&
	    access(IETM_CMD_PATH, X_OK) == 0) {
		iscsi_implementation = ISCSI_IMPL_IET;
#ifdef DEBUG
		fprintf(stderr, "iSCSI implementation: iet\n");
#endif
	} else if (access(STGT_CMD_PATH, X_OK) == 0) {
		iscsi_implementation = ISCSI_IMPL_STGT;
#ifdef DEBUG
		fprintf(stderr, "iSCSI implementation: stgt\n");
#endif
	} else if (stat(SYSFS_SCST, &eStat) == 0 &&
		    S_ISDIR(eStat.st_mode)) {
		iscsi_implementation = ISCSI_IMPL_SCST;
#ifdef DEBUG
		fprintf(stderr, "iSCSI implementation: scst\n");
#endif
	} else if (stat(SYSFS_LIO, &eStat) == 0 &&
		    S_ISDIR(eStat.st_mode)) {
		iscsi_implementation = ISCSI_IMPL_LIO;
#ifdef DEBUG
		fprintf(stderr, "iSCSI implementation: lio\n");
#endif
	}

	if (iscsi_implementation != ISCSI_IMPL_NONE)
		return (B_TRUE);

	return (B_FALSE);
}

void
libshare_iscsi_init(void)
{
	if (iscsi_available())
		iscsi_fstype = register_fstype("iscsi", &iscsi_shareops);
}
