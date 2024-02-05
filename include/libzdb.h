#define	ZDB_COMPRESS_NAME(idx) ((idx) < ZIO_COMPRESS_FUNCTIONS ?	\
	zio_compress_table[(idx)].ci_name : "UNKNOWN")
#define	ZDB_CHECKSUM_NAME(idx) ((idx) < ZIO_CHECKSUM_FUNCTIONS ?	\
	zio_checksum_table[(idx)].ci_name : "UNKNOWN")
#define	ZDB_OT_TYPE(idx) ((idx) < DMU_OT_NUMTYPES ? (idx) :		\
	(idx) == DMU_OTN_ZAP_DATA || (idx) == DMU_OTN_ZAP_METADATA ?	\
	DMU_OT_ZAP_OTHER : \
	(idx) == DMU_OTN_UINT64_DATA || (idx) == DMU_OTN_UINT64_METADATA ? \
	DMU_OT_UINT64_OTHER : DMU_OT_NUMTYPES)

/* Some platforms require part of inode IDs to be remapped */
#ifdef __APPLE__
#define	ZDB_MAP_OBJECT_ID(obj) INO_XNUTOZFS(obj, 2)
#else
#define	ZDB_MAP_OBJECT_ID(obj) (obj)
#endif

#define	ZOR_FLAG_PLAIN_FILE	0x0001
#define	ZOR_FLAG_DIRECTORY	0x0002
#define	ZOR_FLAG_SPACE_MAP	0x0004
#define	ZOR_FLAG_ZAP		0x0008
#define	ZOR_FLAG_ALL_TYPES	-1
#define	ZOR_SUPPORTED_FLAGS	(ZOR_FLAG_PLAIN_FILE	| \
				ZOR_FLAG_DIRECTORY	| \
				ZOR_FLAG_SPACE_MAP	| \
				ZOR_FLAG_ZAP)

#define	ZDB_FLAG_CHECKSUM	0x0001
#define	ZDB_FLAG_DECOMPRESS	0x0002
#define	ZDB_FLAG_BSWAP		0x0004
#define	ZDB_FLAG_GBH		0x0008
#define	ZDB_FLAG_INDIRECT	0x0010
#define	ZDB_FLAG_RAW		0x0020
#define	ZDB_FLAG_PRINT_BLKPTR	0x0040
#define	ZDB_FLAG_VERBOSE	0x0080


typedef struct zdb_ctx {
} zdb_ctx_t;

typedef struct zopt_object_range {
	uint64_t zor_obj_start;
	uint64_t zor_obj_end;
	uint64_t zor_flags;
} zopt_object_range_t;


typedef struct sublivelist_verify {
	/* FREE's that haven't yet matched to an ALLOC, in one sub-livelist */
	zfs_btree_t sv_pair;

	/* ALLOC's without a matching FREE, accumulates across sub-livelists */
	zfs_btree_t sv_leftover;
} sublivelist_verify_t;

typedef struct sublivelist_verify_block {
	dva_t svb_dva;

	/*
	 * We need this to check if the block marked as allocated
	 * in the livelist was freed (and potentially reallocated)
	 * in the metaslab spacemaps at a later TXG.
	 */
	uint64_t svb_allocated_txg;
} sublivelist_verify_block_t;

const char *zdb_ot_name(dmu_object_type_t type);
int livelist_compare(const void *larg, const void *rarg);
