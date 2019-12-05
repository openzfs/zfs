/* BEGIN CSTYLED */
/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */



/*-*************************************
*  Dependencies
***************************************/
#include <sys/zstd/mem.h>
#include <sys/zstd/error_private.h>
#include <sys/zstd/zstd_internal.h>
#include <sys/zfs_context.h>

/*! g_debuglog_enable :
 *  turn on/off debug traces (global switch) */
#if defined(ZSTD_DEBUG) && (ZSTD_DEBUG >= 2)
int g_debuglog_enable = 1;
#endif


/*! ZSTD_getError() :
 *  convert a `size_t` function result into a proper ZSTD_errorCode enum */
ZSTD_ErrorCode ZSTD_getErrorCode(size_t code) { return ERR_getErrorCode(code); }

/*=**************************************************************
*  Custom allocator
****************************************************************/
void* ZSTD_malloc(size_t size, ZSTD_customMem customMem)
{
    if (customMem.customAlloc)
        return customMem.customAlloc(customMem.opaque, size);
    return kmem_alloc((size), KM_SLEEP); //should be never reached
}

void ZSTD_free(void* ptr, ZSTD_customMem customMem)
{
    if (ptr!=NULL) {
        if (customMem.customFree)
            customMem.customFree(customMem.opaque, ptr);
        else
            kmem_free((ptr), 0); //should be never reached
    }
}
/* END CSTYLED */
