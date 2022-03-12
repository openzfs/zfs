// SPDX-License-Identifier: 0BSD

#include <inttypes.h>


#define	U_APPEND_SHORT		"uappnd"
#define	U_APPEND_FULL		"uappend"
#define	SU_APPEND_SHORT		"sappnd"
#define	SU_APPEND_FULL		"sappend"

#define	U_ARCH_SHORT		"uarch"
#define	U_ARCH_FULL		"uarchive"
#define	SU_ARCH_SHORT		"arch"
#define	SU_ARCH_FULL		"archived"

#define	U_HIDDEN_SHORT		"hidden"
#define	U_HIDDEN_FULL		"uhidden"

#define	SU_IMMUTABLE_FULL	"simmutable"
#define	SU_IMMUTABLE_SHORT	"schange"
#define	SU_IMMUTABLE		"schg"
#define	U_IMMUTABLE_FULL	"uimmutable"
#define	U_IMMUTABLE_SHORT	"uchange"
#define	U_IMMUTABLE		"uchg"

#define	SU_NODUMP		"nodump"
#define	UNSET_NODUMP		"dump"

#define	U_UNLINK_SHORT		"uunlnk"
#define	U_UNLINK_FULL		"uunlink"
#define	SU_UNLINK_SHORT		"sunlnk"
#define	SU_UNLINK_FULL		"sunlink"

#define	U_OFFLINE_SHORT		"offline"
#define	U_OFFLINE_FULL		"uoffline"

#define	U_RDONLY		"rdonly"
#define	U_RDONLY_SHORT		"urdonly"
#define	U_RDONLY_FULL		"readonly"

#define	U_REPARSE_SHORT		"reparse"
#define	U_REPARSE_FULL		"ureparse"

#define	U_SPARSE_SHORT		"sparse"
#define	U_SPARSE_FULL		"usparse"

#define	U_SYSTEM_SHORT		"system"
#define	U_SYSTEM_FULL		"usystem"


static const uint64_t all_dos_attributes[] = {
	ZFS_ARCHIVE,
	ZFS_APPENDONLY,
	ZFS_IMMUTABLE,
	ZFS_NOUNLINK,
	ZFS_NODUMP,
	ZFS_HIDDEN,
	ZFS_OFFLINE,
	ZFS_READONLY,
	ZFS_SPARSE,
	ZFS_SYSTEM,
	ZFS_REPARSE,
};

static const char *const all_dos_attribute_names[][7] = {
	{U_ARCH_SHORT, U_ARCH_FULL, SU_ARCH_SHORT, SU_ARCH_FULL},
	{U_APPEND_SHORT, U_APPEND_FULL, SU_APPEND_SHORT, SU_APPEND_FULL},
	{SU_IMMUTABLE_FULL, SU_IMMUTABLE_SHORT, SU_IMMUTABLE,
	    U_IMMUTABLE_FULL, U_IMMUTABLE_SHORT, U_IMMUTABLE},
	{U_UNLINK_SHORT, U_UNLINK_FULL, SU_UNLINK_FULL, SU_UNLINK_SHORT},
	{SU_NODUMP, /* UNSET_NODUMP */},
	{U_HIDDEN_SHORT, U_HIDDEN_FULL},
	{U_OFFLINE_SHORT, U_OFFLINE_FULL},
	{U_RDONLY, U_RDONLY_SHORT, U_RDONLY_FULL},
	{U_SPARSE_SHORT, U_SPARSE_FULL},
	{U_SYSTEM_SHORT, U_SYSTEM_FULL},
	{U_REPARSE_SHORT, U_REPARSE_FULL},
};

_Static_assert(
    ARRAY_SIZE(all_dos_attributes) == ARRAY_SIZE(all_dos_attribute_names),
	"attribute list length mismatch");
