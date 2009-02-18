/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef _SPLAT_CTL_H
#define _SPLAT_CTL_H

/* Contains shared definitions which both the userspace
 * and kernelspace portions of splat must agree on.
 */

#define SPLAT_MAJOR			229 /* XXX - Arbitrary */
#define SPLAT_MINORS                    1
#define SPLAT_NAME			"splatctl"
#define SPLAT_DEV			"/dev/splatctl"

#define SPLAT_NAME_SIZE			20
#define SPLAT_DESC_SIZE			60

typedef struct splat_user {
	char name[SPLAT_NAME_SIZE];	/* short name */
	char desc[SPLAT_DESC_SIZE];	/* short description */
	int id;				/* unique numeric id */
} splat_user_t;

#define	SPLAT_CFG_MAGIC			0x15263748U
typedef struct splat_cfg {
	unsigned int cfg_magic;		/* Unique magic */
	int cfg_cmd;			/* Config command */
	int cfg_arg1;			/* Config command arg 1 */
	int cfg_rc1;			/* Config response 1 */
	union {
		struct {
			int size;
			splat_user_t descs[0];
		} splat_subsystems;
		struct {
			int size;
			splat_user_t descs[0];
		} splat_tests;
	} cfg_data;
} splat_cfg_t;

#define	SPLAT_CMD_MAGIC			0x9daebfc0U
typedef struct splat_cmd {
	unsigned int cmd_magic;		/* Unique magic */
	int cmd_subsystem;		/* Target subsystem */
	int cmd_test;			/* Subsystem test */
	int cmd_data_size;		/* Extra opaque data */
	char cmd_data_str[0];		/* Opaque data region */
} splat_cmd_t;

/* Valid ioctls */
#define SPLAT_CFG				_IOWR('f', 101, long)
#define SPLAT_CMD				_IOWR('f', 102, long)

/* Valid configuration commands */
#define SPLAT_CFG_BUFFER_CLEAR		0x001	/* Clear text buffer */
#define SPLAT_CFG_BUFFER_SIZE		0x002	/* Resize text buffer */
#define SPLAT_CFG_SUBSYSTEM_COUNT	0x101	/* Number of subsystem */
#define SPLAT_CFG_SUBSYSTEM_LIST	0x102	/* List of N subsystems */
#define SPLAT_CFG_TEST_COUNT		0x201	/* Number of tests */
#define SPLAT_CFG_TEST_LIST		0x202	/* List of N tests */

/* Valid subsystem and test commands defined in each subsystem, we do
 * need to be careful to avoid colisions.  That alone may argue to define
 * them all here, for now we just define the global error codes.
 */
#define SPLAT_SUBSYSTEM_UNKNOWN		0xF00
#define SPLAT_TEST_UNKNOWN		0xFFF

#endif /* _SPLAT_CTL_H */
