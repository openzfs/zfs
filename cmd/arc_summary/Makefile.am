transform = $(program_transform_name)

if USING_PYTHON_2
SCRIPT = arc_summary2
else
SCRIPT = arc_summary3
endif

dist_bin_SCRIPTS = $(SCRIPT)

install-exec-hook:
	before=$$(echo $(SCRIPT) | sed '$(transform)'); \
	after=$$(echo arc_summary | sed '$(transform)'); \
	mv "$(DESTDIR)$(bindir)/$$before" "$(DESTDIR)$(bindir)/$$after"
