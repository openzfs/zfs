bin_SCRIPTS = arc_summary

CLEANFILES = arc_summary
EXTRA_DIST = arc_summary2 arc_summary3

if USING_PYTHON_2
SCRIPT = arc_summary2
else
SCRIPT = arc_summary3
endif

arc_summary: $(SCRIPT)
	cp $< $@
