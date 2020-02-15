/*
 * This source code is licensed under both the BSD-style license (found in the
 * file THIRDPARTYLICENSE.zstdlib) and the GPLv2 (found in the file
 * THIRDPARTYLICENSE.gplv2.zstdlib). You may select, at your option, one of the
 * above-listed licenses.
 */

/*
 * Copyright (c) 2016-2020, Yann Collet, Facebook, Inc. All rights reserved.
 * Copyright (c) 2019-2020, Michael Niewöhner. All rights reserved.
 */

#define	MEM_MODULE
#define	XXH_NAMESPACE ZSTD_
#define	XXH_PRIVATE_API
#define	XXH_INLINE_ALL
#define	ZSTD_LEGACY_SUPPORT 0
#define	ZSTD_LIB_DICTBUILDER 0
#define	ZSTD_LIB_DEPRECATED 0
#define	ZSTD_NOBENCH

#include "common/debug.c"
#include "common/entropy_common.c"
#include "common/error_private.c"
#include "common/fse_decompress.c"
#include "common/pool.c"
#include "common/zstd_common.c"

#include "compress/fse_compress.c"
#include "compress/hist.c"
#include "compress/huf_compress.c"
#include "compress/zstd_compress_literals.c"
#include "compress/zstd_compress_sequences.c"
#include "compress/zstd_compress_superblock.c"
#include "compress/zstd_compress.c"
#include "compress/zstd_double_fast.c"
#include "compress/zstd_fast.c"
#include "compress/zstd_lazy.c"
#include "compress/zstd_ldm.c"
#include "compress/zstd_opt.c"

#include "decompress/huf_decompress.c"
#include "decompress/zstd_ddict.c"
#include "decompress/zstd_decompress.c"
#include "decompress/zstd_decompress_block.c"
