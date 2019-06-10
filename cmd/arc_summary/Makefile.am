EXTRA_DIST = arc_summary2 arc_summary3

if USING_PYTHON_2
dist_bin_SCRIPTS = arc_summary2
install-exec-hook:
	mv $(DESTDIR)$(bindir)/arc_summary2 $(DESTDIR)$(bindir)/arc_summary
else
dist_bin_SCRIPTS = arc_summary3
install-exec-hook:
	mv $(DESTDIR)$(bindir)/arc_summary3 $(DESTDIR)$(bindir)/arc_summary
endif
