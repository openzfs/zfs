EXTRA_DIST = arc_summary2 arc_summary3

transform = $(program_transform_name)

if USING_PYTHON_2
dist_bin_SCRIPTS = arc_summary2
install-exec-hook:
	before=$$(echo arc_summary2 | sed '$(transform)'); \
	after=$$(echo arc_summary | sed '$(transform)'); \
	mv "$(DESTDIR)$(bindir)/$$before" "$(DESTDIR)$(bindir)/$$after"
else
dist_bin_SCRIPTS = arc_summary3
install-exec-hook:
	before=$$(echo arc_summary3 | sed '$(transform)'); \
	after=$$(echo arc_summary | sed '$(transform)'); \
	mv "$(DESTDIR)$(bindir)/$$before" "$(DESTDIR)$(bindir)/$$after"
endif
