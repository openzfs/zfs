// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <Ntifs.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/sid.h>

/*
 * Map a Windows SID to a POSIX uid.
 *
 * Recognised schemes:
 *   S-1-5-18          SYSTEM                   -> 0 (root)
 *   S-1-22-1-X        Samba unix-user           -> X
 *   S-1-5-21-*-*-*-R  Windows domain / local    -> R (last sub-authority)
 *   S-1-5-22-*-*-*-R  Windows domain (alt)      -> R
 * Anything else       -> UID_NOBODY
 */
uid_t
spl_sid_to_uid(struct _SID *sid)
{
	/*
	 * S-1-5-18 (SYSTEM), S-1-5-19 (LOCAL SERVICE),
	 * S-1-5-20 (NETWORK SERVICE) -> root.
	 * vssvc.exe runs as NETWORK SERVICE and needs uid 0 to create
	 * ZFS snapshots via the VSS software provider.
	 */
	if (sid->Revision == 1 && sid->SubAuthorityCount == 1 &&
	    sid->IdentifierAuthority.Value[5] == 5 &&
	    sid->SubAuthority[0] >= 18 && sid->SubAuthority[0] <= 20)
		return (0);

	/* S-1-22-1-X (Samba unix-user) */
	if (sid->Revision == 1 && sid->SubAuthorityCount == 2 &&
	    sid->IdentifierAuthority.Value[5] == 22 &&
	    sid->SubAuthority[0] == 1)
		return ((uid_t)sid->SubAuthority[1]);

	/* S-1-5-21-*-*-*-RID (domain / local account) */
	if (sid->Revision == 1 &&
	    sid->IdentifierAuthority.Value[5] == 5 &&
	    sid->SubAuthorityCount >= 5 &&
	    sid->SubAuthority[0] == 21)
		return ((uid_t)sid->SubAuthority[sid->SubAuthorityCount - 1]);

	/* S-1-5-22-*-*-*-RID */
	if (sid->Revision == 1 &&
	    sid->IdentifierAuthority.Value[5] == 5 &&
	    sid->SubAuthorityCount >= 5 &&
	    sid->SubAuthority[0] == 22)
		return ((uid_t)sid->SubAuthority[sid->SubAuthorityCount - 1]);

	return (UID_NOBODY);
}

/*
 * Map a Windows SID to a POSIX gid.
 *
 * Recognised schemes:
 *   S-1-22-2-X        Samba unix-group          -> X
 *   S-1-5-21-*-*-*-R  Windows domain group      -> R
 * Anything else       -> GID_NOBODY
 */
gid_t
spl_sid_to_gid(struct _SID *sid)
{
	/* S-1-22-2-0 (Samba "root" group) */
	if (sid->Revision == 1 && sid->SubAuthorityCount == 1 &&
	    sid->IdentifierAuthority.Value[5] == 22 &&
	    sid->SubAuthority[0] == 2)
		return (0);

	/* S-1-22-2-X (Samba unix-group) */
	if (sid->Revision == 1 && sid->SubAuthorityCount == 2 &&
	    sid->IdentifierAuthority.Value[5] == 22 &&
	    sid->SubAuthority[0] == 2)
		return ((gid_t)sid->SubAuthority[1]);

	/* S-1-5-21-*-*-*-RID (domain / local group) */
	if (sid->Revision == 1 &&
	    sid->IdentifierAuthority.Value[5] == 5 &&
	    sid->SubAuthorityCount >= 5 &&
	    sid->SubAuthority[0] == 21)
		return ((gid_t)sid->SubAuthority[sid->SubAuthorityCount - 1]);

	return (GID_NOBODY);
}

/*
 * Return the uid of the calling process.
 * Elevated / admin tokens map to uid 0; otherwise the user SID is mapped
 * via spl_sid_to_uid().
 * Returns 0 on any error (fail-safe: treat as root so existing behaviour
 * for kernel/system paths is preserved).
 */
uid_t
spl_get_caller_uid(void)
{
	PACCESS_TOKEN token;
	PTOKEN_USER tuser = NULL;
	NTSTATUS status;
	uid_t uid;

	token = PsReferencePrimaryToken(PsGetCurrentProcess());
	if (token == NULL) {
		return (0);
	}

	if (SeTokenIsAdmin(token)) {
		PsDereferencePrimaryToken(token);
		return (0);
	}

	status = SeQueryInformationToken(token, TokenUser, (PVOID *)&tuser);
	PsDereferencePrimaryToken(token);

	if (!NT_SUCCESS(status) || tuser == NULL)
		return (UID_NOBODY);

	uid = spl_sid_to_uid((struct _SID *)tuser->User.Sid);
	ExFreePool(tuser);
	dprintf("out uid %u\r\n", uid);
	return (uid);
}

/*
 * Return the primary group gid of the calling process.
 * Elevated / admin tokens map to gid 0.
 */
gid_t
spl_get_caller_gid(void)
{
	PACCESS_TOKEN token;
	PTOKEN_PRIMARY_GROUP tgrp = NULL;
	NTSTATUS status;
	gid_t gid;

	token = PsReferencePrimaryToken(PsGetCurrentProcess());
	if (token == NULL)
		return (0);

	if (SeTokenIsAdmin(token)) {
		PsDereferencePrimaryToken(token);
		return (0);
	}

	status = SeQueryInformationToken(token, TokenPrimaryGroup,
	    (PVOID *)&tgrp);
	PsDereferencePrimaryToken(token);

	if (!NT_SUCCESS(status) || tgrp == NULL)
		return (GID_NOBODY);

	gid = spl_sid_to_gid((struct _SID *)tgrp->PrimaryGroup);
	ExFreePool(tgrp);
	return (gid);
}

/* Return the effective user id */
uid_t
crgetuid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_uid);
	return (spl_get_caller_uid());
}


/* Return the real user id */
uid_t
crgetruid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_uid);
	return (spl_get_caller_uid());
}

/* Return the saved user id */
uid_t
crgetsuid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_uid);
	return (spl_get_caller_uid());
}

/* Return the filesystem user id */
uid_t
crgetfsuid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_uid);
	return (spl_get_caller_uid());
}

/* Return the effective group id */
gid_t
crgetgid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_gid);
	return (spl_get_caller_gid());
}

/* Return the real group id */
gid_t
crgetrgid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_gid);
	return (spl_get_caller_gid());
}

/* Return the saved group id */
gid_t
crgetsgid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_gid);
	return (spl_get_caller_gid());
}

/* Return the filesystem group id */
gid_t
crgetfsgid(const cred_t *cr)
{
	if (cr != NULL)
		return (cr->cr_gid);
	return (spl_get_caller_gid());
}

/*
 * Fill a cred_t from the token of the process that originated an IRP.
 * Uses IoGetRequestorProcess() so it works correctly even when called
 * from a system worker thread (unlike PsGetCurrentProcess()).
 * Falls back to uid/gid 0 on any error so existing kernel paths are safe.
 */
void
spl_fill_cred_from_irp(cred_t *cr, PIRP Irp)
{
	PEPROCESS proc;
	PACCESS_TOKEN token;
	PTOKEN_USER tuser = NULL;
	PTOKEN_PRIMARY_GROUP tgrp = NULL;
	NTSTATUS status;

	ASSERT(cr != NULL);
	cr->cr_uid = 0;
	cr->cr_gid = 0;

	if (Irp == NULL)
		return;

	proc = IoGetRequestorProcess(Irp);
	if (proc == NULL)
		return;

	token = PsReferencePrimaryToken(proc);
	if (token == NULL)
		return;

	/* Admin / elevated token -> uid 0, gid 0 */
	if (SeTokenIsAdmin(token)) {
		PsDereferencePrimaryToken(token);
		return;
	}

	status = SeQueryInformationToken(token, TokenUser, (PVOID *)&tuser);
	if (NT_SUCCESS(status) && tuser != NULL) {
		cr->cr_uid = spl_sid_to_uid((struct _SID *)tuser->User.Sid);
		ExFreePool(tuser);
	}

	status = SeQueryInformationToken(token, TokenPrimaryGroup,
	    (PVOID *)&tgrp);
	if (NT_SUCCESS(status) && tgrp != NULL) {
		cr->cr_gid = spl_sid_to_gid((struct _SID *)tgrp->PrimaryGroup);
		ExFreePool(tgrp);
	}

	PsDereferencePrimaryToken(token);
	dprintf("spl_fill_cred_from_irp: uid=%u gid=%u\n",
	    cr->cr_uid, cr->cr_gid);
}


/*
 * Unfortunately, to get the count of groups, we have to call XNU which
 * memcpy's them over. No real clean way to get around that, but at least
 * these calls are done sparingly.
 */
int
crgetngroups(const cred_t *cr)
{
	(void) cr;
	return (0);
}


/*
 * We always allocate NGROUPs here, since we don't know how many there will
 * be until after the call. Unlike IllumOS, the ptr returned is allocated
 * and must be returned by a call to crgetgroupsfree().
 */
gid_t *
crgetgroups(const cred_t *cr)
{
	gid_t *gids;
	int count = NGROUPS;
	(void) cr;

	gids = kmem_zalloc(sizeof (gid_t) * count, KM_SLEEP);
	if (!gids)
		return (NULL);

	return (gids);
}

void
crgetgroupsfree(gid_t *gids)
{
	if (!gids)
		return;
	kmem_free(gids, sizeof (gid_t) * NGROUPS);
}

/*
 * Return true if "cr" belongs in group "gid".
 */
int
spl_cred_ismember_gid(cred_t *cr, gid_t gid)
{
	int ret = 0; // Is not member.
	(void) cr; (void) gid;
	if (ret == 1)
		return (TRUE);
	return (FALSE);
}

int
groupmember(gid_t gid, kauth_cred_t *cred)
{
	return (0);
}

void
crhold(cred_t *cr)
{
	(void) cr;
	/* current Windows port: cred_t is always NULL (see CRED()/kcred) */
}

void
crfree(cred_t *cr)
{
	(void) cr;
	/* current Windows port: cred_t is always NULL */
}
