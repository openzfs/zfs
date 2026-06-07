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
	%D%/test_zap \
	%D%/test_namecheck
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


%C%_test_namecheck_CFLAGS = $(AM_CFLAGS)

nodist_%C%_test_namecheck_SOURCES = \
	module/zcommon/zfs_namecheck.c

%C%_test_namecheck_SOURCES = \
	%D%/test_namecheck.c

%C%_test_namecheck_LDADD = \
	libspl.la \
	libunit.la


# test run and coverage targets below
PHONY += unit unit-coverage unit-coverage-html

_unit_run_%: %D%/%
	@echo "  UNITTEST $<" ; $< $(TOPT)

# note: any changes in switches to lcov or genhtml must be carefully checked
#       on 1.x and 2.x; the current option set is carefully chosen to allow
#       both to work sensibly

# .info is marked PRECIOUS, because its usually only created as an intermediate
# from one of the unit phony targets, but once it exists there's no point
# remaking it until and unless the test binary itself changes
.PRECIOUS: %D%/%.info
%D%/%.info: %D%/%
	@-${RM} $@
	@${LCOV} --quiet --quiet --zerocounters --directory $(top_srcdir)
	@echo "  UNITTEST $<" ; $< $(TOPT)
	@${LCOV} --quiet --quiet --capture  \
		--test-name $(notdir $<) \
		--directory $(top_srcdir) \
		--output-file $@ \
		--rc lcov_branch_coverage=1 \
		--rc geninfo_unexecuted_blocks=1 \
		$(addprefix --include $(abs_top_builddir)/, $(call \
		    $(join $(join nodist_%C%_, $(notdir $<)), _SOURCES))) \
		2>/dev/null

_unit_coverage_%: %D%/%.info
	@scripts/coverage_report.pl $<

_unit_coverage_html_%: %D%/%.info
	@-${RM} -r $(subst .info,_coverage, $<)
	@${GENHTML} --quiet -quiet \
		--rc lcov_branch_coverage=1 \
		--rc check_data_consistency=0 \
		--output-directory $(subst .info,_coverage, $<) \
		$< \
		2>/dev/null
	@echo "coverage results:" \
		"file://$(realpath %D%)/$(subst .info,_coverage,$(notdir $<))/index.html"

CLEAN_LOCAL += unit-clean-local
unit-clean-local:
	-${RM} -r %D%/*.info %D%/*_coverage/

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
unit-coverage-html: $(addprefix _unit_coverage_html_, $(_UNIT_TARGETS))
	@$(if $^, true, echo "ERROR: couldn't find unit test: $(T)" && false)
else
unit-coverage:
	@echo "unit test coverage not enabled."
	@echo "re-run configure with --enable-code-coverage"
	@false
unit-coverage-html: unit-coverage
endif
