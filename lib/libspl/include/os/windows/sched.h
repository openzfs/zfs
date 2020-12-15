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

#ifndef _SPL_SCHED_H
#define _SPL_SCHED_H


struct sched_param { 
    int32_t  sched_priority; 
    int32_t  sched_curpriority; 
    union { 
        int32_t  reserved[8]; 
        struct {    
            int32_t  __ss_low_priority;  
            int32_t  __ss_max_repl;  
            struct timespec     __ss_repl_period;   
            struct timespec     __ss_init_budget;   
        }           __ss;   
    }           __ss_un;    
};

#endif
