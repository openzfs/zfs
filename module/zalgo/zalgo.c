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
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zalgo.h>
#include <sys/avl.h>

typedef enum {
	ZG_DUMMY = 0,	/* dummy type, for test */
	ZG_MAC,
	ZG_DIGEST,
	ZG_CHECKSUM,
	ZG_CIPHER,
	ZG_TYPE_MAX,
} zalgo_type_t;

static const uint_t zalgo_subtype_max[ZG_TYPE_MAX] = {
	ZG_DUMMY_SUBTYPE_MAX,
	ZG_MAC_SUBTYPE_MAX,
	ZG_DIGEST_SUBTYPE_MAX,
	ZG_CHECKSUM_SUBTYPE_MAX,
	ZG_CIPHER_SUBTYPE_MAX,
};

static const char *zalgo_type_str[ZG_TYPE_MAX] = {
	"dummy",
	"mac",
	"digest",
	"checksum",
	"cipher",
};

static const char *zalgo_dummy_subtype_str[ZG_DUMMY_SUBTYPE_MAX] = {
	"dummy-0",
	"dummy-1",
	"dummy-2",
};

static const char *zalgo_mac_subtype_str[ZG_MAC_SUBTYPE_MAX] = {
	"HMAC-SHA512"
};

static const char *zalgo_digest_subtype_str[ZG_DIGEST_SUBTYPE_MAX] = {
	"SHA256",
	"SHA512",
	"SHA512-256",
};

static const char *zalgo_checksum_subtype_str[ZG_CHECKSUM_SUBTYPE_MAX] = {
	"fletcher2",
	"fletcher2-byteswap",
	"fletcher4",
	"fletcher4-byteswap",
	"sha256",
	"sha256-byteswap",
	"sha512",
	"sha512-byteswap",
	"skein",
	"skein-byteswap",
	"edonr",
	"edonr-byteswap",
	"blake3",
	"blake3-byteswap",
};

static const char *zalgo_cipher_subtype_str[ZG_CIPHER_SUBTYPE_MAX] = {
	"AES-CCM",
	"AES-GCM",
};

static const char *unknown = "[unknown]";

static const char *
zalgo_type_to_str(zalgo_type_t type)
{
	return (type < ZG_TYPE_MAX ? zalgo_type_str[type] : unknown);
}

static const char *
zalgo_subtype_to_str(zalgo_type_t type, uint_t subtype)
{
	switch (type) {
	case ZG_DUMMY:
		return (subtype < ZG_DUMMY_SUBTYPE_MAX ?
		    zalgo_dummy_subtype_str[subtype] : unknown);
	case ZG_MAC:
		return (subtype < ZG_MAC_SUBTYPE_MAX ?
		    zalgo_mac_subtype_str[subtype] : unknown);
	case ZG_DIGEST:
		return (subtype < ZG_DIGEST_SUBTYPE_MAX ?
		    zalgo_digest_subtype_str[subtype] : unknown);
	case ZG_CHECKSUM:
		return (subtype < ZG_CHECKSUM_SUBTYPE_MAX ?
		    zalgo_checksum_subtype_str[subtype] : unknown);
	case ZG_CIPHER:
		return (subtype < ZG_CIPHER_SUBTYPE_MAX ?
		    zalgo_cipher_subtype_str[subtype] : unknown);
	default:
		return (unknown);
	}

	__builtin_unreachable();
}

typedef struct {
	const void	*zgn_ops;
	const char	*zgn_id;
	const char	*zgn_desc;
	uint_t		zgn_subtype;
	uint64_t	zgn_refcount;
	list_node_t	zgn_link;
} zalgo_node_t;

typedef struct {
	zalgo_node_t	*zr_current;
	zalgo_node_t	*zr_best;
	list_t		zr_nodes;
} zalgo_registry_t;

/*
 * The registry is an array [ZG_TYPE_MAX] of arrays [ZG_XXX_SUBTYPE_MAX] of
 * zalgo_registry_t, which holds the atomic pointer for the currently selected
 * implementation, and a list of all registered nodes (in arbitrary order).
 */
static zalgo_registry_t *zg_registry[ZG_TYPE_MAX] = {};
static kmutex_t zg_registry_lock;

static int
zalgo_register(zalgo_type_t type, uint_t subtype, const char *id,
    const char *desc, const void *ops)
{
	ASSERT3U(type, <, ZG_TYPE_MAX);
	ASSERT3U(subtype, <, zalgo_subtype_max[type]);
	ASSERT3P(id, !=, NULL);
	ASSERT3P(desc, !=, NULL);
	ASSERT3P(ops, !=, NULL);

	zalgo_registry_t *reg = &zg_registry[type][subtype];

	zalgo_node_t *node = kmem_alloc(sizeof (zalgo_node_t), KM_SLEEP);
	node->zgn_subtype = subtype;
	node->zgn_id = id;
	node->zgn_desc = desc;
	node->zgn_ops = ops;
	node->zgn_refcount = 2;

	mutex_enter(&zg_registry_lock);
	for (zalgo_node_t *exist = list_head(&reg->zr_nodes); exist != NULL;
	    exist = list_next(&reg->zr_nodes, exist)) {
		if ((exist->zgn_ops == node->zgn_ops) ||
		    strcmp(exist->zgn_id, node->zgn_id) == 0) {
			/* Don't allow id or ops to be reused. */
			mutex_exit(&zg_registry_lock);
			kmem_free(node, sizeof (zalgo_node_t));
			return (EEXIST);
		}
	}

	list_insert_tail(&reg->zr_nodes, node);

	boolean_t is_current =
	    (atomic_cas_ptr(&reg->zr_current, NULL, node) == NULL);
	if (!is_current)
		atomic_dec_64(&node->zgn_refcount);
	mutex_exit(&zg_registry_lock);

	cmn_err(CE_NOTE, "zalgo: registered '%s' (%s) for %s:%s%s",
	    id, desc, zalgo_type_to_str(type),
	    zalgo_subtype_to_str(type, subtype),
	    is_current ? " [selected]" : "");

	return (0);
}

static void *
zalgo_hold(zalgo_type_t type, uint_t subtype)
{
	ASSERT3U(type, <, ZG_TYPE_MAX);
	ASSERT3U(subtype, <, zalgo_subtype_max[type]);

	zalgo_registry_t *reg = &zg_registry[type][subtype];
	zalgo_node_t *node = atomic_load_ptr(&reg->zr_current);
	if (node == NULL)
		return (NULL);
	atomic_inc_64(&node->zgn_refcount);

	ASSERT3U(node->zgn_subtype, ==, subtype);

	return (node);
}

static void
zalgo_rele(void *nodep)
{
	zalgo_node_t *node = nodep;
	/*
	 * XXX currently can't fall to zero, but will if/when we have a way
	 *     to remove it from the registry.
	 */
	VERIFY3U(atomic_dec_64_nv(&node->zgn_refcount), >, 0);
}

void
zalgo_init(void)
{
	for (zalgo_type_t type = 0; type < ZG_TYPE_MAX; type++) {
		zg_registry[type] = kmem_alloc(sizeof (zalgo_registry_t) *
		    zalgo_subtype_max[type], KM_SLEEP);
		for (uint_t i = 0; i < zalgo_subtype_max[type]; i++) {
			zalgo_registry_t *reg = &zg_registry[type][i];
			reg->zr_current = NULL;
			list_create(&reg->zr_nodes, sizeof (zalgo_node_t),
			    offsetof(zalgo_node_t, zgn_link));
		}
	}
	mutex_init(&zg_registry_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
zalgo_fini(void)
{
	for (zalgo_type_t type = 0; type < ZG_TYPE_MAX; type++) {
		for (uint_t i = 0; i < zalgo_subtype_max[type]; i++) {
			zalgo_registry_t *reg = &zg_registry[type][i];
			zalgo_node_t *node;
			while ((node =
			    list_remove_head(&reg->zr_nodes)) != NULL) {
				if (atomic_cas_ptr(&reg->zr_current,
				    node, NULL) == node)
					atomic_dec_64(&node->zgn_refcount);
				ASSERT3U(node->zgn_refcount, ==, 1);
				kmem_free(node, sizeof (zalgo_node_t));
			}
			list_destroy(&reg->zr_nodes);
		}
		kmem_free(zg_registry[type],
		    sizeof (zalgo_registry_t) * zalgo_subtype_max[type]);
	}
	mutex_destroy(&zg_registry_lock);
}

/* ========== */

#define	ZALGO_DEFINE_API(ty, en)					\
int									\
zalgo_##ty##_register(zalgo_##ty##_subtype_t subtype, const char *id,	\
    const char *desc, const zalgo_##ty##_ops_t *ops)			\
{									\
	return (zalgo_register(ZG_##en, subtype, id, desc, ops));	\
}									\
EXPORT_SYMBOL(zalgo_##ty##_register);					\
									\
zalgo_##ty##_hold_t *							\
zalgo_##ty##_hold(zalgo_##ty##_subtype_t subtype)			\
{									\
	return (zalgo_hold(ZG_##en, subtype));				\
}									\
									\
void									\
zalgo_##ty##_rele(zalgo_##ty##_hold_t *hold)				\
{									\
	return (zalgo_rele(hold));					\
}									\

ZALGO_DEFINE_API(dummy, DUMMY)
ZALGO_DEFINE_API(mac, MAC)
ZALGO_DEFINE_API(digest, DIGEST)
ZALGO_DEFINE_API(checksum, CHECKSUM)
ZALGO_DEFINE_API(cipher, CIPHER)
