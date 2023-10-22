sbin_PROGRAMS   += zfs_util
CPPCHECKTARGETS += zfs_util

INFO_PLIST = %D%/Info.plist
INFOPLIST_STRINGS = %D%/English.lproj/InfoPlist.strings
PKGINFO = %D%/PkgInfo

zfs_util_SOURCES = \
	%D%/zfs_util.c

zfs_util_LDADD = \
        libzfs.la

zfs_util_LDFLAGS = -static -framework IOKit

# build the /Library/Filesystems/zfs.fs/ bundle

noinst_zfs_fs_SOURCES =

bin_PROGRAMS += noinst_zfs.fs

dist_noinst_DATA += $(INFO_PLIST) $(INFOPLIST_STRINGS) $(PKGINFO)

noinst_zfs.fs$(EXEEXT): zfs_util
	mkdir -p zfs.fs/Contents/Resources/English.lproj
	cp -f $(PKGINFO) zfs.fs/Contents/
	cp -f $(INFO_PLIST) zfs.fs/Contents/
	cp -f zfs_util zfs.fs/Contents/Resources/
	cp -f $(INFOPLIST_STRINGS) zfs.fs/Contents/Resources/English.lproj/
	/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $(ZFS_META_VERSION)" zfs.fs/Contents/Info.plist
	/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $(ZFS_META_VERSION)" zfs.fs/Contents/Info.plist
	plutil -convert binary1 zfs.fs/Contents/Resources/English.lproj/InfoPlist.strings

INSTALL_DATA_HOOKS += fs-install-exec-local
fs-install-exec-local: noinst_zfs.fs
	rm -f $(FS_BUNDLEDIR)/Contents/$(PKGINFO)
	rm -f $(FS_BUNDLEDIR)/Contents/$(INFO_PLIST)
	rm -f $(FS_BUNDLEDIR)/Contents/Resources/English.lproj/$(INFOPLIST_STRINGS)
	mkdir -p $(FS_BUNDLEDIR)
	rsync -aH zfs.fs/ $(FS_BUNDLEDIR)/
	ln -fs /usr/bin/true $(FS_BUNDLEDIR)/Contents/Resources/fsck_zfs
	ln -fs /usr/bin/true $(FS_BUNDLEDIR)/Contents/Resources/newfs_zfs
	chown -R root:wheel $(FS_BUNDLEDIR) || echo "Unable to chown root:wheel in $(FS_BUNDLEDIR)"


CLEANFILES += zfs.fs/Contents/Resources/English.lproj/InfoPlist.strings \
	zfs.fs/Contents/Resources/zfs_util \
	zfs.fs/Contents/Info.plist \
	zfs.fs/Contents/PkgInfo \
	zfs.fs/Contents/Resources/English.lproj/InfoPlist.strings \
	zfs.fs/Contents/Resources/fsck_zfs \
	zfs.fs/Contents/Resources/newfs_zfs

CLEAN_LOCAL += zfsutil_clean_local
zfsutil_clean_local:
	@rmdir zfs.fs/Contents/Resources/English.lproj \
	zfs.fs/Contents/Resources \
	zfs.fs/Contents \
	zfs.fs || true
