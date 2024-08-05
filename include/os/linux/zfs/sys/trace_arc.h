/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#include <sys/list.h>

#if defined(_KERNEL)

DEFINE_DTRACE_PROBE1(arc__hit);
DEFINE_DTRACE_PROBE1(arc__iohit);
DEFINE_DTRACE_PROBE1(arc__evict);
DEFINE_DTRACE_PROBE1(arc__delete);
DEFINE_DTRACE_PROBE1(new_state__mru);
DEFINE_DTRACE_PROBE1(new_state__mfu);
DEFINE_DTRACE_PROBE1(new_state__uncached);
DEFINE_DTRACE_PROBE1(arc__async__upgrade__sync);
DEFINE_DTRACE_PROBE1(l2arc__hit);
DEFINE_DTRACE_PROBE1(l2arc__miss);
DEFINE_DTRACE_PROBE2(l2arc__read);
DEFINE_DTRACE_PROBE2(l2arc__write);
DEFINE_DTRACE_PROBE2(l2arc__iodone);
DEFINE_DTRACE_PROBE3(arc__wait__for__eviction);
DEFINE_DTRACE_PROBE4(arc__miss);
DEFINE_DTRACE_PROBE4(l2arc__evict);

#endif /* _KERNEL */
