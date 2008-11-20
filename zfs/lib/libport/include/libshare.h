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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * basic API declarations for share management
 */

#include "zfs_config.h"

#ifdef HAVE_LIBSHARE
#include_next <libshare.h>
#else

#ifndef _LIBSHARE_H
#define	_LIBSHARE_H



#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * Basic datatypes for most functions
 */
typedef void *sa_group_t;
typedef void *sa_share_t;
typedef void *sa_property_t;
typedef void *sa_optionset_t;
typedef void *sa_security_t;
typedef void *sa_protocol_properties_t;
typedef void *sa_resource_t;

typedef void *sa_handle_t;	/* opaque handle to access core functions */

/*
 * defined error values
 */

#define	SA_OK			0
#define	SA_NO_SUCH_PATH		1	/* provided path doesn't exist */
#define	SA_NO_MEMORY		2	/* no memory for data structures */
#define	SA_DUPLICATE_NAME	3	/* object name is already in use */
#define	SA_BAD_PATH		4	/* not a full path */
#define	SA_NO_SUCH_GROUP	5	/* group is not defined */
#define	SA_CONFIG_ERR		6	/* system configuration error */
#define	SA_SYSTEM_ERR		7	/* system error, use errno */
#define	SA_SYNTAX_ERR		8	/* syntax error on command line */
#define	SA_NO_PERMISSION	9	/* no permission for operation */
#define	SA_BUSY			10	/* resource is busy */
#define	SA_NO_SUCH_PROP		11	/* property doesn't exist */
#define	SA_INVALID_NAME		12	/* name of object is invalid */
#define	SA_INVALID_PROTOCOL	13	/* specified protocol not valid */
#define	SA_NOT_ALLOWED		14	/* operation not allowed */
#define	SA_BAD_VALUE		15	/* bad value for property */
#define	SA_INVALID_SECURITY	16	/* invalid security type */
#define	SA_NO_SUCH_SECURITY	17	/* security set not found */
#define	SA_VALUE_CONFLICT	18	/* property value conflict */
#define	SA_NOT_IMPLEMENTED	19	/* plugin interface not implemented */
#define	SA_INVALID_PATH		20	/* path is sub-dir of existing share */
#define	SA_NOT_SUPPORTED	21	/* operation not supported for proto */
#define	SA_PROP_SHARE_ONLY	22	/* property valid on share only */
#define	SA_NOT_SHARED		23	/* path is not shared */
#define	SA_NO_SUCH_RESOURCE	24	/* resource not found */
#define	SA_RESOURCE_REQUIRED	25	/* resource name is required  */
#define	SA_MULTIPLE_ERROR	26	/* multiple protocols reported error */
#define	SA_PATH_IS_SUBDIR	27	/* check_path found path is subdir */
#define	SA_PATH_IS_PARENTDIR	28	/* check_path found path is parent */
#define	SA_NO_SECTION		29	/* protocol requires section info */
#define	SA_NO_SUCH_SECTION	30	/* no section found */
#define	SA_NO_PROPERTIES	31	/* no properties found */
#define	SA_PASSWORD_ENC		32	/* passwords must be encrypted */

/* API Initialization */
#define	SA_INIT_SHARE_API	0x0001	/* init share specific interface */
#define	SA_INIT_CONTROL_API	0x0002	/* init control specific interface */

/* not part of API returns */
#define	SA_LEGACY_ERR		32	/* share/unshare error return */

/*
 * other defined values
 */

#define	SA_MAX_NAME_LEN		100	/* must fit service instance name */
#define	SA_MAX_RESOURCE_NAME	255	/* Maximum length of resource name */

/* Used in calls to sa_add_share() and sa_add_resource() */
#define	SA_SHARE_TRANSIENT	0	/* shared but not across reboot */
#define	SA_SHARE_LEGACY		1	/* share is in dfstab only */
#define	SA_SHARE_PERMANENT	2	/* share goes to repository */

/* sa_check_path() related */
#define	SA_CHECK_NORMAL		0	/* only check against active shares */
#define	SA_CHECK_STRICT		1	/* check against all shares */

/* RBAC related */
#define	SA_RBAC_MANAGE	"solaris.smf.manage.shares"
#define	SA_RBAC_VALUE	"solaris.smf.value.shares"

/*
 * Feature set bit definitions
 */

#define	SA_FEATURE_NONE		0x0000	/* no feature flags set */
#define	SA_FEATURE_RESOURCE	0x0001	/* resource names are required */
#define	SA_FEATURE_DFSTAB	0x0002	/* need to manage in dfstab */
#define	SA_FEATURE_ALLOWSUBDIRS	0x0004	/* allow subdirs to be shared */
#define	SA_FEATURE_ALLOWPARDIRS	0x0008	/* allow parent dirs to be shared */
#define	SA_FEATURE_HAS_SECTIONS	0x0010	/* protocol supports sections */
#define	SA_FEATURE_ADD_PROPERTIES	0x0020	/* can add properties */
#define	SA_FEATURE_SERVER	0x0040	/* protocol supports server mode */

/*
 * legacy files
 */

#define	SA_LEGACY_DFSTAB	"/etc/dfs/dfstab"
#define	SA_LEGACY_SHARETAB	"/etc/dfs/sharetab"

/*
 * SMF related
 */

#define	SA_SVC_FMRI_BASE	"svc:/network/shares/group"

/* initialization */
extern sa_handle_t sa_init(int);
extern void sa_fini(sa_handle_t);
extern int sa_update_config(sa_handle_t);
extern char *sa_errorstr(int);

/* protocol names */
extern int sa_get_protocols(char ***);
extern int sa_valid_protocol(char *);

/* group control (create, remove, etc) */
extern sa_group_t sa_create_group(sa_handle_t, char *, int *);
extern int sa_remove_group(sa_group_t);
extern sa_group_t sa_get_group(sa_handle_t, char *);
extern sa_group_t sa_get_next_group(sa_group_t);
extern char *sa_get_group_attr(sa_group_t, char *);
extern int sa_set_group_attr(sa_group_t, char *, char *);
extern sa_group_t sa_get_sub_group(sa_group_t);
extern int sa_valid_group_name(char *);

/* share control */
extern sa_share_t sa_add_share(sa_group_t, char *, int, int *);
extern int sa_check_path(sa_group_t, char *, int);
extern int sa_move_share(sa_group_t, sa_share_t);
extern int sa_remove_share(sa_share_t);
extern sa_share_t sa_get_share(sa_group_t, char *);
extern sa_share_t sa_find_share(sa_handle_t, char *);
extern sa_share_t sa_get_next_share(sa_share_t);
extern char *sa_get_share_attr(sa_share_t, char *);
extern char *sa_get_share_description(sa_share_t);
extern sa_group_t sa_get_parent_group(sa_share_t);
extern int sa_set_share_attr(sa_share_t, char *, char *);
extern int sa_set_share_description(sa_share_t, char *);
extern int sa_enable_share(sa_group_t, char *);
extern int sa_disable_share(sa_share_t, char *);
extern int sa_is_share(void *);

/* resource name related */
extern sa_resource_t sa_find_resource(sa_handle_t, char *);
extern sa_resource_t sa_get_resource(sa_group_t, char *);
extern sa_resource_t sa_get_next_resource(sa_resource_t);
extern sa_share_t sa_get_resource_parent(sa_resource_t);
extern sa_resource_t sa_get_share_resource(sa_share_t, char *);
extern sa_resource_t sa_add_resource(sa_share_t, char *, int, int *);
extern int sa_remove_resource(sa_resource_t);
extern char *sa_get_resource_attr(sa_resource_t, char *);
extern int sa_set_resource_attr(sa_resource_t, char *, char *);
extern int sa_set_resource_description(sa_resource_t, char *);
extern char *sa_get_resource_description(sa_resource_t);
extern int sa_enable_resource(sa_resource_t, char *);
extern int sa_disable_resource(sa_resource_t, char *);
extern int sa_rename_resource(sa_resource_t, char *);
extern void sa_fix_resource_name(char *);

/* data structure free calls */
extern void sa_free_attr_string(char *);
extern void sa_free_share_description(char *);

/* optionset control */
extern sa_optionset_t sa_get_optionset(sa_group_t, char *);
extern sa_optionset_t sa_get_next_optionset(sa_group_t);
extern char *sa_get_optionset_attr(sa_optionset_t, char *);
extern void sa_set_optionset_attr(sa_optionset_t, char *, char *);
extern sa_optionset_t sa_create_optionset(sa_group_t, char *);
extern int sa_destroy_optionset(sa_optionset_t);
extern sa_optionset_t sa_get_derived_optionset(void *, char *, int);
extern void sa_free_derived_optionset(sa_optionset_t);

/* property functions */
extern sa_property_t sa_get_property(sa_optionset_t, char *);
extern sa_property_t sa_get_next_property(sa_group_t);
extern char *sa_get_property_attr(sa_property_t, char *);
extern sa_property_t sa_create_section(char *, char *);
extern void sa_set_section_attr(sa_property_t, char *, char *);
extern sa_property_t sa_create_property(char *, char *);
extern int sa_add_property(void *, sa_property_t);
extern int sa_update_property(sa_property_t, char *);
extern int sa_remove_property(sa_property_t);
extern int sa_commit_properties(sa_optionset_t, int);
extern int sa_valid_property(void *, char *, sa_property_t);
extern int sa_is_persistent(void *);

/* security control */
extern sa_security_t sa_get_security(sa_group_t, char *, char *);
extern sa_security_t sa_get_next_security(sa_security_t);
extern char *sa_get_security_attr(sa_optionset_t, char *);
extern sa_security_t sa_create_security(sa_group_t, char *, char *);
extern int sa_destroy_security(sa_security_t);
extern void sa_set_security_attr(sa_security_t, char *, char *);
extern sa_optionset_t sa_get_all_security_types(void *, char *, int);
extern sa_security_t sa_get_derived_security(void *, char *, char *, int);
extern void sa_free_derived_security(sa_security_t);

/* protocol specific interfaces */
extern int sa_parse_legacy_options(sa_group_t, char *, char *);
extern char *sa_proto_legacy_format(char *, sa_group_t, int);
extern int sa_is_security(char *, char *);
extern sa_protocol_properties_t sa_proto_get_properties(char *);
extern uint64_t sa_proto_get_featureset(char *);
extern sa_property_t sa_get_protocol_section(sa_protocol_properties_t, char *);
extern sa_property_t sa_get_next_protocol_section(sa_property_t, char *);
extern sa_property_t sa_get_protocol_property(sa_protocol_properties_t, char *);
extern sa_property_t sa_get_next_protocol_property(sa_property_t, char *);
extern int sa_set_protocol_property(sa_property_t, char *, char *);
extern char *sa_get_protocol_status(char *);
extern void sa_format_free(char *);
extern sa_protocol_properties_t sa_create_protocol_properties(char *);
extern int sa_add_protocol_property(sa_protocol_properties_t, sa_property_t);
extern int sa_proto_valid_prop(char *, sa_property_t, sa_optionset_t);
extern int sa_proto_valid_space(char *, char *);
extern char *sa_proto_space_alias(char *, char *);
extern int sa_proto_get_transients(sa_handle_t, char *);
extern int sa_proto_notify_resource(sa_resource_t, char *);
extern int sa_proto_change_notify(sa_share_t, char *);
extern int sa_proto_delete_section(char *, char *);

/* handle legacy (dfstab/sharetab) files */
extern int sa_delete_legacy(sa_share_t, char *);
extern int sa_update_legacy(sa_share_t, char *);
extern int sa_update_sharetab(sa_share_t, char *);
extern int sa_delete_sharetab(sa_handle_t, char *, char *);

/* ZFS functions */
extern int sa_zfs_is_shared(sa_handle_t, char *);
extern int sa_group_is_zfs(sa_group_t);
extern int sa_path_is_zfs(char *);

/* SA Handle specific functions */
extern sa_handle_t sa_find_group_handle(sa_group_t);

#ifdef	__cplusplus
}
#endif

#endif /* _LIBSHARE_H */
#endif /* HAVE_LIBSHARE */
