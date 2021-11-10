/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) Error Implementation.
 */

#include <sys/sysmacros.h>
#include <sys/cmn_err.h>

/*
 * It is often useful to actually have the panic crash the node so you
 * can then get notified of the event, get the crashdump for later
 * analysis and other such goodies.
 * But we would still default to the current default of not to do that.
 */
/* BEGIN CSTYLED */
unsigned int spl_panic_halt;
module_param(spl_panic_halt, uint, 0644);
MODULE_PARM_DESC(spl_panic_halt, "Cause kernel panic on assertion failures");
/* END CSTYLED */

void
spl_dumpstack(void)
{
	printk("Showing stack for process %d\n", current->pid);
	dump_stack();
}
EXPORT_SYMBOL(spl_dumpstack);

int
spl_panic(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	char msg[MAXMSGLEN];
	va_list ap;

	newfile = strrchr(file, '/');
	if (newfile != NULL)
		newfile = newfile + 1;
	else
		newfile = file;

	va_start(ap, fmt);
	(void) vsnprintf(msg, sizeof (msg), fmt, ap);
	va_end(ap);

	printk(KERN_EMERG "%s", msg);
	printk(KERN_EMERG "PANIC at %s:%d:%s()\n", newfile, line, func);
	if (spl_panic_halt)
		panic("%s", msg);

	spl_dumpstack();

	/* Halt the thread to facilitate further debugging */
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (1)
		schedule();

	/* Unreachable */
	return (1);
}
EXPORT_SYMBOL(spl_panic);

void
vcmn_err(int ce, const char *fmt, va_list ap)
{
	char msg[MAXMSGLEN];

	vsnprintf(msg, MAXMSGLEN, fmt, ap);

	switch (ce) {
	case CE_IGNORE:
		break;
	case CE_CONT:
		printk("%s", msg);
		break;
	case CE_NOTE:
		printk(KERN_NOTICE "NOTICE: %s\n", msg);
		break;
	case CE_WARN:
		printk(KERN_WARNING "WARNING: %s\n", msg);
		break;
	case CE_PANIC:
		printk(KERN_EMERG "PANIC: %s\n", msg);
		spl_dumpstack();

		/* Halt the thread to facilitate further debugging */
		set_current_state(TASK_UNINTERRUPTIBLE);
		while (1)
			schedule();
	}
} /* vcmn_err() */
EXPORT_SYMBOL(vcmn_err);

void
cmn_err(int ce, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(ce, fmt, ap);
	va_end(ap);
} /* cmn_err() */
EXPORT_SYMBOL(cmn_err);
