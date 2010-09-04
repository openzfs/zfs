pkglibexecdir = $(libexecdir)/@PACKAGE@/zpios-profile
dist_pkglibexec_SCRIPTS = \
	$(top_srcdir)/scripts/zpios-profile/zpios-profile-disk.sh \
	$(top_srcdir)/scripts/zpios-profile/zpios-profile-pids.sh \
	$(top_srcdir)/scripts/zpios-profile/zpios-profile-post.sh \
	$(top_srcdir)/scripts/zpios-profile/zpios-profile-pre.sh \
	$(top_srcdir)/scripts/zpios-profile/zpios-profile.sh

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
