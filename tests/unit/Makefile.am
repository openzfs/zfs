# SPDX-License-Identifier: CDDL-1.0

# libunit.la includes munit and any additional tools that apply to all tests
libunit_la_CFLAGS = $(AM_CFLAGS)

noinst_LTLIBRARIES += libunit.la
libunit_la_SOURCES = \
	%D%/mock_dmu.c \
	%D%/mock_dmu.h \
	%D%/munit.c \
	%D%/munit.h \
	%D%/unit.c \
	%D%/unit.h


# all test binaries
UNIT_TESTS = \
	%D%/test_zap
noinst_PROGRAMS = $(UNIT_TESTS)


%C%_test_zap_CFLAGS = $(AM_CFLAGS)

nodist_%C%_test_zap_SOURCES = \
	module/zfs/zap.c \
	module/zfs/zap_fat.c \
	module/zfs/zap_impl.c \
	module/zfs/zap_micro.c \
	module/zfs/zap_leaf.c \
	module/zfs/u8_textprep.c

%C%_test_zap_SOURCES = \
	%D%/test_zap.c

%C%_test_zap_LDADD = \
	libspl.la \
	libbtree.la \
	libunit.la


# test run and coverage targets below
PHONY += unit unit-coverage

_unit_run_%: %D%/%
	@echo "  UNITTEST $<" ; $<

_unit_coverage_%: _unit_run_%
	@${LCOV} --quiet --capture  \
		--test-name $(subst _unit_coverage_, , $@) \
		--directory $(top_srcdir) \
		--output-file \
			%D%/$(join $(subst _unit_coverage_, , $@), .info) \
		$(addprefix --include , $(call $(join \
			$(subst _unit_coverage_, nodist_%C%_, $@), _SOURCES)))
	@${GENHTML} --quiet \
		%D%/$(join $(subst _unit_coverage_, , $@), .info) \
		--output-directory \
			%D%/$(join $(subst _unit_coverage_, , $@), _coverage)
	@echo "coverage results:" \
		"file://$(realpath %D%)/$(join $(subst _unit_coverage_, , $@), _coverage)/index.html"

_UNIT_ALL_TARGETS = $(notdir $(UNIT_TESTS))
_UNIT_FIND_TARGET = \
	$(foreach cmd, $(UNIT_TESTS), \
		$(if $(filter $(join test_, $(1)), $(notdir $(cmd))), \
			$(notdir $(cmd))))

_UNIT_TARGETS = $(if $(T), \
	$(call _UNIT_FIND_TARGET, $(T)), $(call _UNIT_ALL_TARGETS))

unit: $(addprefix _unit_run_, $(_UNIT_TARGETS))
	@$(if $^, true, echo "ERROR: couldn't find unit test: $(T)" && false)

if CODE_COVERAGE_ENABLED
unit-coverage: $(addprefix _unit_coverage_, $(_UNIT_TARGETS))
	@$(if $^, true, echo "ERROR: couldn't find unit test: $(T)" && false)
else
unit-coverage:
	@echo "unit test coverage not enabled."
	@echo "re-run configure with --enable-code-coverage"
	@false
endif
