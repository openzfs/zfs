/*	$NetBSD: types.h,v 1.4 2021/08/02 12:56:22 andvar Exp $	*/

/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 *
 *	from: @(#)types.h 1.18 87/07/24 SMI
 *	@(#)types.h	2.3 88/08/15 4.0 RPCSRC
 */

/*
 * Rpc additions to <sys/types.h>
 *
 * Copied from NetBSD src/include/rpc/types.h for the openzfs kmod.
 * License unchanged (Sun RPC).  warn() is a no-op statement so a
 * kernel build with -Werror does not see a leftover comma expression.
 */
#ifndef _RPC_TYPES_H_
#define	_RPC_TYPES_H_

#include <sys/types.h>

#ifndef bool_t
typedef int32_t bool_t;
#endif
#ifndef enum_t
typedef int32_t enum_t;
#endif

typedef uint32_t rpcprog_t;
typedef uint32_t rpcvers_t;
typedef uint32_t rpcproc_t;
typedef uint32_t rpcprot_t;
typedef uint32_t rpcport_t;
typedef int32_t rpc_inline_t;

#define	__dontcare__	-1

#ifndef FALSE
#define	FALSE	(0)
#endif
#ifndef TRUE
#define	TRUE	(1)
#endif
#ifndef NULL
/* pointer, not int 0: fm_payload_set terminator */
#define	NULL	((void *)0)
#endif

#if defined(_KERNEL) || defined(_STANDALONE)

#define	warn(...)	((void)0)

/*
 * Do not include OpenZFS sys/kmem.h from this header.  That overlay
 * redefines bool_t after this file has already typedef'd it, so the
 * prototypes in rpc/xdr.h and the definitions in xdr.c disagree.
 */
void	*spl_xdr_mem_alloc(size_t);
void	spl_xdr_mem_free(void *, size_t);
#define	mem_alloc(bsize)	spl_xdr_mem_alloc((size_t)(bsize))
#define	mem_free(ptr, bsize)	spl_xdr_mem_free((ptr), (size_t)(bsize))

#else /* _KERNEL || _STANDALONE */

#define	mem_alloc(bsize)	calloc((size_t)1, (bsize))
#define	mem_free(ptr, bsize)	free(ptr)

#endif /* _KERNEL || _STANDALONE */

#endif /* !_RPC_TYPES_H_ */
