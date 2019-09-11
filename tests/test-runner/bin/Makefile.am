pkgdatadir = $(datadir)/@PACKAGE@/test-runner/bin
dist_pkgdata_SCRIPTS = \
	test-runner.py \
	zts-report.py
#
# These scripts are compatible with both Python 2.6 and 3.4.  As such the
# python 3 shebang can be replaced at install time when targeting a python
# 2 system.  This allows us to maintain a single version of the source.
#
if USING_PYTHON_2
install-data-hook:
	sed --in-place 's|^#!/usr/bin/env python3|#!/usr/bin/env python2|' \
	    $(DESTDIR)$(pkgdatadir)/test-runner.py \
	    $(DESTDIR)$(pkgdatadir)/zts-report.py
endif
