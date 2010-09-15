pkglibexecdir = $(libexecdir)/@PACKAGE@/zpool-layout
dist_pkglibexec_SCRIPTS = \
	$(top_srcdir)/scripts/zpool-layout/dragon.ddn.conf \
	$(top_srcdir)/scripts/zpool-layout/dragon.llnl.conf

all:
	@list='$(dist_pkglibexec_SCRIPTS)'; \
	for file in $$list; do \
		link=$$(basename $$file); \
		if [ ! -e $$link ]; then \
			$(LN_S) $$file $$link; \
		fi \
	done

clean:
	@list='$(dist_pkglibexec_SCRIPTS)'; \
	for file in $$list; do \
		link=$$(basename $$file); \
		if [ -L $$link ]; then \
			$(RM) $$link; \
		fi \
	done
