include $(top_srcdir)/config/Rules.am

AUTOMAKE_OPTIONS = subdir-objects

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zfs_util

zfs_util_SOURCES = \
	zfs_util.c

zfs_util_LDADD = \
        $(top_builddir)/lib/libzfs/libzfs.la

zfs_util_LDFLAGS = -static -framework IOKit

# build the /Library/Filesystems/zfs.fs/ bundle

FS_BUNDLEPREFIX = /Library/Filesystems
FS_BUNDLEDIR = $(DESTDIR)$(FS_BUNDLEPREFIX)/zfs.fs
PKGINFO = PkgInfo
INFO_PLIST = Info.plist
INFOPLIST_STRINGS = InfoPlist.strings

bin_PROGRAMS = zfs.fs

dist_noinst_DATA = $(INFO_PLIST) English.lproj/$(INFOPLIST_STRINGS) $(PKGINFO)

zfs.fs/Contents/$(PKGINFO):

zfs.fs/Contents/$(INFO_PLIST):

zfs.fs/Contents/Resources/English.lproj/$(INFOPLIST_STRINGS):

zfs.fs$(EXEEXT): zfs.fs/Contents/$(PKGINFO) zfs.fs/Contents/$(INFO_PLIST) zfs.fs/Contents/Resources/English.lproj/$(INFOPLIST_STRINGS) $(INFO_PLIST) English.lproj/$(INFOPLIST_STRINGS) $(PKGINFO) zfs_util
	rm -rf zfs.fs
	mkdir -p zfs.fs/Contents/Resources/English.lproj
	cp -f $(PKGINFO) zfs.fs/Contents/
	cp -f $(INFO_PLIST) zfs.fs/Contents/
	cp -f zfs_util zfs.fs/Contents/Resources/
	cp -f English.lproj/$(INFOPLIST_STRINGS) zfs.fs/Contents/Resources/English.lproj/
	plutil -convert binary1 zfs.fs/Contents/Resources/English.lproj/InfoPlist.strings

install-exec-local:: zfs.fs
	rm -f $(FS_BUNDLEDIR)/Contents/$(PKGINFO)
	rm -f $(FS_BUNDLEDIR)/Contents/$(INFO_PLIST)
	rm -f $(FS_BUNDLEDIR)/Contents/Resources/English.lproj/$(INFOPLIST_STRINGS)
	mkdir -p $(FS_BUNDLEDIR)
	rsync -aH zfs.fs/ $(FS_BUNDLEDIR)/
	cp -f $(DESTDIR)/$(sbindir)/zfs $(FS_BUNDLEDIR)/Contents/Resources/mount_zfs
	ln -fs /usr/bin/true $(FS_BUNDLEDIR)/Contents/Resources/fsck_zfs
	ln -fs /usr/bin/true $(FS_BUNDLEDIR)/Contents/Resources/newfs_zfs
	chown -R root:wheel $(FS_BUNDLEDIR) || echo "Unable to chown root:wheel in $(FS_BUNDLEDIR)"

clean-binPROGRAMS:
	rm -rf zfs.fs/

clean:
	rm -rf zfs.fs

uninstall-hook:
	rm -rf "$(FS_BUNDLEDIR)"
