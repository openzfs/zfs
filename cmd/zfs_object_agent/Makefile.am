lib_LTLIBRARIES = libzoa.la

sbin_PROGRAMS = zcachedb zfs_object_agent zfs_object_perf zoa_test

common_sources = $(wildcard util/src/*.rs util/Cargo.toml zettacache/src/*.rs zettacache/Cargo.toml zettaobject/src/*.rs zettaobject/Cargo.toml Cargo.lock Cargo.toml)

zcachedb_SOURCES = $(wildcard zcdb/src/*.rs zcdb/Cargo.toml) $(common_sources)

zfs_object_agent_SOURCES = $(wildcard server/src/*.rs server/Cargo.toml) $(common_sources)

zfs_object_perf_SOURCES = $(wildcard object_perf/src/*.rs object_perf/Cargo.toml) $(common_sources)

zoa_test_SOURCES = $(wildcard client/src/*.rs client/Cargo.toml) $(common_sources)

libzoa_la_SOURCES = $(wildcard zoa/src/*.rs zoa/Cargo.toml) $(common_sources)

RUSTFLAGS = \
	-L $(abs_top_builddir)/lib/libzfs/.libs \
	-L $(abs_top_builddir)/lib/libnvpair/.libs

.PHONY: \
	zcachedb$(EXEEXT) \
	zfs_object_agent$(EXEEXT) \
	zfs_object_perf$(EXEEXT) \
	zoa_test$(EXEEXT) \
	libzoa.la

libzoa.la :
	RUSTFLAGS="$(RUSTFLAGS)" cargo build --package zoa $(RUSTTARGET)
	cp target/@RUSTDIR@/libzoa.so .

zfs_object_agent$(EXEEXT) :
	RUSTFLAGS="$(RUSTFLAGS)" cargo build --package zfs_object_agent $(RUSTTARGET)
	cp target/@RUSTDIR@/zfs_object_agent .

zfs_object_perf$(EXEEXT) :
	RUSTFLAGS="$(RUSTFLAGS)" cargo build --package zfs_object_perf $(RUSTTARGET)
	cp target/@RUSTDIR@/zfs_object_perf .

zoa_test$(EXEEXT) :
	RUSTFLAGS="$(RUSTFLAGS)" cargo build --package zoa_test $(RUSTTARGET)
	cp target/@RUSTDIR@/zoa_test .

zcachedb$(EXEEXT) :
	RUSTFLAGS="$(RUSTFLAGS)" cargo build --package zcachedb $(RUSTTARGET)
	cp target/@RUSTDIR@/zcachedb .

all: build

build: \
	libzoa.la \
	zcachedb$(EXEEXT) \
	zfs_object_agent$(EXEEXT) \
	zfs_object_perf$(EXEEXT) \
	zoa_test$(EXEEXT)

install-exec-hook :
	cp target/@RUSTDIR@/libzoa.so $(DESTDIR)$(libdir)/libzoa.so
