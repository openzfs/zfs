pkglibexecdir = $(libexecdir)/@PACKAGE@/zpool-config
dist_pkglibexec_SCRIPTS = \
	$(top_srcdir)/scripts/zpool-config/dm0-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/file-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/file-raid10.sh \
	$(top_srcdir)/scripts/zpool-config/file-raidz2.sh \
	$(top_srcdir)/scripts/zpool-config/file-raidz.sh \
	$(top_srcdir)/scripts/zpool-config/hda-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/lo-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/lo-raid10.sh \
	$(top_srcdir)/scripts/zpool-config/lo-raidz2.sh \
	$(top_srcdir)/scripts/zpool-config/lo-raidz.sh \
	$(top_srcdir)/scripts/zpool-config/lo-faulty-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/lo-faulty-raid10.sh \
	$(top_srcdir)/scripts/zpool-config/lo-faulty-raidz.sh \
	$(top_srcdir)/scripts/zpool-config/lo-faulty-raidz2.sh \
	$(top_srcdir)/scripts/zpool-config/lo-faulty-raidz3.sh \
	$(top_srcdir)/scripts/zpool-config/md0-raid10.sh \
	$(top_srcdir)/scripts/zpool-config/md0-raid5.sh \
	$(top_srcdir)/scripts/zpool-config/ram0-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/scsi_debug-noraid.sh \
	$(top_srcdir)/scripts/zpool-config/scsi_debug-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/scsi_debug-raid10.sh \
	$(top_srcdir)/scripts/zpool-config/scsi_debug-raidz.sh \
	$(top_srcdir)/scripts/zpool-config/scsi_debug-raidz2.sh \
	$(top_srcdir)/scripts/zpool-config/scsi_debug-raidz3.sh \
	$(top_srcdir)/scripts/zpool-config/sda-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/zpool-raid0.sh \
	$(top_srcdir)/scripts/zpool-config/zpool-raid10.sh \
	$(top_srcdir)/scripts/zpool-config/zpool-raidz.sh

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
