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
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libzfs.h>
#include <libshare.h>
#include <sys/fs/zfs.h>
#include "libshare_impl.h"
#include "iscsi.h"

/*
 * ============================================================
 * Support functions
 */

char *
trim(char *str)
{
	size_t len = 0;
	char *frontp = str;
	char *endp = NULL;

	if (str == NULL)
		return (NULL);
	if (str[0] == '\0')
		return (str);

	len = strlen(str);
	endp = str + len;

	/*
	 * Move the front and back pointers to address the first non-whitespace
	 * characters from each end.
	 */
	while (isspace(*frontp))
		++frontp;
	if (endp != frontp)
		while (isspace(*(--endp)) && endp != frontp) {}

	if (str + len - 1 != endp)
		*(endp + 1) = '\0';
	else if (frontp != str && endp == frontp)
		*str = '\0';

	/*
	 * Shift the string so that it starts at str so that if it's dynamically
	 * allocated, we can still free it on the returned pointer. Note the
	 * reuse of endp to mean the front of the string buffer now.
	 */
	endp = str;
	if (frontp != str) {
		while (*frontp)
			*endp++ = *frontp++;
		*endp = '\0';
	}

	return (str);
}

/*
 * Can only retreive the sessions/connections for one TID,
 * so this one accepts the parameter TID.
 */
static list_t *
iscsi_retrieve_sessions_stgt(int tid)
{
	int rc = SA_OK, buffer_len;
	char buffer[512], cmd[PATH_MAX];
	char *token, *dup_value;
	FILE *shareiscsi_temp_fp;
	iscsi_session_t *session = NULL;
	list_t *target_sessions = malloc(sizeof (list_t));

	/* For storing the share info */
	char *initiator = NULL, *address = NULL;

	list_create(target_sessions, sizeof (iscsi_session_t),
		    offsetof(iscsi_session_t, next));

	/* CMD: tgtadm --lld iscsi --op show --mode conn --tid TID */
	rc = snprintf(cmd, sizeof (cmd), "%s --lld iscsi --op show "
		    "--mode conn --tid %d", STGT_CMD_PATH, tid);

	if (rc < 0 || rc >= sizeof (cmd))
		return (NULL);

#ifdef DEBUG
	fprintf(stderr, "CMD: %s\n", cmd);
#endif

	shareiscsi_temp_fp = popen(cmd, "r");
	if (shareiscsi_temp_fp == NULL)
		return (NULL);

	while (fgets(buffer, sizeof (buffer), shareiscsi_temp_fp) != 0) {
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

		token = strchr(buffer, ':');
		dup_value = strdup(token + 2);

		if (strncmp(buffer, "        Initiator: ", 19) == 0) {
			initiator = dup_value;
		} else if (strncmp(buffer, "        IP Address: ", 20) == 0) {
			address = dup_value;
		}

		if (initiator == NULL || address == NULL)
			continue; /* Incomplete session definition */

		session = iscsi_session_list_alloc();
		if (session == NULL)
			exit(SA_NO_MEMORY);

		strncpy(session->name, "", sizeof (session->name));
		session->tid = tid;
		strncpy(session->initiator, initiator,
			sizeof (session->initiator));
		strncpy(session->ip, address, sizeof (session->ip));
		session->state = 1;

#ifdef DEBUG
		fprintf(stderr, "iscsi_retrieve_sessions: target=%s, tid=%d, "
			"initiator=%s, ip=%s, state=%d\n",
			session->name, session->tid, session->initiator,
			session->ip, session->state);
#endif

		/* Append the sessions to the list of new sessions */
		list_insert_tail(target_sessions, session);
	}

	if (pclose(shareiscsi_temp_fp) != 0)
		fprintf(stderr, "Failed to pclose stream\n");

	return (target_sessions);
}

/*
 * Retreive user list
 */
static list_t *
iscsi_retrieve_users_stgt(void)
{
	int rc = SA_OK;
	char buffer[512], cmd[PATH_MAX];
	FILE *shareiscsi_temp_fp;
	iscsi_users_t *users = NULL;
	list_t *user_list = malloc(sizeof (list_t));

	/* For storing the user info */
	char *username = NULL;

	list_create(user_list, sizeof (iscsi_users_t),
		    offsetof(iscsi_users_t, next));

	/* CMD: tgtadm --lld iscsi --op show --mode account */
	rc = snprintf(cmd, sizeof (cmd), "%s --lld iscsi --op show "
		"--mode account", STGT_CMD_PATH);

	if (rc < 0 || rc >= sizeof (cmd))
		return (NULL);

#ifdef DEBUG
	fprintf(stderr, "CMD: %s\n", cmd);
#endif

	shareiscsi_temp_fp = popen(cmd, "r");
	if (shareiscsi_temp_fp == NULL)
		return (NULL);

	while (fgets(buffer, sizeof (buffer), shareiscsi_temp_fp) != 0) {
		if (strncmp(buffer, "Account list", 12) == 0)
			continue;

		username = trim(buffer);


		users = (iscsi_users_t *) malloc(sizeof (iscsi_users_t));
		if (users == NULL)
			exit(SA_NO_MEMORY);

		list_link_init(&users->next);

		strncpy(users->username, username, sizeof (users->username));

#ifdef DEBUG
		fprintf(stderr, "iscsi_retrieve_users_stgt: user=%s\n",
			users->username);
#endif

		/* Append the sessions to the list of new sessions */
		list_insert_tail(user_list, users);
	}

	if (pclose(shareiscsi_temp_fp) != 0)
		fprintf(stderr, "Failed to pclose stream\n");

	return (user_list);
}

/*
 * Create a global user
 */
int
iscsi_create_user_stgt(const char *username, const char *passwd)
{
	int rc;
	char *argv[12];

	/*
	 * CMD: tgtadm --lld iscsi --op new --mode account \
	 *      --user user --password pass
	 */
	argv[0]  = STGT_CMD_PATH;
	argv[1]  = (char *)"--lld";
	argv[2]  = (char *)"iscsi";
	argv[3]  = (char *)"--op";
	argv[4]  = (char *)"new";
	argv[5]  = (char *)"--mode";
	argv[6]  = (char *)"account";
	argv[7]  = (char *)"--user";
	argv[8]  = (char *)username;
	argv[9]  = (char *)"--password";
	argv[10] = (char *)passwd;
	argv[11] = NULL;

#ifdef DEBUG
	int i;
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 11; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	if (rc != 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
}

/*
 * ============================================================
 * Core functions
 */

int
iscsi_retrieve_targets_stgt(void)
{
	int rc = SA_OK, buffer_len;
	char buffer[512], cmd[PATH_MAX];
	char *value, *token, *key, *colon;
	FILE *shareiscsi_temp_fp;
	iscsi_session_t *session;
	list_t *sessions;

	/* For soring the targets */
	char *tid = NULL, *name = NULL, *lun = NULL, *state = NULL;
	char *iotype = NULL, *iomode = NULL, *blocks = NULL;
	char *blocksize = NULL, *path = NULL;
	iscsi_target_t *target;

	/* CMD: tgtadm --lld iscsi --op show --mode target */
	rc = snprintf(cmd, sizeof (cmd), "%s --lld iscsi --op show "
		    "--mode target", STGT_CMD_PATH);

	if (rc < 0 || rc >= sizeof (cmd))
		return (SA_SYSTEM_ERR);

#ifdef DEBUG
	fprintf(stderr, "CMD: %s\n", cmd);
#endif

	shareiscsi_temp_fp = popen(cmd, "r");
	if (shareiscsi_temp_fp == NULL)
		return (SA_SYSTEM_ERR);

	while (fgets(buffer, sizeof (buffer), shareiscsi_temp_fp) != 0) {
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

		if (strncmp(buffer, "Target ", 7) == 0) {
			/* Target definition */
			/* => Target 1: iqn.2012-11.com.bayour:test */

			/* Split the line in three, separated by space */
			token = strchr(buffer, ' ');
			while (token != NULL) {
				colon = strchr(token, ':');

				if (colon == NULL)
					goto next_token;

				key = token + 1;
				value = colon + 2;
				*colon = '\0';

				tid = strdup(key);
				if (tid == NULL)
					exit(SA_NO_MEMORY);

				name = strdup(value);
				if (name == NULL)
					exit(SA_NO_MEMORY);
next_token:
				token = strtok(NULL, " ");
			}
		} else if (strncmp(buffer, "        LUN: ", 13) == 0) {
			/* LUN */
			token = strchr(buffer, ':');
			lun = strdup(token + 2);
		} else if (strncmp(buffer, "            Online: ",
			    20) == 0) {
			/* STATUS */
			token = strchr(buffer, ':');
			state = strdup(token + 2);
		} else if (strncmp(buffer, "            Backing store path: ",
			    32) == 0) {
			/* PATH */
			token = strchr(buffer, ':');
			path = strdup(token + 2);

			if (strncmp(path, "None", 4) == 0) {
				/*
				 * For some reason it isn't possible to
				 * add a path to the first LUN, so it's
				 * done in the second...
				 * Reset the variables and try again in
				 * the next loop round.
				 */
				lun = NULL;
				path = NULL;
			}
		}

		if (tid == NULL || name == NULL || lun == NULL ||
		    state == NULL || path == NULL)
			continue; /* Incomplete target definition */

		target = (iscsi_target_t *) malloc(sizeof (iscsi_target_t));
		if (target == NULL) {
			rc = SA_NO_MEMORY;
			goto retrieve_targets_stgt_out;
		}

		/* Save the values in the struct */
		target->tid = atoi(tid);
		target->lun = atoi(lun);
		if (strncmp(state, "Yes", 3))
			target->state = 1;
		else
			target->state = 0;
		strncpy(target->name, name, sizeof (target->name));
		strncpy(target->path, path, sizeof (target->path));

		/* Get all sessions for this TID */
		sessions = iscsi_retrieve_sessions_stgt(target->tid);

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
		fprintf(stderr, "iscsi_retrieve_targets_stgt: "
			"target=%s, tid=%d, lun=%d, path=%s, active=%d\n",
			target->name, target->tid, target->lun, target->path,
			target->session ? target->session->state : -1);
#endif

		/* Append the target to the list of new targets */
		list_insert_tail(&all_iscsi_targets_list, target);

		tid = name = NULL;
		lun = state = iotype = iomode = NULL;
		blocks = blocksize = path = NULL;
	}

retrieve_targets_stgt_out:
	if (pclose(shareiscsi_temp_fp) != 0)
		fprintf(stderr, "Failed to pclose stream\n");

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
iscsi_enable_share_one_stgt(sa_share_impl_t impl_share, int tid)
{
	int rc = SA_OK, ret, user_exists = 0;
	char *argv[18], tid_s[11], vendid[22], iqn[255], *shareopts;
	iscsi_shareopts_t *opts;
	iscsi_users_t *usr;
	list_t *users;
	iscsi_initiator_list_t *initiator;
	list_t *initiators;
#ifdef DEBUG
	int i;
#endif

#ifdef DEBUG
	fprintf(stderr, "iscsi_enable_share_one_stgt: tid=%d, sharepath=%s\n",
		tid, impl_share->sharepath);
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

	/* Parse 'initiator=...' option */
	initiators = iscsi_parse_initiator(opts);

	if (strlen(opts->authname) && strlen(opts->authpass)) {
		/* Get users list */
		users = iscsi_retrieve_users_stgt();

		/* Make sure user exists */
		for (usr = list_head(users);
		    usr != NULL;
		    usr = list_next(users, usr)) {
			if (strcmp(usr->username, opts->authname) == 0) {
				user_exists = 1;
				break;
			}
		}

		if (!user_exists) {
			if ((rc = iscsi_create_user_stgt(opts->authname,
				opts->authpass)) != SA_OK)
				return (SA_SYSTEM_ERR);
		}
	}

	/* int: between -2,147,483,648 and 2,147,483,647 => 10 chars + NUL */
	ret = snprintf(tid_s, sizeof (tid_s), "%d", tid);
	if (ret < 0 || ret >= sizeof (tid_s)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/* Setup the vendor id value */
	ret = snprintf(vendid, sizeof (vendid), "vendor_id=ZFSOnLinux");
	if (ret < 0 || ret >= sizeof (vendid)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}
	vendid[21] = '\0';

	/* Generate an IQN */
	if (!opts->name) {
		if (iscsi_generate_target(impl_share->dataset, iqn,
					    sizeof (iqn)) != SA_OK)
			return (SA_SYSTEM_ERR);
	} else
		strcpy(iqn, opts->name);

	/* TODO: set 'iomode' and 'blocksize' */

	/*
	 * ------
	 * PART 1 - do the (initial) share. No path etc...
	 * CMD: tgtadm --lld iscsi --op new --mode target --tid TID	\
	 *        --targetname `cat /etc/iscsi_target_id`:test
	 */
	argv[0]  = STGT_CMD_PATH;
	argv[1]  = (char *)"--lld";
	argv[2]  = (char *)"iscsi";
	argv[3]  = (char *)"--op";
	argv[4]  = (char *)"new";
	argv[5]  = (char *)"--mode";
	argv[6]  = (char *)"target";
	argv[7]  = (char *)"--tid";
	argv[8]  = tid_s;
	argv[9]  = (char *)"--targetname";
	argv[10] = iqn;
	argv[11] = NULL;

#ifdef DEBUG
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 11; i++)
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
	 * PART 2 - Set share path and lun.
	 * CMD: tgtadm --lld iscsi --op new --mode logicalunit --tid 1 \
	 *      --lun 1 --backing-store /dev/zvol/mypool/tests/iscsi/tst001
	 *      --device-type disk --bstype rdwr
	 */
	argv[6]  = (char *)"logicalunit";
	argv[7]  = (char *)"--tid";
	argv[8]  = tid_s;
	argv[9]  = (char *)"--lun";
	argv[10] = (char *)"1";
	argv[11] = (char *)"--backing-store";
	argv[12] = impl_share->sharepath;
	argv[13] = (char *)"--device-type";
	argv[14] = opts->type;
	argv[15] = (char *)"--bstype";
	argv[16] = opts->iomode;
	argv[17] = NULL;

#ifdef DEBUG
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 17; i++)
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
	 * PART 3 - Bind the target to initiator(s)
	 */
	if (!list_is_empty(initiators)) {
		for (initiator = list_head(initiators);
			initiator != NULL;
			initiator = list_next(initiators, initiator)) {
			/*
			 * CMD: tgtadm --lld iscsi --op bind --mode target \
			 *      --tid 1 --initiator-address <initiator>
			 */
			argv[4]  = (char *)"bind";
			argv[6]  = (char *)"target";
			argv[9]  = (char *)"--initiator-address";
			argv[10] = initiator->initiator;
			argv[11] = NULL;

#ifdef DEBUG
			fprintf(stderr, "CMD: ");
			for (i = 0; i < 11; i++)
				fprintf(stderr, "%s ", argv[i]);
			fprintf(stderr, "\n");
#endif

			rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
			if (rc != 0) {
				free(opts);
				return (SA_SYSTEM_ERR);
			}
		}
	} else {
		/*
		 * CMD: tgtadm --lld iscsi --op bind --mode target --tid 1 \
		 *      --initiator-address ALL
		 */
		argv[4]  = (char *)"bind";
		argv[6]  = (char *)"target";
		argv[9]  = (char *)"--initiator-address";
		argv[10] = (char *)"ALL";
		argv[11] = NULL;

#ifdef DEBUG
		fprintf(stderr, "CMD: ");
		for (i = 0; i < 11; i++)
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
	 * PART 4 - Add user acl
	 * CMD: tgtadm --lld iscsi --op bind --mode account --tid 1 \
	 *      --user user
	 */
	if (strlen(opts->authname) && strlen(opts->authpass)) {
		argv[4]  = (char *)"bind";
		argv[6]  = (char *)"account";
		argv[9]  = (char *)"--user";
		argv[10] = opts->authname;
		argv[11] = NULL;

#ifdef DEBUG
		fprintf(stderr, "CMD: ");
		for (i = 0; i < 11; i++)
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
	 * PART 5 - Set vendor id = ZoL
	 * CMD: tgtadm --lld iscsi --op update --mode logicalunit --tid 1 \
	 *      --lun 1 --params vendor_id=ZFSOnLinux
	 */
	argv[4]  = (char *)"update";
	argv[6]  = (char *)"logicalunit";
	argv[9]  = (char *)"--lun";
	argv[10] = (char *)"1";
	argv[11] = (char *)"--params";
	argv[12] = vendid;
	argv[13] = NULL;

#ifdef DEBUG
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 13; i++)
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
	 * PART 6 - Run local update script.
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

	return (SA_OK);
}

int
iscsi_disable_share_one_stgt(int tid)
{
	int rc = SA_OK, ret;
	char *argv[10], tid_s[11];

	/* int: between -2,147,483,648 and 2,147,483,647 => 10 chars + NUL */
	ret = snprintf(tid_s, sizeof (tid_s), "%d", tid);
	if (ret < 0 || ret >= sizeof (tid_s))
		return (SA_SYSTEM_ERR);

	/* CMD: tgtadm --lld iscsi --op delete --mode target --tid TID */
	argv[0] = STGT_CMD_PATH;
	argv[1] = (char *)"--lld";
	argv[2] = (char *)"iscsi";
	argv[3] = (char *)"--op";
	argv[4] = (char *)"delete";
	argv[5] = (char *)"--mode";
	argv[6] = (char *)"target";
	argv[7] = (char *)"--tid";
	argv[8] = tid_s;
	argv[9] = NULL;

#ifdef DEBUG
	int i;
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 9; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	if (rc != 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
}
