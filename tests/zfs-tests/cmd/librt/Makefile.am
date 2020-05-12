include $(top_srcdir)/config/Rules.am

if BUILD_MACOS

lib_LTLIBRARIES = librt.la

nodist_librt_la_SOURCES = \
	mach_gettime.c

# We don't want .dylib, but .so.1 as it is hardcoded into test-runner.py
# -module -version-info produces librt.1.so (not .so.1 as we need)
librt_la_LDFLAGS=-module -avoid-version -shrext .so.1

#install-exec-local: librt_la
#	ln -s

endif

