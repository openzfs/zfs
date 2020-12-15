/*
* CDDL HEADER START
*
* This file and its contents are supplied under the terms of the
* Common Development and Distribution License ("CDDL"), version 1.0.
* You may only use this file in accordance with the terms of version
* 1.0 of the CDDL.
*
* A full copy of the text of the CDDL should have accompanied this
* source.  A copy of the CDDL is also available via the Internet at
* http://www.illumos.org/license/CDDL.
*
* CDDL HEADER END
*/

/*
* Copyright (c) 2017, Jorgen Lundman. All rights reserved.
*/

#ifndef	_TERMIOS_H
#define	_TERMIOS_H

#define ECHOE           0x00000002      /* visually erase chars */
#define ECHOK           0x00000004      /* echo NL after line kill */
#define ECHO            0x00000008      /* enable echoing */
#define ECHONL          0x00000010      /* echo NL even if ECHO is off */

#define TCSAFLUSH       2               /* drain output, flush input */

#define TIOCGWINSZ (104)
#define TIOCSWINSZ (103)

struct termios
{
	int c_lflag;
};

struct winsize {
	unsigned short ws_row;		/* rows, in characters */
	unsigned short ws_col;		/* columns, in character */
	unsigned short ws_xpixel;	/* horizontal size, pixels */
	unsigned short ws_ypixel;	/* vertical size, pixels */
};

int tcgetattr(int fildes, struct termios *termios_p);

int tcsetattr(int fildes, int optional_actions,
	const struct termios *termios_p);

#endif
