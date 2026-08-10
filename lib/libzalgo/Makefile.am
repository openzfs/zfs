noinst_LTLIBRARIES += libzalgo.la libzalgo_shim_fletcher.la libzalgo_shim_icp.la libzalgo_shim_openssl.la
CPPCHECKTARGETS    += libzalgo.la libzalgo_shim_fletcher.la libzalgo_shim_icp.la libzalgo_shim_openssl.la

libzalgo_la_CFLAGS  = $(AM_CFLAGS) $(KERNEL_CFLAGS) $(LIBRARY_CFLAGS)
libzalgo_la_CPPFLAGS  = $(AM_CPPFLAGS)

nodist_libzalgo_la_SOURCES = \
	module/zalgo/zalgo.c

libzalgo_shim_fletcher_la_CFLAGS  = $(libzalgo_la_CFLAGS)
libzalgo_shim_fletcher_la_CPPFLAGS  = $(libzalgo_la_CPPFLAGS)

nodist_libzalgo_shim_fletcher_la_SOURCES = \
	module/zalgo/zalgo_shim_fletcher.c

libzalgo_shim_icp_la_CFLAGS  = $(libzalgo_la_CFLAGS)
libzalgo_shim_icp_la_CPPFLAGS  = $(libzalgo_la_CPPFLAGS)

nodist_libzalgo_shim_icp_la_SOURCES = \
	module/zalgo/zalgo_shim_icp.c

libzalgo_shim_openssl_la_CFLAGS  = $(libzalgo_la_CFLAGS)
libzalgo_shim_openssl_la_CPPFLAGS  = $(libzalgo_la_CPPFLAGS)

nodist_libzalgo_shim_openssl_la_SOURCES = \
	module/zalgo/zalgo_shim_openssl.c

