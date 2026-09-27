/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998-2014 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _COMMON_H_INCLUDED
#define	_COMMON_H_INCLUDED

#define	FTP_DEFAULT_PORT	21
#define	HTTP_DEFAULT_PORT	80
#define	HTTPS_DEFAULT_PORT	443
#define	FTP_DEFAULT_PROXY_PORT	21
#define	HTTP_DEFAULT_PROXY_PORT	3128

#ifdef WITH_SSL
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

/* Connection */
typedef struct fetchconn conn_t;
struct fetchconn {
	int		 sd;		/* socket descriptor */
	char		*buf;		/* buffer */
	size_t		 bufsize;	/* buffer size */
	size_t		 buflen;	/* length of buffer contents */
	int		 err;		/* last protocol reply code */
#ifdef WITH_SSL
	SSL		*ssl;		/* SSL handle */
	SSL_CTX		*ssl_ctx;	/* SSL context */
	X509		*ssl_cert;	/* server certificate */
	const SSL_METHOD *ssl_meth;	/* SSL method */
#endif
	int		 ref;		/* reference count */
};

/* Structure used for error message lists */
struct fetcherr {
	const int	 num;
	const int	 cat;
	const char	*string;
};

/* For SOCKS header size */
#define	HEAD_SIZE	4
#define	FQDN_SIZE	256
#define	PACK_SIZE	1
#define	PORT_SIZE	2
#define	BUFF_SIZE	HEAD_SIZE + FQDN_SIZE + PACK_SIZE + PORT_SIZE

/* SOCKS5 Request Header */
#define	SOCKS_VERSION_5		0x05
/* SOCKS5 CMD */
#define	SOCKS_CONNECTION	0x01
#define	SOCKS_BIND		0x02
#define	SOCKS_UDP		0x03
#define	SOCKS_NOMETHODS		0xFF
#define	SOCKS5_NOTIMPLEMENTED	0x00
/* SOCKS5 Reserved */
#define	SOCKS_RSV		0x00
/* SOCKS5 Address Type */
#define	SOCKS_ATYP_IPV4		0x01
#define	SOCKS_ATYP_DOMAINNAME	0x03
#define	SOCKS_ATYP_IPV6		0x04
/* SOCKS5 Reply Field */
#define	SOCKS_SUCCESS			0x00
#define	SOCKS_GENERAL_FAILURE		0x01
#define	SOCKS_CONNECTION_NOT_ALLOWED	0x02
#define	SOCKS_NETWORK_UNREACHABLE	0x03
#define	SOCKS_HOST_UNREACHABLE		0x04
#define	SOCKS_CONNECTION_REFUSED	0x05
#define	SOCKS_TTL_EXPIRED		0x06
#define	SOCKS_COMMAND_NOT_SUPPORTED	0x07
#define	SOCKS_ADDRESS_NOT_SUPPORTED	0x08

/* for fetch_writev */
struct iovec;

void		 fetch_seterr(struct fetcherr *, int);
void		 fetch_syserr(void);
void		 fetch_info(const char *, ...) __printflike(1, 2);
int		 fetch_socks5_getenv(char **host, int *port);
int		 fetch_socks5_init(conn_t *conn, const char *host,
			int port, int verbose);
int		 fetch_default_port(const char *);
int		 fetch_default_proxy_port(const char *);
struct addrinfo *fetch_resolve(const char *, int, int);
int		 fetch_bind(int, int, const char *);
conn_t		*fetch_connect(const char *, int, int, int);
conn_t		*fetch_reopen(int);
conn_t		*fetch_ref(conn_t *);
#ifdef WITH_SSL
int		 fetch_ssl_cb_verify_crt(int, X509_STORE_CTX*);
#endif
int		 fetch_ssl(conn_t *, const struct url *, int);
ssize_t		 fetch_read(conn_t *, char *, size_t);
int		 fetch_getln(conn_t *);
ssize_t		 fetch_write(conn_t *, const char *, size_t);
ssize_t		 fetch_writev(conn_t *, struct iovec *, int);
int		 fetch_putln(conn_t *, const char *, size_t);
int		 fetch_close(conn_t *);
int		 fetch_add_entry(struct url_ent **, int *, int *,
			const char *, struct url_stat *);
int		 fetch_netrc_auth(struct url *url);
int		 fetch_no_proxy_match(const char *);

#define	ftp_seterr(n)	 fetch_seterr(ftp_errlist, n)
#define	http_seterr(n)	 fetch_seterr(http_errlist, n)
#define	netdb_seterr(n)	 fetch_seterr(netdb_errlist, n)
#define	url_seterr(n)	 fetch_seterr(url_errlist, n)
#define	socks5_seterr(n) fetch_seterr(socks5_errlist, n)

#ifndef NDEBUG
#define	DEBUGF(...)							\
	do {								\
		if (fetchDebug)						\
			fprintf(stderr, __VA_ARGS__);			\
	} while (0)
#else
#define	DEBUGF(...)							\
	do {								\
		/* nothing */						\
	} while (0)
#endif

/*
 * I don't really like exporting http_request() and ftp_request(),
 * but the HTTP and FTP code occasionally needs to cross-call
 * eachother, and this saves me from adding a lot of special-case code
 * to handle those cases.
 *
 * Note that _*_request() free purl, which is way ugly but saves us a
 * whole lot of trouble.
 */
FILE		*http_request(struct url *, const char *,
			struct url_stat *, struct url *, const char *);
FILE		*http_request_body(struct url *, const char *,
			struct url_stat *, struct url *, const char *,
			const char *, const char *);
FILE		*ftp_request(struct url *, const char *,
			struct url_stat *, struct url *, const char *);

/*
 * Check whether a particular flag is set
 */
#define	CHECK_FLAG(x)	(flags && strchr(flags, (x)))

#endif
