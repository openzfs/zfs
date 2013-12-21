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
 *
 * ZoL will read and modify appropriate files below /sys/kernel/scst_tgt.
 * See iscsi_retrieve_targets_scst() for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
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

/*
 * Preferably we should use the dataset name here, but there's a limit
 * of 16 characters...
 */
static void
iscsi_generate_scst_device_name(char **device)
{
	char string[17];
	static const char valid_salts[] =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789";
	unsigned long i;
	struct timeval tv;

	/* Mske sure that device is set */
	assert(device != NULL);

	/* Seed number for rand() */
	gettimeofday(&tv, NULL);
	srand((tv.tv_sec ^ tv.tv_usec) + getpid());

	/* ASCII characters only */
	for (i = 0; i < sizeof (string) - 1; i++)
		string[i] = valid_salts[rand() % (sizeof (valid_salts) - 1)];
	string[ i ] = '\0';

	*device = strdup(string);
}

/*
 * name:        $SYSFS/targets/iscsi/$name
 * tid:         $SYSFS/targets/iscsi/$name/tid
 * initiator:   $SYSFS/targets/iscsi/$name/sessions/$initiator/
 * sid:         $SYSFS/targets/iscsi/$name/sessions/$initiator/sid
 * cid:         $SYSFS/targets/iscsi/$name/sessions/$initiator/$ip/cid
 * ip:          $SYSFS/targets/iscsi/$name/sessions/$initiator/$ip/ip
 * state:       $SYSFS/targets/iscsi/$name/sessions/$initiator/$ip/state
 */
static list_t *
iscsi_retrieve_sessions_scst(void)
{
	int ret;
	char path[PATH_MAX], tmp_path[PATH_MAX], *buffer = NULL;
	struct stat eStat;
	iscsi_dirs_t *entry1, *entry2, *entry3;
	iscsi_session_t *session = NULL;
	list_t *entries1, *entries2, *entries3;
	list_t *target_sessions = malloc(sizeof (list_t));

	/* For storing the share info */
	char *tid = NULL, *sid = NULL, *cid = NULL, *name = NULL,
		*initiator = NULL, *ip = NULL, *state = NULL;

	list_create(target_sessions, sizeof (iscsi_session_t),
		    offsetof(iscsi_session_t, next));

	/* DIR: $SYSFS/targets/iscsi/$name */
	ret = snprintf(path, sizeof (path), "%s/targets/iscsi", SYSFS_SCST);
	if (ret < 0 || ret >= sizeof (path))
		return (target_sessions);

	entries1 = iscsi_look_for_stuff(path, "iqn.", B_TRUE, 4);
	if (!list_is_empty(entries1))
		return (target_sessions);
	for (entry1 = list_head(entries1);
	    entry1 != NULL;
	    entry1 = list_next(entries1, entry1)) {
		/* DIR: $SYSFS/targets/iscsi/$name */

		/* RETRIEVE name */
		name = entry1->entry;

		/* RETRIEVE tid */
		ret = snprintf(tmp_path, sizeof (tmp_path), "%s/tid",
			    entry1->path);
		if (ret < 0 || ret >= sizeof (tmp_path))
			goto iscsi_retrieve_sessions_scst_error;
		if (iscsi_read_sysfs_value(tmp_path, &buffer) != SA_OK)
			goto iscsi_retrieve_sessions_scst_error;
		if (tid)
			free(tid);
		tid = buffer;
		buffer = NULL;

		ret = snprintf(path, sizeof (path), "%s/sessions",
			    entry1->path);
		if (ret < 0 || ret >= sizeof (path))
			goto iscsi_retrieve_sessions_scst_error;

		entries2 = iscsi_look_for_stuff(path, "iqn.", B_TRUE, 4);
		if (!list_is_empty(entries2))
			goto iscsi_retrieve_sessions_scst_error;
		for (entry2 = list_head(entries2);
		    entry2 != NULL;
		    entry2 = list_next(entries2, entry2)) {
			/* $SYSFS/targets/iscsi/$name/sessions/$initiator */

			/* RETRIEVE initiator */
			initiator = entry2->entry;

			/* RETRIEVE sid */
			ret = snprintf(tmp_path, sizeof (tmp_path), "%s/sid",
				    entry2->path);
			if (ret < 0 || ret >= sizeof (tmp_path))
				goto iscsi_retrieve_sessions_scst_error;
			if (iscsi_read_sysfs_value(tmp_path, &buffer) != SA_OK)
				goto iscsi_retrieve_sessions_scst_error;
			if (sid)
				free(sid);
			sid = buffer;
			buffer = NULL;

			entries3 = iscsi_look_for_stuff(entry2->path, NULL,
							B_TRUE, 4);
			if (!list_is_empty(entries3))
				goto iscsi_retrieve_sessions_scst_error;
			for (entry3 = list_head(entries3);
			    entry3 != NULL;
			    entry3 = list_next(entries3, entry3)) {
				/*
				 * $SYSFS/targets/iscsi/$name/sessions/
				 *     $initiator/$ip
				 */
				ret = snprintf(path, sizeof (path), "%s/cid",
					    entry3->path);
				if (ret < 0 || ret >= sizeof (path))
					goto iscsi_retrieve_sessions_scst_error;
				if (stat(path, &eStat) == -1)
					/* Not a IP directory */
					break;

				/* RETRIEVE ip */
				ip = entry3->entry;

				/* RETRIEVE cid */
				ret = snprintf(tmp_path, sizeof (tmp_path),
					    "%s/cid", entry3->path);
				if (ret < 0 || ret >= sizeof (tmp_path))
					goto iscsi_retrieve_sessions_scst_error;
				if (iscsi_read_sysfs_value(tmp_path, &buffer)
				    != SA_OK)
					goto iscsi_retrieve_sessions_scst_error;
				if (cid)
					free(cid);
				cid = buffer;
				buffer = NULL;

				/* RETRIEVE state */
				ret = snprintf(tmp_path, sizeof (tmp_path),
					    "%s/state", entry3->path);
				if (ret < 0 || ret >= sizeof (tmp_path))
					goto iscsi_retrieve_sessions_scst_error;
				if (iscsi_read_sysfs_value(tmp_path, &buffer)
				    != SA_OK)
					goto iscsi_retrieve_sessions_scst_error;
				if (state)
					free(state);
				state = buffer;
				buffer = NULL;

				/* SAVE values */
				if (tid == NULL || sid == NULL || cid == NULL ||
				    name == NULL || initiator == NULL ||
				    ip == NULL || state == NULL)
					continue; /* Incomplete session def */

				session = iscsi_session_list_alloc();
				if (session == NULL)
					exit(SA_NO_MEMORY);

				session->tid = atoi(tid);
				session->sid = atoi(sid);
				session->cid = atoi(cid);

				strncpy(session->name, name,
					sizeof (session->name));
				strncpy(session->initiator, initiator,
					sizeof (session->initiator));
				strncpy(session->ip, ip,
					sizeof (session->ip));

				session->hd[0] = '\0';
				session->dd[0] = '\0';

				if (strncmp(state, "established", 11) == 0)
					session->state = 1;
				else
					session->state = 0;

#ifdef DEBUG
				fprintf(stderr, "iscsi_retrieve_sessions: "
					"target=%s, tid=%d, sid=%d, cid=%d, "
					"initiator=%s, ip=%s, state=%d\n",
					session->name, session->tid,
					session->sid, session->cid,
					session->initiator, session->ip,
					session->state);
#endif

				/* Append the sessions to the list of new */
				list_insert_tail(target_sessions, session);

				/* Clear variables */
				free(tid);
				free(sid);
				free(cid);
				free(state);
				name = tid = sid = cid = ip = NULL;
				initiator = state = NULL;
			}
		}
	}

iscsi_retrieve_sessions_scst_error:
	free(tid);
	free(sid);
	free(cid);
	free(state);

	return (target_sessions);
}

/*
 * ============================================================
 * Core functions
 */

int
iscsi_retrieve_targets_scst(void)
{
	char path[PATH_MAX], tmp_path[PATH_MAX], *buffer, *link = NULL;
	int rc = SA_SYSTEM_ERR, ret;
	iscsi_dirs_t *entry1, *entry2, *entry3;
	list_t *entries1, *entries2, *entries3;
	iscsi_target_t *target;
	iscsi_session_t *session;
	list_t *sessions;

	/* For storing the share info */
	char *tid = NULL, *lun = NULL, *state = NULL, *blocksize = NULL;
	char *name = NULL, *iotype = NULL, *dev_path = NULL, *device = NULL;

	/* Get all sessions */
	sessions = iscsi_retrieve_sessions_scst();

	/* DIR: /sys/kernel/scst_tgt/targets */
	ret = snprintf(path, sizeof (path), "%s/targets", SYSFS_SCST);
	if (ret < 0 || ret >= sizeof (path))
		return (SA_SYSTEM_ERR);

	entries1 = iscsi_look_for_stuff(path, "iscsi", B_TRUE, 0);
	for (entry1 = list_head(entries1);
	    entry1 != NULL;
	    entry1 = list_next(entries1, entry1)) {
		entries2 = iscsi_look_for_stuff(entry1->path, "iqn.",
						B_TRUE, 4);
		for (entry2 = list_head(entries2);
		    entry2 != NULL;
		    entry2 = list_next(entries2, entry2)) {
			/* DIR: /sys/kernel/scst_tgt/targets/iscsi/iqn.* */

			/* Save the share name */
			name = entry2->entry;

			/* RETRIEVE state */
			ret = snprintf(tmp_path, sizeof (tmp_path),
					"%s/enabled", entry2->path);
			if (ret < 0 || ret >= sizeof (tmp_path))
				goto retrieve_targets_scst_out;
			if (iscsi_read_sysfs_value(tmp_path, &buffer) != SA_OK)
				goto retrieve_targets_scst_out;
			state = buffer;
			buffer = NULL;

			/* RETRIEVE tid */
			ret = snprintf(tmp_path, sizeof (tmp_path), "%s/tid",
					entry2->path);
			if (ret < 0 || ret >= sizeof (tmp_path))
				goto retrieve_targets_scst_out;
			if (iscsi_read_sysfs_value(tmp_path, &buffer) != SA_OK)
				goto retrieve_targets_scst_out;
			tid = buffer;
			buffer = NULL;

			/* RETRIEVE lun(s) */
			ret = snprintf(tmp_path, sizeof (tmp_path),
					"%s/luns", entry2->path);
			if (ret < 0 || ret >= sizeof (tmp_path))
				goto retrieve_targets_scst_out;

			entries3 = iscsi_look_for_stuff(tmp_path, NULL,
							B_TRUE, 0);
			for (entry3 = list_head(entries3);
			    entry3 != NULL;
			    entry3 = list_next(entries3, entry3)) {
				lun = entry3->entry;

				/* RETRIEVE blocksize */
				ret = snprintf(tmp_path, sizeof (tmp_path),
						"%s/luns/%s/device/blocksize",
						entry2->path, lun);
				if (ret < 0 || ret >= sizeof (tmp_path))
					goto retrieve_targets_scst_out;
				if (iscsi_read_sysfs_value(tmp_path, &buffer)
				    != SA_OK)
					goto retrieve_targets_scst_out;
				blocksize = buffer;
				buffer = NULL;

				/* RETRIEVE block device path */
				ret = snprintf(tmp_path, sizeof (tmp_path),
						"%s/luns/%s/device/filename",
						entry2->path, lun);
				if (ret < 0 || ret >= sizeof (tmp_path))
					goto retrieve_targets_scst_out;
				if (iscsi_read_sysfs_value(tmp_path, &buffer)
				    != SA_OK)
					goto retrieve_targets_scst_out;
				dev_path = buffer;
				buffer = NULL;

				/*
				 * RETRIEVE scst device name
				 * trickier: '6550a239-iscsi1' (s/.*-//)
				 */
				ret = snprintf(tmp_path, sizeof (tmp_path),
					    "%s/luns/%s/device/t10_dev_id",
					    entry2->path, lun);
				if (ret < 0 || ret >= sizeof (tmp_path))
					goto retrieve_targets_scst_out;
				if (iscsi_read_sysfs_value(tmp_path, &buffer)
				    != SA_OK)
					goto retrieve_targets_scst_out;
				device = strstr(buffer, "-") + 1;
				buffer = NULL;

				/*
				 * RETRIEVE iotype
				 * tricker: it's only availible in the
				 * link.
				 *
				 * $SYSFS/targets/iscsi/$name/luns/0/
				 *   device/handler
				 * => /sys/kernel/scst_tgt/handlers/
				 *   vdisk_blockio
				 */
				ret = snprintf(tmp_path, sizeof (tmp_path),
						"%s/luns/%s/device/handler",
						entry2->path, lun);
				if (ret < 0 || ret >= sizeof (tmp_path))
					goto retrieve_targets_scst_out;

				link = (char *) calloc(PATH_MAX, 1);
				if (link == NULL) {
					rc = SA_NO_MEMORY;
					goto retrieve_targets_scst_out;
				}

				if (readlink(tmp_path, link, PATH_MAX) == -1) {
					rc = errno;
					goto retrieve_targets_scst_out;
				}
				link[strlen(link)] = '\0';
				iotype = strstr(link, "_") + 1;

				/* TODO: Retrieve iomode */

				target = (iscsi_target_t *)
					malloc(sizeof (iscsi_target_t));
				if (target == NULL) {
					rc = SA_NO_MEMORY;
					goto retrieve_targets_scst_out;
				}

				target->tid = atoi(tid);
				target->lun = atoi(lun);
				target->state = atoi(state);
				target->blocksize = atoi(blocksize);

				strncpy(target->name,	name,
					sizeof (target->name));
				strncpy(target->path,	dev_path,
					sizeof (target->path));
				strncpy(target->device,	device,
					sizeof (target->device));
				strncpy(target->iotype,	iotype,
					sizeof (target->iotype));
				/*
				 * TODO
				 * strncpy(target->iomode,	iomode,
				 *	sizeof (target->iomode));
				 */

				/* Link the session here */
				target->session = NULL;
				for (session = list_head(sessions);
				    session != NULL;
				    session = list_next(sessions, session)) {
					if (session->tid == target->tid) {
						target->session = session;
						list_link_init(
						    &target->session->next);

						break;
					}
				}

#ifdef DEBUG
				fprintf(stderr, "iscsi_retrieve_targets_scst: "
					"target=%s, tid=%d, lun=%d, path=%s\n",
					target->name, target->tid, target->lun,
					target->path);
#endif

				/* Append the target to the list of new trgs */
				list_insert_tail(&all_iscsi_targets_list,
					    target);
			}
		}
	}

	free(link);

	return (SA_OK);

retrieve_targets_scst_out:
	free(link);

	return (rc);
}

/* NOTE: TID is not use with SCST - it's autogenerated at create time. */
int
iscsi_enable_share_one_scst(sa_share_impl_t impl_share, int tid)
{
	int rc, ret;
	char *argv[3], *shareopts, *device, buffer[255], path[PATH_MAX];
	iscsi_shareopts_t *opts;
	iscsi_initiator_list_t *initiator;
	list_t *initiators;

#ifdef DEBUG
	fprintf(stderr, "iscsi_enable_share_one_scst: tid=%d, sharepath=%s\n",
		tid, impl_share->sharepath);
#endif

	opts = (iscsi_shareopts_t *) malloc(sizeof (iscsi_shareopts_t));
	if (opts == NULL)
		return (SA_NO_MEMORY);

	/* Get any share options */
	shareopts = FSINFO(impl_share, iscsi_fstype)->shareopts;
	rc = iscsi_get_shareopts(impl_share, shareopts, &opts);
	if (rc != SA_OK) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/* Generate a scst device name from the dataset name */
	iscsi_generate_scst_device_name(&device);

	/* Parse 'initiator=...' option */
	initiators = iscsi_parse_initiator(opts);

#ifdef DEBUG
	fprintf(stderr, "iscsi_enable_share_one_scst: name=%s, iomode=%s, "
		"type=%s, lun=%d, blocksize=%d,  authname=%s, authpass=%s\n",
		opts->name, opts->iomode, opts->type, opts->lun,
		opts->blocksize, opts->authname, opts->authpass);
#endif

	/*
	 * ======
	 * PART 1 - Add target
	 * CMD: echo "add_target IQN" > $SYSFS/targets/iscsi/mgmt
	 */
	ret = sprintf(path, "%s/targets/iscsi/mgmt", SYSFS_SCST);
	if (ret < 0 || ret >= sizeof (buffer)) {
		free(opts);
		return (SA_NO_MEMORY);
	}

	ret = snprintf(buffer, sizeof (buffer), "add_target %s", opts->name);
	if (ret < 0 || ret >= sizeof (buffer)) {
		free(opts);
		return (SA_NO_MEMORY);
	}
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
		free(opts);
		return (SA_NO_MEMORY);
	}

	/*
	 * ======
	 * PART 2 - Add device
	 * CMD: echo "add_device DEV filename=/dev/zvol/$vol;blocksize=512" \
	 *	> $SYSFS/handlers/vdisk_blockio/mgmt
	 */
	ret = snprintf(path, sizeof (buffer), "%s/handlers/vdisk_%s/mgmt",
			SYSFS_SCST, opts->type);
	if (ret < 0 || ret >= sizeof (path)) {
		free(opts);
		return (SA_NO_MEMORY);
	}
	ret = snprintf(buffer, sizeof (buffer), "add_device %s filename=%s; "
			"blocksize=%d", device, impl_share->sharepath,
			opts->blocksize);
	if (ret < 0 || ret >= sizeof (buffer)) {
		free(opts);
		return (SA_NO_MEMORY);
	}
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
		free(opts);
		return (SA_NO_MEMORY);
	}

	if (!strlen(opts->authname) || list_is_empty(initiators)) {
		/*
		 * ======
		 * PART 3 - Add lun
		 * -> target based authentication
		 * CMD: echo "add DEV 0" > $SYSFS/targets/iscsi/IQN/luns/mgmt
		 */
		ret = snprintf(path, sizeof (path),
			"%s/targets/iscsi/%s/luns/mgmt",
			SYSFS_SCST, opts->name);
		if (ret < 0 || ret >= sizeof (path)) {
			free(opts);
			return (SA_NO_MEMORY);
		}
		ret = snprintf(buffer, sizeof (buffer), "add %s %d", device,
			opts->lun);
		if (ret < 0 || ret >= sizeof (buffer)) {
			free(opts);
			return (SA_NO_MEMORY);
		}
		if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
			free(opts);
			return (SA_NO_MEMORY);
		}
	}

	/*
	 * PART 4a - work in per-portal access control mode
	 * CMD: echo 1 > $SYSFS/targets/iscsi/IQN/per_portal_acl
	 */
	ret = sprintf(path, "%s/targets/iscsi/%s/per_portal_acl",
			SYSFS_SCST, opts->name);
	if (ret < 0 || ret >= sizeof (buffer)) {
		free(opts);
		return (SA_NO_MEMORY);
	}
	strcpy(buffer, "1");
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
		free(opts);
		return (SA_NO_MEMORY);
	}

	/* PART 4 - access control */

	/*
	 * PART 4b - set user+pass authentication
	 * CMD: echo "add_target_attribute IQN IncomingUser USER PASS"	\
	 *      > $SYSFS/targets/iscsi/mgmt
	 */
	ret = sprintf(path, "%s/targets/iscsi/mgmt", SYSFS_SCST);
	if (ret < 0 || ret >= sizeof (buffer)) {
		free(opts);
		return (SA_NO_MEMORY);
	}
	ret = snprintf(buffer, sizeof (buffer),
		"add_target_attribute %s IncomingUser %s %s",
		opts->name, opts->authname, opts->authpass);
	if (ret < 0 || ret >= sizeof (buffer)) {
		free(opts);
		return (SA_NO_MEMORY);
	}
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
		free(opts);
		return (SA_NO_MEMORY);
	}

	for (initiator = list_head(initiators);
		initiator != NULL;
		initiator = list_next(initiators, initiator)) {
		/*
		 * PART 4c - create security group
		 * CMD: echo "create GROUP" \
		 *      > $SYSFS/targets/iscsi/IQN/ini_groups/mgmt
		 *
		 * NOTE: We use the initiator name as group name.
		 */
		ret = snprintf(path, sizeof (path),
			"%s/targets/iscsi/%s/ini_groups/mgmt",
			SYSFS_SCST, opts->name);
		if (ret < 0 || ret >= sizeof (path)) {
			free(opts);
			return (SA_NO_MEMORY);
		}
		ret = snprintf(buffer, sizeof (buffer), "create %s",
			initiator->initiator);
		if (ret < 0 || ret >= sizeof (buffer)) {
			free(opts);
			return (SA_NO_MEMORY);
		}
		if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
			free(opts);
			return (SA_NO_MEMORY);
		}

		/*
		 * PART 4d - add lun to security group
		 * -> initiator based authentication
		 * CMD: echo "add DEV LUN read_only=[01]" \
		 * > $SYSFS/targets/iscsi/IQN/ini_groups/GROUP/luns/mgmt
		 */
		ret = snprintf(path, sizeof (path),
			"%s/targets/iscsi/%s/ini_groups/%s/luns/mgmt",
			SYSFS_SCST, opts->name, initiator->initiator);
		if (ret < 0 || ret >= sizeof (path)) {
			free(opts);
			return (SA_NO_MEMORY);
		}
		ret = snprintf(buffer, sizeof (buffer), "add %s %d "
			"read_only=%d", device, opts->lun,
			initiator->read_only);
		if (ret < 0 || ret >= sizeof (buffer)) {
			free(opts);
			return (SA_NO_MEMORY);
		}
		if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
			free(opts);
			return (SA_NO_MEMORY);
		}

		/*
		 * PART 4e - add initiator to security group
		 * CMD: echo "add IQN" \
		 * > $SYSFS/targets/iscsi/IQN/ini_groups/GRP/initiators/mgmt
		 */
		ret = snprintf(path, sizeof (path),
			"%s/targets/iscsi/%s/ini_groups/%s/"
			"initiators/mgmt", SYSFS_SCST, opts->name,
			initiator->initiator);
		if (ret < 0 || ret >= sizeof (path)) {
			free(opts);
			return (SA_NO_MEMORY);
		}

		ret = snprintf(buffer, sizeof (buffer), "add %s",
			initiator->initiator);
		if (ret < 0 || ret >= sizeof (buffer)) {
			free(opts);
			return (SA_NO_MEMORY);
		}
		if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
			free(opts);
			return (SA_NO_MEMORY);
		}
	}

	/*
	 * ======
	 * PART 5 - Enable target
	 * CMD: echo 1 > $SYSFS/targets/iscsi/$name/enabled
	 */
	ret = snprintf(path, sizeof (path), "%s/targets/iscsi/%s/enabled",
			SYSFS_SCST, opts->name);
	if (ret < 0 || ret >= sizeof (path)) {
		free(opts);
		return (SA_NO_MEMORY);
	}
	strcpy(buffer, "1");
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK) {
		free(opts);
		return (SA_NO_MEMORY);
	}

	/*
	 * ======
	 * PART 6 - Run local update script.
	 */
	if (access(EXTRA_ISCSI_SHARE_SCRIPT, X_OK) == 0) {
		/* CMD: /sbin/zfs_share_iscsi <TID> */
		argv[0] = (char *)EXTRA_ISCSI_SHARE_SCRIPT;
		argv[1] = opts->name;
		argv[2] = NULL;

#ifdef DEBUG
		int i;
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
iscsi_disable_share_one_scst(int tid)
{
	int ret;
	char path[PATH_MAX], buffer[255];
	iscsi_target_t *target;

	for (target = list_head(&all_iscsi_targets_list);
	    target != NULL;
	    target = list_next(&all_iscsi_targets_list, target)) {
		if (target->tid == tid) {
#ifdef DEBUG
			fprintf(stderr, "iscsi_disable_share_one_scst: "
				"target=%s, tid=%d, path=%s, iotype=%s\n",
				target->name, target->tid,
				target->path, target->iotype);
#endif

			break;
		}
	}

	/*
	 * ======
	 * PART 1 - Disable target
	 * CMD: echo 0 > $SYSFS/targets/iscsi/$name/enabled
	 */
	ret = snprintf(path, sizeof (path), "%s/targets/iscsi/%s/enabled",
			SYSFS_SCST, target->name);
	if (ret < 0 || ret >= sizeof (path))
		return (SA_SYSTEM_ERR);
	strcpy(buffer, "0");
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 2 - Delete device
	 *
	 * dev=`/bin/ls -l \
	 *  $SYSFS/targets/iscsi/$name/luns/0/device | sed 's@.*\/@@'`
	 * echo "del_device $dev" > $SYSFS/handlers/vdisk_blockio/mgmt
	 */
	ret = snprintf(path, sizeof (path), "%s/handlers/vdisk_%s/mgmt",
			SYSFS_SCST, target->iotype);
	if (ret < 0 || ret >= sizeof (path))
		return (SA_SYSTEM_ERR);
	ret = snprintf(buffer, sizeof (buffer), "del_device %s",
		    target->device);
	if (ret < 0 || ret >= sizeof (buffer))
		return (SA_SYSTEM_ERR);
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 3 - Delete target
	 * CMD: echo "del_target $name" > $SYSFS/targets/iscsi/mgmt
	 */
	ret = snprintf(path, sizeof (path), "%s/targets/iscsi/mgmt",
			SYSFS_SCST);
	if (ret < 0 || ret >= sizeof (path))
		return (SA_SYSTEM_ERR);
	ret = snprintf(buffer, sizeof (buffer), "del_target %s",
		    target->name);
	if (ret < 0 || ret >= sizeof (buffer))
		return (SA_SYSTEM_ERR);
	if (iscsi_write_sysfs_value(path, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	return (SA_OK);
}
