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
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Turbo Fredriksson <turbo@bayour.com>, based on nfs.c by
 *                    Gunnar Beutner
 *
 * This is an addition to the zfs device driver to retrieve, add and remove
 * iSCSI targets using the 'ietadm' command. As of this, it only currently
 * supports the IET iSCSI target implementation.
 *
 * It uses a linked list named 'iscsi_target_t' to keep track of all targets.
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"
#include "iscsi.h"

static sa_fstype_t *iscsi_fstype;
boolean_t iscsi_available;

#if DEBUG >= 3
#define PROC_IET_VOLUME "/tmp/volume"
#else
#define PROC_IET_VOLUME "/proc/net/iet/volume"
#endif

#define IETM_CMD_PATH "/usr/sbin/ietadm"

/*
 * Generate a target name using the current year and month,
 * the domain name and the path:
 *
 * => iqn.yyyy-mm.tld.domain:path
 */
int
iscsi_generate_target(const char *path, char *iqn, size_t iqn_len)
{
	char tsbuf[8]; /* YYYY-MM */
	char domain[256], revname[255], name[255],
	  tmpdom[255], *p, tmp[20][255];
	time_t now;
	struct tm *now_local;
	int i;

	/* Get current time in EPOCH */
	now = time(NULL);
	now_local = localtime(&now);
	if (now_local == NULL)
		return -1;

	/* Parse EPOCH and get YYY-MM */
	if (strftime(tsbuf, sizeof (tsbuf), "%Y-%m", now_local) == 0)
		return -1;

	/* Retrieve the domain */
	if ((getdomainname(domain, sizeof (domain)) < 0) ||
	    (strcmp(domain, "(none)") == 0))
	{
		fprintf(stderr, "ERROR: Can't get domainname: %s\n", strerror(errno));
		return -1;
	}

	/* Reverse the domainname ('bayour.com' => 'com.bayour') */
	strncpy(tmpdom, domain, sizeof (domain));
	tmpdom [sizeof(tmpdom)-1] = '\0';

	i = 0;
	p = strtok(tmpdom, ".");
	while (p != NULL) {
		strncpy(tmp[i], p, strlen(p));
		p = strtok(NULL, ".");

		i++;
	}
	i--;
	memset(&revname[0], 0, sizeof (revname));
	for (; i >= 0; i--) {
		if(strlen(revname)) {
			snprintf(tmpdom, strlen(revname)+strlen(tmp[i])+2,
				 "%s.%s", revname, tmp[i]);
			snprintf(revname, strlen(tmpdom)+1, "%s", tmpdom);
		} else {
			strncpy(revname, tmp[i], strlen(tmp[i]));
			revname [sizeof(revname)-1] = '\0';
		}
	}

	/* Take the dataset name, replace / with . */
	strncpy(name, path, sizeof(name));
	char *pos = name;
	if (pos == NULL)
		return SA_SYSTEM_ERR;
	while( *pos != '\0' ) {
		switch( *pos ) {
		case '/':
		case '-':
		case ':':
		case ' ':
			*pos = '.';
		}
		++pos;
	}

	/* Put the whole thing togheter */
	snprintf(iqn, iqn_len, "iqn.%s.%s:%s", tsbuf, revname, name);

	return 0;
}

int
iscsi_enable_share_one(int tid, char *sharename, const char *sharepath,
    const char *iotype)
{
	char *argv[10];
	char params_name[255], params_path[255], tid_s[16];
	int rc;
#ifdef DEBUG
	int i;
#endif

	/*
	 * ietadm --op new --tid $next --params Name=$iqn
	 * ietadm --op new --tid $next --lun=0 --params \
	 *   Path=/dev/zvol/$sharepath,Type=$iotype
	 */

#ifdef DEBUG
	fprintf(stderr, "iscsi_enable_share_one(%d, %s, %s, %s)\n",
		tid, sharename, sharepath, iotype);
#endif

	/* PART 1 */
	snprintf(params_name, sizeof (params_name), "Name=%s", sharename);

	/* int: between -2,147,483,648 and 2,147,483,647 => 10 chars + NUL */
	snprintf(tid_s, sizeof(tid_s), "%d", tid);
	if (!file_is_executable(IETM_CMD_PATH)) {
		fprintf(stderr, "ERROR: %s: Does not exists or is not executable.\n", IETM_CMD_PATH);
		return SA_SYSTEM_ERR;
	}
	argv[0] = IETM_CMD_PATH;
	argv[1] = "--op";
	argv[2] = "new";
	argv[3] = "--tid";
	argv[4] = tid_s;
	argv[5] = "--params";
	argv[6] = params_name;
	argv[7] = NULL;

#ifdef DEBUG
	fprintf(stderr, "  ");
	for (i=0; i < 8; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, 0);
	if (rc < 0)
		return SA_SYSTEM_ERR;

	/* PART 2 */
	snprintf(params_path, sizeof (params_path),
		 "Path=%s,Type=%s", sharepath, iotype);

	argv[5] = "--lun";
	argv[6] = "0";
	argv[7] = "--params";
	argv[8] = params_path;
	argv[9] = NULL;

#ifdef DEBUG
	fprintf(stderr, "  ");
	for (i=0; i < 10; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, 0);
	if (rc < 0)
		return SA_SYSTEM_ERR;

	/* Reload the share file */
	iscsi_retrieve_targets();

	return 0;
}

static int
iscsi_enable_share(sa_share_impl_t impl_share)
{
	char *shareopts;
	char iqn[255];
	int tid = 0;
	iscsi_target_t *target = iscsi_targets;

	if (!iscsi_available)
		return SA_SYSTEM_ERR;

	shareopts = FSINFO(impl_share, iscsi_fstype)->shareopts;
	if (shareopts == NULL) /* on/off */
		return SA_SYSTEM_ERR;

	if (strcmp(shareopts, "off") == 0)
		return (0);

	if (iscsi_generate_target(impl_share->dataset, iqn, sizeof (iqn)) < 0)
		return SA_SYSTEM_ERR;

	/* Go through list of targets, take next avail. */
	while (target != NULL) {
		tid = target->tid;
		target = target->next;
	}
	tid++; /* Next TID is/should be availible */

	/* Magic: Enable (i.e., 'create new') share */
	return iscsi_enable_share_one(tid, iqn,
	    impl_share->sharepath, "fileio");
}

int
iscsi_disable_share_one(int tid)
{
	char *argv[6];
	char tid_s[16];
	int rc;
#ifdef DEBUG
	int i;
#endif

#ifdef DEBUG
	fprintf(stderr, "iscsi_disable_share_one(%d)\n", tid);
#endif

	/* int: between -2,147,483,648 and 2,147,483,647 => 10 chars + NUL */
	snprintf(tid_s, sizeof (tid_s), "%d", tid);
	if (!file_is_executable(IETM_CMD_PATH)) {
		fprintf(stderr, "ERROR: %s: Does not exists or is not executable.\n", IETM_CMD_PATH);
		return SA_SYSTEM_ERR;
	}
	argv[0] = IETM_CMD_PATH;
	argv[1] = "--op";
	argv[2] = "delete";
	argv[3] = "--tid";
	argv[4] = tid_s;
	argv[5] = NULL;

#ifdef DEBUG
	fprintf(stderr, "  ");
	for (i=0; i < 6; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, 0);
	if (rc < 0)
		return SA_SYSTEM_ERR;
	else {
		/* Reload the share file */
		iscsi_retrieve_targets();

		return SA_OK;
	}
}

static int
iscsi_disable_share(sa_share_impl_t impl_share)
{
#ifdef DEBUG
	fprintf(stderr, "iscsi_disable_share()\n");
#endif

	if (!iscsi_available) {
		/*
		 * The share can't possibly be active, so nothing
		 * needs to be done to disable it.
		 */
		return SA_OK;
	}

	return SA_OK;
}

int
iscsi_disable_share_all(void)
{
	int rc = 0;
	iscsi_target_t *target = iscsi_targets;

	while (target != NULL) {
		rc += iscsi_disable_share_one(target->tid);

		target = target->next;
	}

	return rc;
}

static boolean_t
iscsi_is_share_active(sa_share_impl_t impl_share)
{
	iscsi_target_t *target = iscsi_targets;
#ifdef DEBUG
	fprintf(stderr, "iscsi_is_share_active()\n");
#endif

	while (target != NULL) {
		if (strcmp(impl_share->sharepath, target->path) == 0)
			return B_TRUE;

		target = target->next;
	}

	return B_FALSE;
}

static int
iscsi_validate_shareopts(const char *shareopts)
{
	/* TODO: implement */
#ifdef DEBUG
	fprintf(stderr, "iscsi_validate_shareopts()\n");
#endif

	return 0;
}

static int
iscsi_update_shareopts(sa_share_impl_t impl_share, const char *resource,
		       const char *shareopts)
{
	char *shareopts_dup;
	boolean_t needs_reshare = B_FALSE;
	char *old_shareopts;

	FSINFO(impl_share, iscsi_fstype)->active = iscsi_is_share_active(impl_share);

	old_shareopts = FSINFO(impl_share, iscsi_fstype)->shareopts;

	if (FSINFO(impl_share, iscsi_fstype)->active && old_shareopts != NULL &&
	    strcmp(old_shareopts, shareopts) != 0) {
		needs_reshare = B_TRUE;
		iscsi_disable_share(impl_share);
	}

	shareopts_dup = strdup(shareopts);

	if (shareopts_dup == NULL)
		return SA_NO_MEMORY;

	if (old_shareopts != NULL)
		free(old_shareopts);

	FSINFO(impl_share, iscsi_fstype)->shareopts = shareopts_dup;

	if (needs_reshare)
		iscsi_enable_share(impl_share);

	return 0;
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
 * iscsi_retrieve_targets() retrieves list of iSCSI targets from
 * /proc/net/iet/volume
 */
int
iscsi_retrieve_targets(void)
{
	FILE *iscsi_volumes_fp;
	char buffer[512];
	char *line, *token, *key, *value, *colon, *dup_value, *c;
	char *tid = NULL, *name = NULL, *lun = NULL, *state = NULL;
	char *iotype = NULL, *iomode = NULL, *blocks = NULL;
	char *blocksize = NULL, *path = NULL;
	iscsi_target_t *target, *new_targets = NULL;
	int rc = SA_OK;
	enum { ISCSI_TARGET, ISCSI_LUN } type;

	/* Open file with targets */
	iscsi_volumes_fp = fopen(PROC_IET_VOLUME, "r");
	if (iscsi_volumes_fp == NULL) {
		rc = SA_SYSTEM_ERR;
		goto out;
	}

	/* Load the file... */
	while (fgets(buffer, sizeof (buffer), iscsi_volumes_fp) != NULL) {
		/* tid:1 name:iqn.2011-12.com.bayour:storage.astrix
		 *         lun:0 state:0 iotype:fileio iomode:wt blocks:31457280 blocksize:512 path:/dev/zvol/share/VMs/Astrix
		 */

		/* Trim trailing new-line character(s). */
//		for (c = line; *c; c++) {
//			if (*c == '\r' || *c == '\n') {
//				c = '\0';
//				break;
//			}
//		}
		while (buffer[strlen(buffer) - 1] == '\r' ||
		       buffer[strlen(buffer) - 1] == '\n')
			buffer[strlen(buffer) - 1] = '\0';

		if (buffer[0] != '\t') {
			/*
			 * Line doesn't start with a TAB which means this is a
			 * target definition
			 */
			line = buffer;
			type = ISCSI_TARGET;

			free(tid);
			tid = NULL;

			free(name);
			name = NULL;
		} else {
			/* LUN definition */
			line = buffer + 1;
			type = ISCSI_LUN;

			free(lun);
			lun = NULL;

			free(state);
			state = NULL;

			free(iotype);
			iotype = NULL;

			free(iomode);
			iomode = NULL;

			free(blocks);
			blocks = NULL;

			free(blocksize);
			blocksize = NULL;

			free(path);
			path = NULL;
		}

		/* Get each option, which is separated by space */
		/* token='tid:18' */
		token = strtok(line, " ");
		while (token != NULL) {
			colon = strchr(token, ':');

			if (colon == NULL)
				goto next;

			key = token;
			value = colon + 1;
			*colon = '\0';

			dup_value = strdup(value);

			if (dup_value == NULL) {
				rc = SA_NO_MEMORY;
				goto out;
			}

			if (type == ISCSI_TARGET) {
				if (strcmp(key, "tid") == 0)
					tid = dup_value;
				else if (strcmp(key, "name") == 0)
					name = dup_value;
				else
					free(dup_value);
			} else {
				if (strcmp(key, "lun") == 0)
					lun = dup_value;
				else if (strcmp(key, "state") == 0)
					state = dup_value;
				else if (strcmp(key, "iotype") == 0)
					iotype = dup_value;
				else if (strcmp(key, "iomode") == 0)
					iomode = dup_value;
				else if (strcmp(key, "blocks") == 0)
					blocks = dup_value;
				else if (strcmp(key, "blocksize") == 0)
					blocksize = dup_value;
				else if (strcmp(key, "path") == 0)
					path = dup_value;
				else
					free(dup_value);
			}

next:
			token = strtok(NULL, " ");
		}

		if (type != ISCSI_LUN)
			continue;

		if (tid == NULL || name == NULL || lun == NULL ||
		    state == NULL || iotype == NULL || iomode == NULL ||
		    blocks == NULL || blocksize == NULL || path == NULL)
			continue; /* Incomplete LUN definition */

		target = (iscsi_target_t *)malloc(sizeof (iscsi_target_t));
		if (target == NULL) {
			rc = SA_NO_MEMORY;
			goto out;
		}

		target->tid = atoi(tid);
		strncpy(target->name, name, sizeof (target->name));
		target->lun = atoi(lun);
		target->state = atoi(state);
		strncpy(target->iotype, iotype, sizeof (target->iotype));
		strncpy(target->iomode, iomode, sizeof (target->iomode));
		target->blocks = atoi(blocks);
		target->blocksize = atoi(blocksize);
		strncpy(target->path, path, sizeof (target->path));

		/* Append the target to the list of new targets */
		target->next = new_targets;
		new_targets = target;
	}

	/* TODO: free existing iscsi_targets */
	iscsi_targets = new_targets;

out:
	if (iscsi_volumes_fp != NULL)
		fclose(iscsi_volumes_fp);

	free(tid);
	free(name);
	free(lun);
	free(state);
	free(iotype);
	free(iomode);
	free(blocks);
	free(blocksize);
	free(path);

	return rc;
}

void
libshare_iscsi_init(void)
{
	iscsi_available = (iscsi_retrieve_targets() == SA_OK);

	iscsi_fstype = register_fstype("iscsi", &iscsi_shareops);
}
