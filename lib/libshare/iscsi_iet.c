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
 * Copyright (c) 2011-2014 Turbo Fredriksson <turbo@bayour.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <libzfs.h>
#include <libshare.h>
#include <sys/fs/zfs.h>
#include "libshare_impl.h"
#include "iscsi.h"

/*
 * ============================================================
 * Support functions
 */

/*
 * Reads the proc file and register if a tid have a sid. Save the value in
 * all_iscsi_targets_list->state
 */
static list_t *
iscsi_retrieve_sessions_iet(void)
{
	FILE *iscsi_volumes_fp = NULL;
	char *line, *token, *key, *value, *colon, *dup_value, buffer[512];
	int buffer_len;
	iscsi_session_t *session = NULL;
	list_t *target_sessions = malloc(sizeof (list_t));
	enum { ISCSI_SESSION, ISCSI_SID, ISCSI_CID } type;

	/* For storing the share info */
	char *tid = NULL, *name = NULL, *sid = NULL, *initiator = NULL,
		*cid = NULL, *ip = NULL, *state = NULL, *hd = NULL,
		*dd = NULL;

	list_create(target_sessions, sizeof (iscsi_session_t),
		    offsetof(iscsi_session_t, next));

	/* Open file with targets */
	iscsi_volumes_fp = fopen(PROC_IET_SESSION, "r");
	if (iscsi_volumes_fp == NULL)
		exit(SA_SYSTEM_ERR);

	/* Load the file... */
	while (fgets(buffer, sizeof (buffer), iscsi_volumes_fp) != NULL) {
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

		if (buffer[0] != '\t') {
			/*
			 * Line doesn't start with a TAB which means this is a
			 * session definition
			 */
			line = buffer;
			type = ISCSI_SESSION;

			free(name);
			free(tid);
			free(sid);
			free(cid);
			free(ip);
			free(initiator);
			free(state);
			free(hd);
			free(dd);

			name = tid = sid = cid = ip = NULL;
			initiator = state = hd = dd = NULL;
		} else if (buffer[0] == '\t' && buffer[1] == '\t') {
			/* Start with two tabs - CID definition */
			line = buffer + 2;
			type = ISCSI_CID;
		} else if (buffer[0] == '\t') {
			/* Start with one tab - SID definition */
			line = buffer + 1;
			type = ISCSI_SID;
		} else {
			/* Unknown line - skip it. */
			continue;
		}

		/* Get each option, which is separated by space */
		/* token='tid:18' */
		token = strtok(line, " ");
		while (token != NULL) {
			colon = strchr(token, ':');
			if (colon == NULL)
				goto next_sessions;

			key = token;
			value = colon + 1;
			*colon = '\0';

			dup_value = strdup(value);
			if (dup_value == NULL)
				exit(SA_NO_MEMORY);

			if (type == ISCSI_SESSION) {
				if (strcmp(key, "tid") == 0)
					tid = dup_value;
				else if (strcmp(key, "name") == 0)
					name = dup_value;
				else
					free(dup_value);
			} else if (type == ISCSI_SID) {
				if (strcmp(key, "sid") == 0)
					sid = dup_value;
				else if (strcmp(key, "initiator") == 0)
					initiator = dup_value;
				else
					free(dup_value);
			} else {
				if (strcmp(key, "cid") == 0)
					cid = dup_value;
				else if (strcmp(key, "ip") == 0)
					ip = dup_value;
				else if (strcmp(key, "state") == 0)
					state = dup_value;
				else if (strcmp(key, "hd") == 0)
					hd = dup_value;
				else if (strcmp(key, "dd") == 0)
					dd = dup_value;
				else
					free(dup_value);
			}

next_sessions:
			token = strtok(NULL, " ");
		}

		if (tid == NULL || sid == NULL || cid == NULL ||
		    name == NULL || initiator == NULL || ip == NULL ||
		    state == NULL || dd == NULL || hd == NULL)
			continue; /* Incomplete session definition */

		session = iscsi_session_list_alloc();
		if (session == NULL)
			exit(SA_NO_MEMORY);

		/* Save the values in the struct */
		session->tid = atoi(tid);
		session->sid = atoi(sid);
		session->cid = atoi(cid);

		strncpy(session->name, name, sizeof (session->name));
		strncpy(session->initiator, initiator,
			sizeof (session->initiator));
		strncpy(session->ip, ip, sizeof (session->ip));
		strncpy(session->hd, hd, sizeof (session->hd));
		strncpy(session->dd, dd, sizeof (session->dd));

		if (strcmp(state, "active") == 0)
			session->state = 1;
		else
			session->state = 0;

#ifdef DEBUG
		fprintf(stderr, "iscsi_retrieve_sessions: target=%s, tid=%d, "
			"sid=%d, cid=%d, initiator=%s, ip=%s, state=%d\n",
			session->name, session->tid, session->sid, session->cid,
			session->initiator, session->ip, session->state);
#endif

		/* Append the sessions to the list of new sessions */
		list_insert_tail(target_sessions, session);
	}

	if (iscsi_volumes_fp != NULL)
		fclose(iscsi_volumes_fp);

	free(name);
	free(tid);
	free(sid);
	free(cid);
	free(ip);
	free(initiator);
	free(state);
	free(hd);
	free(dd);

	return (target_sessions);
}

/*
 * ============================================================
 * Core functions
 */

int
iscsi_retrieve_targets_iet(void)
{
	FILE *iscsi_volumes_fp = NULL;
	char buffer[512];
	char *line, *token, *key, *value, *colon, *dup_value;
	int rc = SA_OK, buffer_len;
	iscsi_session_t *session;
	list_t *sessions;
	enum { ISCSI_TARGET, ISCSI_LUN } type;

	/* For soring the targets */
	char *tid = NULL, *name = NULL, *lun = NULL, *state = NULL;
	char *iotype = NULL, *iomode = NULL, *blocks = NULL;
	char *blocksize = NULL, *path = NULL;
	iscsi_target_t *target;

	/* Get all sessions */
	sessions = iscsi_retrieve_sessions_iet();

	/* Open file with targets */
	iscsi_volumes_fp = fopen(PROC_IET_VOLUME, "r");
	if (iscsi_volumes_fp == NULL)
		return (SA_SYSTEM_ERR);

	/* Load the file... */
	while (fgets(buffer, sizeof (buffer), iscsi_volumes_fp) != NULL) {
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

		if (buffer[0] != '\t') {
			/*
			 * Line doesn't start with a TAB which
			 * means this is a target definition
			 */
			line = buffer;
			type = ISCSI_TARGET;

			free(tid);
			free(name);
			free(lun);
			free(state);
			free(iotype);
			free(iomode);
			free(blocks);
			free(blocksize);
			free(path);

			tid = name = NULL;
			lun = state = iotype = iomode = NULL;
			blocks = blocksize = path = NULL;
		} else {
			/* LUN definition */
			line = buffer + 1;
			type = ISCSI_LUN;
		}

		/* Get each option, which is separated by space */
		/* token='tid:18' */
		token = strtok(line, " ");
		while (token != NULL) {
			colon = strchr(token, ':');
			if (colon == NULL)
				goto next_targets;

			key = token;
			value = colon + 1;
			*colon = '\0';

			dup_value = strdup(value);

			if (dup_value == NULL)
				exit(SA_NO_MEMORY);

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

next_targets:
			token = strtok(NULL, " ");
		}

		if (type != ISCSI_LUN)
			continue;

		if (tid == NULL || name == NULL || lun == NULL ||
		    state == NULL || iotype == NULL || iomode == NULL ||
		    blocks == NULL || blocksize == NULL || path == NULL)
			continue; /* Incomplete target definition */

		target = (iscsi_target_t *) malloc(sizeof (iscsi_target_t));
		if (target == NULL) {
			rc = SA_NO_MEMORY;
			goto retrieve_targets_iet_out;
		}

		/* Save the values in the struct */
		target->tid = atoi(tid);
		target->lun = atoi(lun);
		target->state = atoi(state);
		target->blocks = atoi(blocks);
		target->blocksize = atoi(blocksize);

		strncpy(target->name,	name,	sizeof (target->name));
		strncpy(target->path,	path,	sizeof (target->path));
		strncpy(target->iotype,	iotype,	sizeof (target->iotype));
		strncpy(target->iomode,	iomode,	sizeof (target->iomode));

		/* Link the session here */
		target->session = NULL;
		for (session = list_head(sessions);
		    session != NULL;
		    session = list_next(sessions, session)) {
			if (session->tid == target->tid) {
				target->session = session;
				list_link_init(&target->session->next);

				break;
			}
		}

#ifdef DEBUG
		fprintf(stderr, "iscsi_retrieve_targets_iet: target=%s, "
			"tid=%d, lun=%d, path=%s, active=%d\n", target->name,
			target->tid, target->lun, target->path,
			target->session ? target->session->state : -1);
#endif

		/* Append the target to the list of new targets */
		list_insert_tail(&all_iscsi_targets_list, target);
	}

retrieve_targets_iet_out:
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

	return (rc);
}

int
iscsi_enable_share_one_iet(sa_share_impl_t impl_share, int tid)
{
	char *argv[10], params[255], tid_s[11], lun_s[11];
	char *shareopts;
	iscsi_shareopts_t *opts;
	int rc, ret;
#ifdef DEBUG
	int i;
#endif

	opts = (iscsi_shareopts_t *) malloc(sizeof (iscsi_shareopts_t));
	if (opts == NULL)
		return (SA_NO_MEMORY);

	/* Get any share options */
	shareopts = FSINFO(impl_share, iscsi_fstype)->shareopts;
	rc = iscsi_get_shareopts(impl_share, shareopts, &opts);
	if (rc < 0) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

#ifdef DEBUG
	fprintf(stderr, "iscsi_enable_share_one_iet: name=%s, tid=%d, "
		"sharepath=%s, iomode=%s, type=%s, lun=%d, blocksize=%d, "
		"authname=%s, authpass=%s\n",
		opts->name, tid, impl_share->sharepath, opts->iomode,
		opts->type, opts->lun, opts->blocksize, opts->authname,
		opts->authpass);
#endif

	/*
	 * ietadm --op new --tid $next --params Name=$iqn
	 * ietadm --op new --tid $next --lun=0 --params \
	 *   Path=/dev/zvol/$sharepath,Type=<fileio|blockio|nullio>
	 */

	/*
	 * ======
	 * PART 1 - do the (inital) share. No path etc...
	 * CMD: ietadm --op new --tid <TID> --params <PARAMS>
	 */
	ret = snprintf(params, sizeof (params), "Name=%s",
			opts->name);
	if (ret < 0 || ret >= sizeof (params)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/* int: between -2,147,483,648 and 2,147,483,647 => 10 chars + NUL */
	ret = snprintf(tid_s, sizeof (tid_s), "%d", tid);
	if (ret < 0 || ret >= sizeof (tid_s)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	ret = snprintf(lun_s, sizeof (lun_s), "%d", opts->lun);
	if (ret < 0 || ret >= sizeof (lun_s)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	argv[0] = IETM_CMD_PATH;
	argv[1] = (char *)"--op";
	argv[2] = (char *)"new";
	argv[3] = (char *)"--tid";
	argv[4] = tid_s;
	argv[5] = (char *)"--params";
	argv[6] = params;
	argv[7] = NULL;

#ifdef DEBUG
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 7; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	if (rc != 0) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/*
	 * ======
	 * PART 2 - Add user acl
	 * CMD: ietadm --op new --tid <TID> --user \
	 *      --params=IncomingUser=iscsi-user,Password=secret
	 */
	if (strlen(opts->authname) && strlen(opts->authpass)) {
		ret = snprintf(params, sizeof (params),
				"IncomingUser=%s,Password=%s",
				opts->authname, opts->authpass);
		if (ret < 0 || ret >= sizeof (params)) {
			free(opts);
			return (SA_SYSTEM_ERR);
		}

		argv[5] = (char *)"--user";
		argv[6] = (char *)"--params";
		argv[7] = params;
		argv[8] = NULL;

#ifdef DEBUG
		fprintf(stderr, "CMD: ");
		for (i = 0; i < 8; i++)
			fprintf(stderr, "%s ", argv[i]);
		fprintf(stderr, "\n");
#endif

		rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
		if (rc != 0) {
			free(opts);
			return (SA_SYSTEM_ERR);
		}
	}

	/*
	 * ======
	 * PART 3 - Set share path and lun.
	 * CMD: ietadm --op new --tid <TID> --lun <LUN> --params <PARAMS>
	 */
	ret = snprintf(params, sizeof (params),
			"Path=%s,Type=%s,iomode=%s,BlockSize=%d",
			impl_share->sharepath, opts->type, opts->iomode,
			opts->blocksize);
	if (ret < 0 || ret >= sizeof (params)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	argv[5] = (char *)"--lun";
	argv[6] = lun_s;
	argv[7] = (char *)"--params";
	argv[8] = params;
	argv[9] = NULL;

#ifdef DEBUG
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 9; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	if (rc != 0) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/*
	 * ======
	 * PART 4 - Run local update script.
	 */
	if (access(EXTRA_ISCSI_SHARE_SCRIPT, X_OK) == 0) {
		/* CMD: /sbin/zfs_share_iscsi <TID> */
		argv[0] = (char *)EXTRA_ISCSI_SHARE_SCRIPT;
		argv[1] = tid_s;
		argv[2] = NULL;

#ifdef DEBUG
		fprintf(stderr, "CMD: ");
		for (i = 0; i < 2; i++)
			fprintf(stderr, "%s ", argv[i]);
		fprintf(stderr, "\n");
#endif

		/* Ignore any error from script - "fire and forget" */
		libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	}

	free(opts);
	return (SA_OK);
}

int
iscsi_disable_share_one_iet(int tid)
{
	char *argv[6];
	char tid_s[11];
	int rc, ret;

	/* int: between -2,147,483,648 and 2,147,483,647 => 10 chars + NUL */
	ret = snprintf(tid_s, sizeof (tid_s), "%d", tid);
	if (ret < 0 || ret >= sizeof (tid_s))
		return (SA_SYSTEM_ERR);

	/* CMD: ietadm --op delete --tid <TID> */
	argv[0] = IETM_CMD_PATH;
	argv[1] = (char *)"--op";
	argv[2] = (char *)"delete";
	argv[3] = (char *)"--tid";
	argv[4] = tid_s;
	argv[5] = NULL;

#ifdef DEBUG
	int i;
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 5; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	if (rc != 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
}
