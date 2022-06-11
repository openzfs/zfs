sudoersddir = $(sysconfdir)/sudoers.d
sudoersd_DATA = \
	%D%/sudoers.d/zfs

dist_noinst_DATA += $(sudoersd_DATA)


sysconf_zfsdir = $(sysconfdir)/zfs

dist_sysconf_zfs_DATA = \
	%D%/zfs/vdev_id.conf.alias.example \
	%D%/zfs/vdev_id.conf.multipath.example \
	%D%/zfs/vdev_id.conf.sas_direct.example \
	%D%/zfs/vdev_id.conf.sas_switch.example \
	%D%/zfs/vdev_id.conf.scsi.example

sysconf_zfs_DATA = \
	%D%/zfs/zfs-functions

SUBSTFILES          += $(sysconf_zfs_DATA)
SHELLCHECKSCRIPTS   += $(sysconf_zfs_DATA)
$(call SHELLCHECK_OPTS,$(sysconf_zfs_DATA)): SHELLCHECK_SHELL = sh


if BUILD_LINUX
initconf_DATA = \
	%D%/default/zfs

SUBSTFILES          += $(initconf_DATA)
SHELLCHECKSCRIPTS   += $(initconf_DATA)
$(call SHELLCHECK_OPTS,$(initconf_DATA)): SHELLCHECK_SHELL = sh


if INIT_SYSV
dist_noinst_DATA += %D%/init.d/README.md

init_SCRIPTS = \
	%D%/init.d/zfs-import \
	%D%/init.d/zfs-load-key \
	%D%/init.d/zfs-mount \
	%D%/init.d/zfs-share \
	%D%/init.d/zfs-zed

SUBSTFILES        += $(init_SCRIPTS)
SHELLCHECKSCRIPTS += $(init_SCRIPTS)
$(call SHELLCHECK_OPTS,$(init_SCRIPTS)): SHELLCHECK_SHELL = sh
endif


if INIT_SYSTEMD
dist_systemdpreset_DATA = \
	%D%/systemd/system/50-zfs.preset

systemdunit_DATA = \
	%D%/systemd/system/zfs-import-cache.service \
	%D%/systemd/system/zfs-import-scan.service \
	%D%/systemd/system/zfs-import.target \
	%D%/systemd/system/zfs-mount.service \
	%D%/systemd/system/zfs-scrub-monthly@.timer \
	%D%/systemd/system/zfs-scrub-weekly@.timer \
	%D%/systemd/system/zfs-scrub@.service \
	%D%/systemd/system/zfs-trim-monthly@.timer \
	%D%/systemd/system/zfs-trim-weekly@.timer \
	%D%/systemd/system/zfs-trim@.service \
	%D%/systemd/system/zfs-share.service \
	%D%/systemd/system/zfs-volume-wait.service \
	%D%/systemd/system/zfs-volumes.target \
	%D%/systemd/system/zfs-zed.service \
	%D%/systemd/system/zfs.target

SUBSTFILES += $(systemdunit_DATA)

INSTALL_DATA_HOOKS += systemd-install-data-hook
systemd-install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(systemdunitdir)"
	ln -sf /dev/null "$(DESTDIR)$(systemdunitdir)/zfs-import.service"


systemdgenerator_PROGRAMS = \
	%D%/systemd/system-generators/zfs-mount-generator

%C%_systemd_system_generators_zfs_mount_generator_SOURCES = \
	%D%/systemd/system-generators/zfs-mount-generator.c

%C%_systemd_system_generators_zfs_mount_generator_LDADD = \
	libzfs.la

%C%_systemd_system_generators_zfs_mount_generator_LDFLAGS = -pthread

CPPCHECKTARGETS += $(systemdgenerator_PROGRAMS)
endif


if WANT_MODULES_LOAD_D
dist_modulesload_DATA = \
	%D%/modules-load.d/zfs.conf
endif
endif
