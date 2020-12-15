#ifndef LIBSPL_LIBINTL_H
#define LIBSPL_LIBINTL_H

#ifdef HAVE_GETTEXT

#else
#define	gettext(str)		(str)
#define	dgettext(domain, str)	(str)
#define textdomain(domain) (domain)
#endif


#endif
