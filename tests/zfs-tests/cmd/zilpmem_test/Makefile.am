#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2021 by Christian Schwarz. All rights reserved.
#

include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

pkgexec_PROGRAMS = zilpmem_test zilpmem_testimpl

#
# What happens on `make` is the following:
# - `zilpmem_testimpl$(EXEEXT)` is evaluated
#     - `zilpmem_test$(EXEEXT)` is evaluated
#       - ./zilpmem_test, a libtool wrapper script that points to ./.libs/zilpmem_test
#       - ./.libs/zilpmem_test, the ELF binary from zilpmem_test_dummy.c
#     - `$(abs_builddir)/target/release/zilpmem_test` is evaluated
#       - generate-linking-metadata (phony) is evaluated
#         - invokes libtool with CCLD replaced with generate-linking-metadata.py
#           => generate-linking-metadata.py receives the raw linker args
#         		- generates .cargo/config.toml with target config that includes linker args
#         		- generates ld-metadata.json for consumption by zilpmem_test/build.rs
#				- generates ldlibrarypaths.env for consupmtion from shell
#       - invoke cargo to build the ELF binary $(abs_builddir)/target/release/zilpmem_test
#          - uses linker args from .cargo/config.toml
#          - build.rs emits a bunch of "cargo:..." lines based on ld-metadata.json
#     - we overwrite .libs/zilpmem_test$(EXEEXT) with $(abs_builddir)/target/release/zilpmem_test
#
# Outcome of this ordeal:
#
# - ./zilpmem_test is a fully usable libtool wrapper script
#    - Execute binary: ./zilpmmem_test works due to libtool wrapper
#    - GDB on binary: libtool --tag=CC --mode=execute rust-gdb ./zilpmem_test
# - cargo run works without setting LD_LIBRARY_PATH
# - cargo test works without setting LD_LIBRARY_PATH
# - debugging a test:
#     - rust-gdb target/release/deps/zilpmem_test-47424e4cc48e2791
#    		- does NOT work because LD_LIBRARY_PATH is not set
#     - cargo with gdb -- test -p zilpmem_test --release
#           - is also not smart enough, I opened an issue https://github.com/cbourjau/cargo-with/issues/30
#    - => XXX the test binaries (and only those) should have `rpath` set, but I don't know how do tell cargo this
#    - instead, use ldlibrarypaths.env
# - rust-analyzer works
#    - For a common crash & burn setup where
#       - the editor + rust-analyzer runs on the host machine
#       - the zfs tree resides on the host machine
#       - the zfs tree is NFS-mounted in a VM
#       - building & execution are done in the VM
#      use the ZILPMEM_TEST_BUILD_SUBSTITUTE_LD_LIBRARY_PATH env variable
#      to rewrite the library paths. Otherwise rust analyzer completions are rather disappointing.
#      If you still have problems the host linker probably doesn't understand the linker flags
#      in the generated .cargo/config.toml.
#      Cargo build on the guest won't produce _exactly_ the kinds of builds that we want
#      because the linker flags are missing but it's fine for development.
#      XXX Future Work: once build.rs can emit arbitrary linker flags stop generating .cargo/config.toml.

zilpmem_test_SOURCES = zilpmem_test_dummy.c
zilpmem_test_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libspl/libspl.la

zilpmem_test_LDFLAGS = \
	-ldl -lpmem -lpthread -lrt

.PHONY: generate-linking-metadata
generate-linking-metadata: CCLD=$(abs_srcdir)/generate-linking-metadata.py
generate-linking-metadata:
	mkdir -p "$(abs_srcdir)/.cargo"
	export GENERATE_LINKER_METADATA_PY_CARGOCONFIG=$(abs_srcdir)/.cargo/config.toml && \
	export GENERATE_LINKER_METADATA_PY_LDMETADATAJSON=$(abs_srcdir)/zilpmem_test-rs/ld-metadata.json && \
	export GENERATE_LINKER_METADATA_PY_LDPATHSENV=$(abs_srcdir)/zilpmem_test-rs/ldlibrarypaths.env && \
	$(zilpmem_test_LINK) $(zilpmem_test_LDADD)
	rm $@

.PHONY: $(abs_builddir)/target/release/zilpmem_test
$(abs_builddir)/target/release/zilpmem_test: generate-linking-metadata
	cargo --locked build --release --bin zilpmem_test

zilpmem_testimpl$(EXEEXT): zilpmem_test$(EXEEXT) $(abs_builddir)/target/release/zilpmem_test
	cp -a $(abs_builddir)/target/release/zilpmem_test .libs/zilpmem_test$(EXEEXT)

check-local:
	cargo --locked test

clean-local:
	cargo clean -p zilpmem_test -p bindings
