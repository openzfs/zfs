pkglibexecdir = $(libexecdir)/@PACKAGE@/zpool-config
dist_pkglibexec_SCRIPTS = \
        $(top_srcdir)/scripts/zpool-config/dm0-raid0.sh \
        $(top_srcdir)/scripts/zpool-config/dragon-raid0-1x70.sh \
        $(top_srcdir)/scripts/zpool-config/dragon-raid10-35x2.sh \
        $(top_srcdir)/scripts/zpool-config/dragon-raidz2-7x10.sh \
        $(top_srcdir)/scripts/zpool-config/dragon-raidz-7x10.sh \
        $(top_srcdir)/scripts/zpool-config/file-raid0.sh \
        $(top_srcdir)/scripts/zpool-config/file-raid10.sh \
        $(top_srcdir)/scripts/zpool-config/file-raidz2.sh \
        $(top_srcdir)/scripts/zpool-config/file-raidz.sh \
        $(top_srcdir)/scripts/zpool-config/hda-raid0.sh \
        $(top_srcdir)/scripts/zpool-config/lo-raid0.sh \
        $(top_srcdir)/scripts/zpool-config/lo-raid10.sh \
        $(top_srcdir)/scripts/zpool-config/lo-raidz2.sh \
        $(top_srcdir)/scripts/zpool-config/lo-raidz.sh \
        $(top_srcdir)/scripts/zpool-config/md0-raid10.sh \
        $(top_srcdir)/scripts/zpool-config/md0-raid5.sh \
        $(top_srcdir)/scripts/zpool-config/ram0-raid0.sh \
        $(top_srcdir)/scripts/zpool-config/sda-raid0.sh \
        $(top_srcdir)/scripts/zpool-config/supermicro-raid0-1x16.sh \
        $(top_srcdir)/scripts/zpool-config/supermicro-raid10-8x2.sh \
        $(top_srcdir)/scripts/zpool-config/supermicro-raidz2-4x4.sh \
        $(top_srcdir)/scripts/zpool-config/supermicro-raidz-4x4.sh \
        $(top_srcdir)/scripts/zpool-config/x4550-raid0-1x48.sh \
        $(top_srcdir)/scripts/zpool-config/x4550-raid10-24x2.sh \
        $(top_srcdir)/scripts/zpool-config/x4550-raidz2-8x6.sh \
        $(top_srcdir)/scripts/zpool-config/x4550-raidz-8x6.sh

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
