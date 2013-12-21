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
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <libzfs.h>
#include <libshare.h>
#include <sys/fs/zfs.h>
#include <libgen.h>
#include "libshare_impl.h"
#include "iscsi.h"

/* Defined in iscsi_scst.c - COULD move it to iscsi.c, but... */
extern list_t *iscsi_look_for_stuff(char *, const char *, boolean_t, int);

/*
 * ============================================================
 * Support functions
 */

static const char *
iscsi_generate_lio_serialno(void)
{
	static char string[34];
	static const char valid_salts[] =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789";
	unsigned long i;
	struct timeval tv;

	/* Seed number for rand() */
	gettimeofday(&tv, NULL);
	srand((tv.tv_sec ^ tv.tv_usec) + getpid());

	/* ASCII characters only */
	for (i = 0; i < sizeof (string) - 1; i++)
		string[i] = valid_salts[rand() % (sizeof (valid_salts) - 1)];
	string[ i ] = '\0';

	return (string);
}

/* Mabye put in iscsi.c or libshare.c, but so far it's only used here */
static const char *
iscsi_get_ipaddress(void)
{
	int rc;
	char hostname[1024];
	static char addrstr[100];
	struct addrinfo *result;
	void *ptr = NULL;

	/* Get the hostname */
	if (gethostname(hostname, 1024) != 0)
		return (NULL);

	/* resolve the domain name into a list of addresses */
	rc = getaddrinfo(hostname, NULL, NULL, &result);
	if (rc != 0)
		return (NULL);

	inet_ntop(result->ai_family, result->ai_addr->sa_data, addrstr, 100);
	switch (result->ai_family) {
	case AF_INET:
		ptr = &((struct sockaddr_in *) result->ai_addr)->sin_addr;
		break;
	case AF_INET6:
		ptr = &((struct sockaddr_in6 *) result->ai_addr)->sin6_addr;
		break;
	}
	inet_ntop(result->ai_family, ptr, addrstr, 100);

	freeaddrinfo(result);
	return (addrstr);
}

// TODO
static list_t *
iscsi_retrieve_sessions_lio(void)
{
	list_t *target_sessions = malloc(sizeof (list_t));

	list_create(target_sessions, sizeof (iscsi_session_t),
		    offsetof(iscsi_session_t, next));

// $SYSFS/iscsi/IQN/tpgt_TID/acls/INITIATOR/info
// => No active iSCSI Session for Initiator Endpoint: INITIATOR
// OR:
// => InitiatorName: INITIATOR
//    InitiatorAlias: 
//    LIO Session ID: 2   ISID: 0x80 12 34 56 dd 65  TSIH: 2  SessionType: Normal
//    Session State: TARG_SESS_STATE_LOGGED_IN
//    ---------------------[iSCSI Session Values]-----------------------
//      CmdSN/WR  :  CmdSN/WC  :  ExpCmdSN  :  MaxCmdSN  :     ITT    :     TTT
//     0x00000010   0x00000010   0x00001f53   0x00001f62   0x531f0000   0x00000d81
//    ----------------------[iSCSI Connections]-------------------------
//    CID: 1  Connection State: TARG_CONN_STATE_LOGGED_IN
//       Address 192.168.69.3 TCP  StatSN: 0x1a00d3e6

	return (target_sessions);
}

/* Setup the device part of a target */
static int
iscsi_setup_device_lio(sa_share_impl_t impl_share, iscsi_shareopts_t *opts,
	int tid, const char *serno)
{
	int ret;
	char path1[PATH_MAX], buffer[255];
	mode_t dirmode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	/*
	 * ======
	 * PART 1a - Setup the path
	 */
	ret = snprintf(path1, sizeof (path1), "%s/core/%s_%d/%s",
			SYSFS_LIO, opts->type, tid, opts->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

#if DEBUG >= 2
	fprintf(stderr, "mkdir -p %s\n", path1);
#endif
	if ((mkdirp(path1, dirmode) < 0) && (errno != EEXIST)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/*
	 * ======
	 * PART 1b1 - set device
	 */
	ret = snprintf(path1, sizeof (path1), "%s/core/%s_%d/%s/udev_path",
			SYSFS_LIO, opts->type, tid, opts->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	ret = snprintf(buffer, sizeof (buffer), "%s",
		    impl_share->sharepath);
	if (ret < 0 || ret >= sizeof (buffer))
		return (SA_SYSTEM_ERR);

	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 1b2 - set device
	 */
	ret = snprintf(path1, sizeof (path1), "%s/core/%s_%d/%s/control",
			SYSFS_LIO, opts->type, tid, opts->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	if (strcmp(opts->type, "iblock") == 0) {
		ret = snprintf(buffer, sizeof (buffer), "udev_path=%s",
			impl_share->sharepath);
	} else if (strcmp(opts->type, "fileio") == 0) {
		ret = snprintf(buffer, sizeof (buffer), "fd_dev_name=%s",
			impl_share->sharepath);
	} else
		/* Invalid type */
		ret = -1;
	if (ret < 0 || ret >= sizeof (buffer))
		return (SA_SYSTEM_ERR);

	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 1c2 - Set serial number
	 */
	ret = snprintf(path1, sizeof (path1),
			"%s/core/%s_%d/%s/wwn/vpd_unit_serial",
			SYSFS_LIO, opts->type, tid, opts->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	ret = snprintf(buffer, sizeof (buffer), "%s", serno);
	if (ret < 0 || ret >= sizeof (buffer))
		return (SA_SYSTEM_ERR);

	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 1d - Enable
	 */
	ret = snprintf(path1, sizeof (path1),
			"%s/core/%s_%d/%s/enable",
			SYSFS_LIO, opts->type, tid, opts->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	strcpy(buffer, "1");

	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 1e - Set block size
	 */
	ret = snprintf(path1, sizeof (path1),
			"%s/core/%s_%d/%s/attrib/block_size",
			SYSFS_LIO, opts->type, tid, opts->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	ret = snprintf(buffer, sizeof (buffer), "%d",
			opts->blocksize);
	if (ret < 0 || ret >= sizeof (buffer))
		return (SA_SYSTEM_ERR);

	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/* Setup the iqn/iscsi part of a target */
static int
iscsi_setup_iqn_lio(sa_share_impl_t impl_share, iscsi_shareopts_t *opts,
	char *iqn, int tid, const char *serno)
{
	int ret;
	char path1[PATH_MAX], path2[PATH_MAX], buffer[255];
	const char *ip;
	mode_t dirmode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	/*
	 * ======
	 * PART 2a2 - Get IP address
	 */
	ip = iscsi_get_ipaddress();

	/*
	 * ======
	 * PART 2a3 - Setup path
	 */
	ret = snprintf(path1, sizeof (path1),
			"%s/iscsi/%s/tpgt_%d/np/%s:3260",
			SYSFS_LIO, iqn, tid, ip);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

#if DEBUG >= 2
	fprintf(stderr, "mkdir -p %s\n", path1);
#endif
	if ((mkdirp(path1, dirmode) < 0) && (errno != EEXIST)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/*
	 * ======
	 * PART 2b - Setup symbolic link paths
	 */
	ret = snprintf(path1, sizeof (path1),
			"%s/iscsi/%s/tpgt_%d/lun/lun_%d",
			SYSFS_LIO, iqn, tid, opts->lun);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

#if DEBUG >= 2
	fprintf(stderr, "mkdir -p %s\n", path1);
#endif
	if ((mkdirp(path1, dirmode) < 0) && (errno != EEXIST)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	/*
	 * ======
	 * PART 2b - Create symbolic link
	 */
	ret = snprintf(path1, sizeof (path1), "%s/core/%s_%d/%s",
			SYSFS_LIO, opts->type, tid, opts->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	ret = snprintf(path2, sizeof (path2),
			"%s/iscsi/%s/tpgt_%d/lun/lun_%d/%s",
			SYSFS_LIO, iqn, tid, opts->lun, serno);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

#if DEBUG >= 2
	fprintf(stderr, "ln -s %s %s\n", path1, path2);
#endif
	if (symlink(path1, path2) != 0)
		return (SA_SYSTEM_ERR);

	/*
	 * ======
	 * PART 2c - Disable enforce_discovery_auth
	 */
	ret = snprintf(path1, sizeof (path1),
			"%s/iscsi/discovery_auth/enforce_discovery_auth",
			SYSFS_LIO);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	strcpy(buffer, "0");

	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	return (SA_OK);
}

/* Setup the acl/mapped lun part of a target */
static int
iscsi_setup_acl_lio(sa_share_impl_t impl_share, iscsi_shareopts_t *opts,
	char *iqn, int tid, const char *serno)
{
	int ret;
	char path1[PATH_MAX], path2[PATH_MAX], buffer[255];
	mode_t dirmode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	iscsi_initiator_list_t *initiator;
	list_t *initiators;

	/* Parse initiators option */
	initiators = iscsi_parse_initiator(opts);
	if (list_is_empty(initiators))
		return (SA_OK);
	for (initiator = list_head(initiators);
		initiator != NULL;
		initiator = list_next(initiators, initiator)) {

		/*
		 * ======
		 * PART 3a - Create initiator ACL directory
		 */
		ret = snprintf(path1, sizeof (path1),
				"%s/iscsi/%s/tpgt_%d/acls/%s/lun_%d",
				SYSFS_LIO, iqn, tid, initiator->initiator,
				opts->lun);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);

#if DEBUG >= 2
		fprintf(stderr, "mkdir -p %s\n", path1);
#endif
		if ((mkdirp(path1, dirmode) < 0) && (errno != EEXIST)) {
			free(opts);
			return (SA_SYSTEM_ERR);
		}

		/*
		 * ======
		 * PART 3b - Set default cmdsn_depth
		 */
		ret = snprintf(path1, sizeof (path1),
				"%s/iscsi/%s/tpgt_%d/acls/%s/cmdsn_depth",
				SYSFS_LIO, iqn, tid, initiator->initiator);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);

		strcpy(buffer, "16");

		if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
			return (SA_NO_MEMORY);

		/*
		 * ======
		 * PART 3c - Create symbolic link
		 *
		 * CMD: ln -s SYSFS_LIO/iscsi/IQN/tpgt_TID/lun/lun_LUN	\
		 *	SYSFS_LIO/iscsi/IQN/tpgt_TID/acls/I_IQN/lun_LUn/SERNO
		 */
		ret = snprintf(path1, sizeof (path1),
				"%s/iscsi/%s/tpgt_%d/lun/lun_%d",
				SYSFS_LIO, iqn, tid, opts->lun);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);

		ret = snprintf(path2, sizeof (path2),
				"%s/iscsi/%s/tpgt_%d/acls/%s/lun_%d/%s",
				SYSFS_LIO, iqn, tid, initiator->initiator,
				opts->lun, serno);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);

#if DEBUG >= 2
		fprintf(stderr, "ln -s %s %s\n", path1, path2);
#endif
		if (symlink(path1, path2) != 0)
			return (SA_SYSTEM_ERR);

		/*
		 * ======
		 * PART 3d - Set rw/ro mode
		 */
		ret = snprintf(path1, sizeof (path1),
				"%s/iscsi/%s/tpgt_%d/acls/"
				"%s/lun_%d/write_protect",
				SYSFS_LIO, iqn, tid,
				initiator->initiator, opts->lun);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);

		if (initiator->read_only)
			strcpy(buffer, "1");
		else
			strcpy(buffer, "0");

		if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
			return (SA_NO_MEMORY);

		/*
		 * ======
		 * PART 3e - Set auth name and password
		 */
		if (strlen(opts->authname) && strlen(opts->authpass)) {
			/* Set username */
			ret = snprintf(path1, sizeof (path1),
				"%s/iscsi/%s/tpgt_%d/acls/%s/auth/userid",
				SYSFS_LIO, iqn, tid, initiator->initiator);
			if (ret < 0 || ret >= sizeof (path1))
				return (SA_SYSTEM_ERR);

			strcpy(buffer, opts->authname);

			if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
				return (SA_NO_MEMORY);

			/* Set password */
			ret = snprintf(path1, sizeof (path1),
				"%s/iscsi/%s/tpgt_%d/acls/%s/auth/password",
				SYSFS_LIO, iqn, tid, initiator->initiator);
			if (ret < 0 || ret >= sizeof (path1))
				return (SA_SYSTEM_ERR);

			strcpy(buffer, opts->authpass);

			if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
				return (SA_NO_MEMORY);
		}
	}

	return (SA_OK);
}

/*
 * ============================================================
 * Core functions
 */

/*
 * find $SYSFS_LIO/core/{iblock,fileio}_* -maxdepth 0 -type d |
 *   while read bstore; do
 *     tid=$(echo $bstore | sed 's@.*_@@')
 *     iqn=$(find $bstore/\* -maxdepth 0 -type d | sed 's@.*\/@@')
 *     dev=$(cat $(find $bstore/\* -maxdepth 0 -type d)/udev_path)
 *     lun=$(find $SYSFS_LIO/iscsi/$iqn/tpgt_1/lun/\* -maxdepth 0 -type d | \
 *       sed 's@.*_@@')
 *     blksize=$(cat $(find $bstore/\* -maxdepth 0 -type d)/attrib/block_size)
 *   done
 */
int
iscsi_retrieve_targets_lio(void)
{
	int ret;
	char path[PATH_MAX], path2[PATH_MAX], *buffer = NULL;
	iscsi_session_t *session;
	iscsi_target_t *target;
	iscsi_dirs_t *entry1, *entry2;
	list_t *entries1, *entries2, *sessions;

	/* For storing the share info */
	char *tid = NULL, *lun = NULL, *blocksize = NULL, *name = NULL,
		*dev_path = NULL, *iqn = NULL, *tmp = NULL;
	char type[255];
	/* TODO: *iotype = NULL, */

	/*
	 * ======
	 * Retreive values we need
	 */

	/* Get all sessions */
	sessions = iscsi_retrieve_sessions_lio();

	/* DIR: /sys/kernel/config/target/core */
	ret = snprintf(path, sizeof (path), "%s/core", SYSFS_LIO);
	if (ret < 0 || ret >= sizeof (path))
		return (SA_SYSTEM_ERR);

	entries1 = iscsi_look_for_stuff(path, NULL, B_TRUE, 0);
	for (entry1 = list_head(entries1);
	    entry1 != NULL;
	    entry1 = list_next(entries1, entry1)) {
		/* DIR: .../{iblock,fileio}_[0-9]. */
// fprintf(stderr, "ENTRY1=%s (%s)\n", entry1->entry, entry1->path); // DEBUG
		// check entry1->entry for '{iblock,fileio}_*'
		if (strncmp(entry1->entry, "iblock_", 7) == 0) {
			strcpy(type, "iblock");
			tid = strstr(entry1->entry, "iblock_") + 7;
		} else if (strncmp(entry1->entry, "fileio_", 7) == 0) {
			strcpy(type, "fileio");
			tid = strstr(entry1->entry, "fileio_") + 7;
		}

		entries2 = iscsi_look_for_stuff(entry1->path, "iqn.",
						B_TRUE, 4);
		for (entry2 = list_head(entries2);
		    entry2 != NULL;
		    entry2 = list_next(entries2, entry2)) {
// fprintf(stderr, "ENTRY2a=%s (%s)\n", entry2->entry, entry2->path); // DEBUG
			/* DIR: .../iqn.[a-z0-9]* */
			tmp = strdup(entry2->entry);
			iqn = strtok(tmp, ":");

			/* From the IQN, get the name (part after :) */
			tmp = strdup(entry2->entry);
			name = strstr(tmp, ":") + 1; /* Exclude the : char */

			/* Get the dev path */
			ret = snprintf(path2, sizeof (path2), "%s/udev_path",
					entry2->path);
			if (ret < 0 || ret >= sizeof (path2))
				return (SA_SYSTEM_ERR);

			if (iscsi_read_sysfs_value(path2, &buffer) != SA_OK)
				return (SA_NO_MEMORY);
			dev_path = strdup(buffer);

			/* Get the block size */
			ret = snprintf(path2, sizeof (path2),
					"%s/attrib/block_size",
					entry2->path);
			if (ret < 0 || ret >= sizeof (path2))
				return (SA_SYSTEM_ERR);

			if (iscsi_read_sysfs_value(path2, &buffer) != SA_OK)
				return (SA_NO_MEMORY);
			blocksize = strdup(buffer);
		}

		ret = snprintf(path2, sizeof (path2),
				"%s/iscsi/%s:%s/tpgt_%s/lun",
				SYSFS_LIO, iqn, name, tid);
		if (ret < 0 || ret >= sizeof (path2))
			return (SA_SYSTEM_ERR);
		entries2 = iscsi_look_for_stuff(path2, "lun_", B_TRUE, 4);
		for (entry2 = list_head(entries2);
		    entry2 != NULL;
		    entry2 = list_next(entries2, entry2)) {
// fprintf(stderr, "ENTRY2b=%s (%s)\n", entry2->entry, entry2->path); // DEBUG
			lun = strstr(entry2->entry, "lun_") + 4;
		}

		/* TODO: Retrieve iomode */
		/* TODO: Retrieve iotype */

		/*
		 * ======
		 * Setup the list of targets
		 */
		target = (iscsi_target_t *)
			malloc(sizeof (iscsi_target_t));
		if (target == NULL)
			return (SA_NO_MEMORY);

#if DEBUG >= 2
		fprintf(stderr, "iqn=%s\n", iqn);
		fprintf(stderr, "  name=%s\n", name);
		fprintf(stderr, "  dev=%s\n", dev_path);
		fprintf(stderr, "  type=%s\n", type);
		fprintf(stderr, "  tid=%s\n", tid);
		fprintf(stderr, "  lun=%s\n", lun);
		fprintf(stderr, "  blocksize=%s\n", blocksize);
		fprintf(stderr, "\n");
#endif

		/*
		 * NOTE: Sometimes 'lun' is null, because there is/was
		 *       a problem with retrieving the value.
		 */
		if (tid != NULL && lun != NULL && blocksize != NULL &&
			iqn != NULL && name != NULL && dev_path != NULL) {
			target->tid = atoi(tid);
			target->lun = atoi(lun);
			target->blocksize = atoi(blocksize);

			strncpy(target->iqn,	iqn,
				sizeof (target->iqn));
			strncpy(target->name,	name,
				sizeof (target->name));
			strncpy(target->path,	dev_path,
				sizeof (target->path));
			strncpy(target->iotype,	type,
				sizeof (target->iotype));
			/*
			 * TODO
			 * strncpy(target->iotype,	iotype,
			 *	sizeof (target->iotype));
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
			fprintf(stderr, "iscsi_retrieve_targets_lio: "
				"target=%s, tid=%d, lun=%d, path=%s\n",
				target->name, target->tid, target->lun,
				target->path);
#endif

			/* Append the target to the list of new trgs */
			list_insert_tail(&all_iscsi_targets_list, target);
		}
	}

	return (SA_OK);
}

int
iscsi_enable_share_one_lio(sa_share_impl_t impl_share, int tid)
{
	int rc, ret;
	char *argv[3], tid_s[11], iqn[255], path1[PATH_MAX], buffer[255];
	const char *serno;
	iscsi_shareopts_t *opts;
	char *shareopts;

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
	fprintf(stderr, "iscsi_enable_share_one_lio: name=%s, tid=%d, "
		"sharepath=%s, iomode=%s, type=%s, lun=%d, blocksize=%d, "
		"initiator_acl='%s'\n",
		opts->name, tid, impl_share->sharepath, opts->iomode,
		opts->type, opts->lun, opts->blocksize, opts->initiator);
#endif

	/* Generate serial number */
	serno = iscsi_generate_lio_serialno();

	/* Create IQN */
	if (!opts->name) {
		if (iscsi_generate_target(impl_share->dataset, iqn,
					    sizeof (iqn)) != SA_OK)
			return (SA_SYSTEM_ERR);
	} else
		strcpy(iqn, opts->name);

	/*
	 * ======
	 * PART 1 - Setup device
	 */
	if (iscsi_setup_device_lio(impl_share, opts, tid, serno) != SA_OK)
		return (SA_SYSTEM_ERR);

	/*
	 * ======
	 * PART 2 - Setup IQN
	 */
	if (iscsi_setup_iqn_lio(impl_share, opts, iqn, tid, serno) != SA_OK)
		return (SA_SYSTEM_ERR);

	/*
	 * ======
	 * PART 3 - Setup ACL, Initiators
	 */
	if (iscsi_setup_acl_lio(impl_share, opts, iqn, tid, serno) != SA_OK)
		return (SA_SYSTEM_ERR);

	/*
	 * ======
	 * PART 4 - Enable IQN
	 */
	ret = snprintf(path1, sizeof (path1), "%s/iscsi/%s/tpgt_%d/enable",
			SYSFS_LIO, iqn, tid);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	strcpy(buffer, "1");

	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 5 - If no authname/authpass, disable authentication
	 */
	if (!strlen(opts->authname)) {
		ret = snprintf(path1, sizeof (path1),
			"%s/iscsi/%s/tpgt_%d/attrib/authentication",
			SYSFS_LIO, iqn, tid);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);
		strcpy(buffer, "0");
		if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
			return (SA_NO_MEMORY);

		ret = snprintf(path1, sizeof (path1),
			"%s/iscsi/%s/tpgt_%d/attrib/generate_node_acls",
			SYSFS_LIO, iqn, tid);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);
		strcpy(buffer, "1");
		if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
			return (SA_NO_MEMORY);
	}

	/*
	 * ======
	 * PART 6 - Run local update script.
	 */
	ret = snprintf(tid_s, sizeof (tid_s), "%d", tid);
	if (ret < 0 || ret >= sizeof (tid_s)) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

	if (access(EXTRA_ISCSI_SHARE_SCRIPT, X_OK) == 0) {
		/* CMD: /sbin/zfs_share_iscsi <TID> */
		argv[0] = (char *)EXTRA_ISCSI_SHARE_SCRIPT;
		argv[1] = tid_s;
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
iscsi_disable_share_one_lio(int tid)
{
	int ret;
	char path1[PATH_MAX], path2[PATH_MAX], buffer[255];
	list_t *entries1, *entries2, *entries3;
	iscsi_dirs_t *entry1, *entry2, *entry3;
	iscsi_target_t *target;

	for (target = list_head(&all_iscsi_targets_list);
	    target != NULL;
	    target = list_next(&all_iscsi_targets_list, target)) {
		if (target->tid == tid) {
#ifdef DEBUG
			fprintf(stderr, "iscsi_disable_share_one_lio: "
				"target=%s, tid=%d, path=%s\n",
				target->name, target->tid, target->path);
#endif

			break;
		}
	}

	/*
	 * ======
	 * PART 1 - Disable target
	 * CMD: echo 0 > $SYSFS/iscsi/IQN/tpgt_TID/enable
	 */
	ret = snprintf(path1, sizeof (path1), "%s/iscsi/%s:%s/tpgt_%d/enable",
			SYSFS_LIO, target->iqn, target->name, target->tid);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);
	strcpy(buffer, "0");
	if (iscsi_write_sysfs_value(path1, buffer) != SA_OK)
		return (SA_NO_MEMORY);

	/*
	 * ======
	 * PART 2 - Recursivly delete IQN directory.
	 */

	/* CMD: rmdir $SYSFS/iscsi/IQN/tpgt_TID/np/IP:PORT */
	ret = snprintf(path1, sizeof (path1), "%s/iscsi/%s:%s/tpgt_%d/np",
			SYSFS_LIO, target->iqn, target->name, target->tid);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	entries1 = iscsi_look_for_stuff(path1, NULL, B_TRUE, 0);
	for (entry1 = list_head(entries1);
		entry1 != NULL;
		entry1 = list_next(entries1, entry1)) {
#ifdef DEBUG
		fprintf(stderr, "CMD: rmdir(%s)\n", entry1->path);
#endif
		ret = rmdir(entry1->path);
		if (ret < 0) {
			fprintf(stderr, "ERR: Failed to remove %s\n",
				entry1->path);
			return (SA_SYSTEM_ERR);
		}
	}

	/* CMD: rm    $SYSFS/iscsi/IQN/tpgt_TID/acls/INITIATOR/lun_LUN/LINK */
	/* CMD: rmdir $SYSFS/iscsi/IQN/tpgt_TID/acls/INITIATOR/lun_LUN */
	/* CMD: rmdir $SYSFS/iscsi/IQN/tpgt_TID/acls/INITIATOR */
	ret = snprintf(path1, sizeof (path1), "%s/iscsi/%s:%s/tpgt_%d/acls",
			SYSFS_LIO, target->iqn, target->name, target->tid);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	entries1 = iscsi_look_for_stuff(path1, "iqn.", B_TRUE, 4);
	for (entry1 = list_head(entries1);
		entry1 != NULL;
		entry1 = list_next(entries1, entry1)) {
		entries2 = iscsi_look_for_stuff(entry1->path, "lun_",
						B_TRUE, 4);
		for (entry2 = list_head(entries2);
			entry2 != NULL;
			entry2 = list_next(entries2, entry2)) {
			entries3 = iscsi_look_for_stuff(entry2->path, NULL,
							B_FALSE, 0);
			for (entry3 = list_head(entries3);
				entry3 != NULL;
				entry3 = list_next(entries3, entry3)) {
#ifdef DEBUG
				fprintf(stderr, "CMD: unlink(%s)\n",
					entry3->path);
#endif
				ret = unlink(entry3->path);
				if (ret < 0) {
					fprintf(stderr, "ERR: Failed to remove "
						"%s\n", entry3->path);
					return (SA_SYSTEM_ERR);
				}
			}

#ifdef DEBUG
			fprintf(stderr, "CMD: rmdir(%s)\n", entry2->path);
#endif
			ret = rmdir(entry2->path);
			if (ret < 0) {
				fprintf(stderr, "ERR: Failed to remove %s\n",
					entry2->path);
				return (SA_SYSTEM_ERR);
			}
		}

#ifdef DEBUG
		fprintf(stderr, "CMD: rmdir(%s)\n", entry1->path);
#endif
		ret = rmdir(entry1->path);
		if (ret < 0) {
			fprintf(stderr, "ERR: Failed to remove %s\n",
				entry1->path);
			return (SA_SYSTEM_ERR);
		}
	}

	/* CMD: rm    $SYSFS/iscsi/IQN/tpgt_TID/lun/lun_LUN/LINK */
	/* CMD: rmdir $SYSFS/iscsi/IQN/tpgt_TID/lun/lun_LUN */
	ret = snprintf(path1, sizeof (path1), "%s/iscsi/%s:%s/tpgt_%d/lun",
			SYSFS_LIO, target->iqn, target->name, target->tid);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);

	entries1 = iscsi_look_for_stuff(path1, "lun_", B_TRUE, 4);
	for (entry1 = list_head(entries1);
		entry1 != NULL;
		entry1 = list_next(entries1, entry1)) {
		ret = snprintf(path2, sizeof (path2),
				"%s/iscsi/%s:%s/tpgt_%d/lun/%s",
				SYSFS_LIO, target->iqn, target->name,
				target->tid, entry1->entry);
		if (ret < 0 || ret >= sizeof (path1))
			return (SA_SYSTEM_ERR);

		entries2 = iscsi_look_for_stuff(path2, NULL, B_FALSE, 0);
		for (entry2 = list_head(entries2);
			entry2 != NULL;
			entry2 = list_next(entries2, entry2)) {
#ifdef DEBUG
			fprintf(stderr, "CMD: unlink(%s)\n", entry2->path);
#endif
			ret = unlink(entry2->path);
			if (ret < 0) {
				fprintf(stderr, "ERR: Failed to remove %s\n",
					entry2->path);
				return (SA_SYSTEM_ERR);
			}
		}

#ifdef DEBUG
		fprintf(stderr, "CMD: rmdir(%s)\n", entry1->path);
#endif
		ret = rmdir(entry1->path);
		if (ret < 0) {
			fprintf(stderr, "ERR: Failed to remove %s\n",
				entry1->path);
			return (SA_SYSTEM_ERR);
		}
	}

	/* CMD: rmdir $SYSFS/iscsi/IQN/tpgt_TID */
	ret = snprintf(path1, sizeof (path1), "%s/iscsi/%s:%s/tpgt_%d",
			SYSFS_LIO, target->iqn, target->name, target->tid);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);
#ifdef DEBUG
		fprintf(stderr, "CMD: rmdir(%s)\n", path1);
#endif
	ret = rmdir(path1);
	if (ret < 0) {
		fprintf(stderr, "ERR: Failed to remove %s\n", path1);
		return (SA_SYSTEM_ERR);
	}

	/* CMD: rmdir $SYSFS/iscsi/IQN:NAME */
	ret = snprintf(path1, sizeof (path1), "%s/iscsi/%s:%s",
			SYSFS_LIO, target->iqn, target->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);
#ifdef DEBUG
		fprintf(stderr, "CMD: rmdir(%s)\n", path1);
#endif
	ret = rmdir(path1);
	if (ret < 0) {
		fprintf(stderr, "ERR: Failed to remove %s\n", path1);
		return (SA_SYSTEM_ERR);
	}

	/*
	 * ======
	 * PART 3 - Delete device backstore
	 */

	/* CMD: rmdir $SYSFS/core/iblock_TID/IQN:NAME */
	ret = snprintf(path1, sizeof (path1), "%s/core/%s_%d/%s:%s",
			SYSFS_LIO, target->iotype, target->tid,
			target->iqn, target->name);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);
#ifdef DEBUG
		fprintf(stderr, "CMD: rmdir(%s)\n", path1);
#endif
	ret = rmdir(path1);
	if (ret < 0) {
		fprintf(stderr, "ERR: Failed to remove %s\n", path1);
		return (SA_SYSTEM_ERR);
	}

	/* CMD: rmdir $SYSFS/core/iblock_TID */
	ret = snprintf(path1, sizeof (path1), "%s/core/%s_%d",
			SYSFS_LIO, target->iotype, target->tid);
	if (ret < 0 || ret >= sizeof (path1))
		return (SA_SYSTEM_ERR);
#ifdef DEBUG
		fprintf(stderr, "CMD: rmdir(%s)\n", path1);
#endif
	ret = rmdir(path1);
	if (ret < 0) {
		fprintf(stderr, "ERR: Failed to remove %s\n", path1);
		return (SA_SYSTEM_ERR);
	}

	return (SA_OK);
}
