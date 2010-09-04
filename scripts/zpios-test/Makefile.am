pkglibexecdir = $(libexecdir)/@PACKAGE@/zpios-test
dist_pkglibexec_SCRIPTS = \
        $(top_srcdir)/scripts/zpios-test/16th-8192rc-4rs-1cs-4off.sh \
        $(top_srcdir)/scripts/zpios-test/1th-16rc-4rs-1cs-4off.sh \
        $(top_srcdir)/scripts/zpios-test/1x256th-65536rc-4rs-1cs-4off.sh \
        $(top_srcdir)/scripts/zpios-test/256th-65536rc-4rs-1cs-4off.sh \
        $(top_srcdir)/scripts/zpios-test/4th-1024rc-4rs-1cs-4off.sh \
        $(top_srcdir)/scripts/zpios-test/large.sh \
        $(top_srcdir)/scripts/zpios-test/large-thread-survey.sh \
        $(top_srcdir)/scripts/zpios-test/medium.sh \
        $(top_srcdir)/scripts/zpios-test/small.sh \
        $(top_srcdir)/scripts/zpios-test/tiny.sh

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
