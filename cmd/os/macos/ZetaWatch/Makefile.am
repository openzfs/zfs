# ZetaWatch - macOS menu bar application that monitors ZFS pool status.
#
# Folded into this repository, with the original maintainer's (cbreak,
# https://github.com/cbreak-black/ZetaWatch) blessing, from ZetaWatch and
# ZFSWrapper so that ZFSWrapper's libzfs usage stays in sync with this repo's
# libzfs automatically instead of needing to be hand-patched every release.

AM_LIBTOOLFLAGS += --tag=CXX
CPPCHECKTARGETS += ZetaWatch

ZETAWATCH_TEAMID = 735AM5QEU3

ZETAWATCH_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(srcdir)/%D%/ZFSWrapper/include \
	-I$(srcdir)/%D%/ZetaWatch \
	-I$(srcdir)/%D%/ZetaWatch/InvariantDisks \
	-I$(srcdir)/%D%/CommonAuthorization \
	-I$(srcdir)/%D%/ZetaAuthorizationHelper \
	-DZFSW_HAS_ZPOOL_STATUS_COMPATIBILITY_ERR \
	-DZFSW_HAS_ZPOOL_STATUS_INCOMPATIBLE_FEAT

ZETAWATCH_APP_FRAMEWORKS = \
	-framework Cocoa \
	-framework Security \
	-framework ServiceManagement \
	-framework IOKit \
	-framework DiskArbitration \
	-framework Foundation \
	-framework UserNotifications

##
## ZFSWrapper - C++ wrapper around libzfs/libzfs_core/libnvpair, shared by
## the app and the privileged helper. Compiled directly into both consuming
## targets below (it's 3 small files - not worth a convenience library).
##
ZFSWRAPPER_SOURCES = \
	%D%/ZFSWrapper/include/ZFSNVList.hpp \
	%D%/ZFSWrapper/include/ZFSStrings.hpp \
	%D%/ZFSWrapper/include/ZFSUtils.hpp \
	%D%/ZFSWrapper/src/ZFSNVList.cpp \
	%D%/ZFSWrapper/src/ZFSStrings.cpp \
	%D%/ZFSWrapper/src/ZFSStringsNS.mm \
	%D%/ZFSWrapper/src/ZFSUtils.cpp

##
## ZetaAuthorizationHelper - privileged helper tool, installed by the app
## itself via SMJobBless() on first launch (not staged by `make install`).
## It is a flat Mach-O tool, not a bundle: its Info.plist/Launchd.plist are
## embedded as linker sections, exactly like Xcode's
## CREATE_INFOPLIST_SECTION_IN_BINARY used to do.
##
noinst_PROGRAMS += ZetaAuthorizationHelper

ZetaAuthorizationHelper_SOURCES = \
	$(ZFSWRAPPER_SOURCES) \
	%D%/ZetaAuthorizationHelper/ZetaAuthorizationHelper.h \
	%D%/ZetaAuthorizationHelper/ZetaAuthorizationHelper.mm \
	%D%/ZetaAuthorizationHelper/ZetaAuthorizationHelperProtocol.h \
	%D%/ZetaAuthorizationHelper/ZetaCPPUtils.hpp \
	%D%/ZetaAuthorizationHelper/main.m \
	%D%/CommonAuthorization/CommonAuthorization.h \
	%D%/CommonAuthorization/CommonAuthorization.m

ZetaAuthorizationHelper_CPPFLAGS = $(ZETAWATCH_CPPFLAGS)
ZetaAuthorizationHelper_CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-missing-field-initializers
ZetaAuthorizationHelper_OBJCFLAGS = -fobjc-arc -Wall -Wextra
ZetaAuthorizationHelper_OBJCXXFLAGS = -std=c++17 -fobjc-arc -Wall -Wextra -Wno-missing-field-initializers
ZetaAuthorizationHelper_LDFLAGS = \
	-Wl,-sectcreate,__TEXT,__info_plist,$(srcdir)/%D%/ZetaAuthorizationHelper/Info.plist \
	-Wl,-sectcreate,__TEXT,__launchd_plist,$(srcdir)/%D%/ZetaAuthorizationHelper/Launchd.plist \
	-framework Security -framework Foundation
ZetaAuthorizationHelper_LDADD = libzfs.la

##
## ZetaLoginItemHelper - trampoline app that relaunches the main app at
## login, registered/unregistered via SMLoginItemSetEnabled(). Needs a real
## bundle (Contents/Info.plist), unlike the authorization helper above.
##
noinst_PROGRAMS += ZetaLoginItemHelper_bin

ZetaLoginItemHelper_bin_SOURCES = \
	%D%/ZetaLoginItemHelper/main.m

ZetaLoginItemHelper_bin_OBJCFLAGS = -fobjc-arc -Wall -Wextra
ZetaLoginItemHelper_bin_LDFLAGS = -static -framework Cocoa -framework Foundation

noinst_ZetaLoginItemHelper.app: ZetaLoginItemHelper_bin
	$(AM_V_GEN)$(MKDIR_P) ZetaLoginItemHelper.app/Contents/MacOS
	$(AM_V_at)cp -f $(srcdir)/%D%/ZetaLoginItemHelper/Info.plist ZetaLoginItemHelper.app/Contents/Info.plist
	$(AM_V_at)cp -f ZetaLoginItemHelper_bin ZetaLoginItemHelper.app/Contents/MacOS/ZetaLoginItemHelper
	$(AM_V_at)/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $(ZFS_META_VERSION)" \
		ZetaLoginItemHelper.app/Contents/Info.plist
	$(AM_V_at)/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $(ZFS_META_VERSION)" \
		ZetaLoginItemHelper.app/Contents/Info.plist

##
## ZetaWatch.app - the menu bar application itself.
##
noinst_PROGRAMS += ZetaWatch_bin

ZetaWatch_bin_SOURCES = \
	$(ZFSWRAPPER_SOURCES) \
	%D%/ZetaWatch/main.m \
	%D%/ZetaWatch/PathValueTransformer.m \
	%D%/ZetaWatch/SizeTransformer.mm \
	%D%/ZetaWatch/ZetaAuthorization.h \
	%D%/ZetaWatch/ZetaAuthorization.mm \
	%D%/ZetaWatch/ZetaAutoImporter.h \
	%D%/ZetaWatch/ZetaAutoImporter.mm \
	%D%/ZetaWatch/ZetaBookmarkMenu.h \
	%D%/ZetaWatch/ZetaBookmarkMenu.mm \
	%D%/ZetaWatch/ZetaCommanderBase.h \
	%D%/ZetaWatch/ZetaCommanderBase.mm \
	%D%/ZetaWatch/ZetaConfirmDialog.h \
	%D%/ZetaWatch/ZetaConfirmDialog.mm \
	%D%/ZetaWatch/ZetaDictQueryDialog.h \
	%D%/ZetaWatch/ZetaDictQueryDialog.mm \
	%D%/ZetaWatch/ZetaFileSystemPropertyMenu.h \
	%D%/ZetaWatch/ZetaFileSystemPropertyMenu.mm \
	%D%/ZetaWatch/ZetaFormatHelpers.cpp \
	%D%/ZetaWatch/ZetaFormatHelpers.hpp \
	%D%/ZetaWatch/ZetaImportMenu.h \
	%D%/ZetaWatch/ZetaImportMenu.mm \
	%D%/ZetaWatch/ZetaKeyLoader.h \
	%D%/ZetaWatch/ZetaKeyLoader.mm \
	%D%/ZetaWatch/ZetaMainMenu.h \
	%D%/ZetaWatch/ZetaMainMenu.mm \
	%D%/ZetaWatch/ZetaNotificationCenter.h \
	%D%/ZetaWatch/ZetaNotificationCenter.mm \
	%D%/ZetaWatch/ZetaPoolPropertyMenu.h \
	%D%/ZetaWatch/ZetaPoolPropertyMenu.mm \
	%D%/ZetaWatch/ZetaPoolWatcher.h \
	%D%/ZetaWatch/ZetaPoolWatcher.mm \
	%D%/ZetaWatch/ZetaQueryDialog.h \
	%D%/ZetaWatch/ZetaQueryDialog.mm \
	%D%/ZetaWatch/ZetaSnapshotMenu.h \
	%D%/ZetaWatch/ZetaSnapshotMenu.mm \
	%D%/ZetaWatch/ZetaWatchDelegate.h \
	%D%/ZetaWatch/ZetaWatchDelegate.mm \
	%D%/ZetaWatch/InvariantDisks/IDDiskArbitrationDispatcher.cpp \
	%D%/ZetaWatch/InvariantDisks/IDDiskArbitrationDispatcher.hpp \
	%D%/ZetaWatch/InvariantDisks/IDDiskArbitrationHandler.hpp \
	%D%/ZetaWatch/InvariantDisks/IDDiskArbitrationUtils.cpp \
	%D%/ZetaWatch/InvariantDisks/IDDiskArbitrationUtils.hpp \
	%D%/CommonAuthorization/CommonAuthorization.h \
	%D%/CommonAuthorization/CommonAuthorization.m

ZetaWatch_bin_CPPFLAGS = $(ZETAWATCH_CPPFLAGS)
ZetaWatch_bin_CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-missing-field-initializers
ZetaWatch_bin_OBJCFLAGS = -fobjc-arc -Wall -Wextra
ZetaWatch_bin_OBJCXXFLAGS = -std=c++17 -fobjc-arc -Wall -Wextra -Wno-missing-field-initializers
ZetaWatch_bin_LDFLAGS = $(ZETAWATCH_APP_FRAMEWORKS)
ZetaWatch_bin_LDADD = libzfs.la

# Nib compilation - ibtool can compile each xib standalone. ibtool ships
# only with full Xcode.app, never the standalone Command Line Tools; on a
# CLT-only machine (no $(IBTOOL)) fall back to the pre-compiled .nib
# committed alongside the source .xib - editing the UI still needs full
# Xcode, but plain building doesn't.
ZetaWatch.app/Contents/Resources/Base.lproj/%.nib: $(srcdir)/%D%/ZetaWatch/Base.lproj/%.xib \
		$(srcdir)/%D%/ZetaWatch/Base.lproj/%.nib
	$(AM_V_GEN)$(MKDIR_P) ZetaWatch.app/Contents/Resources/Base.lproj
	$(AM_V_at)if test -n "$(IBTOOL)"; then \
		$(IBTOOL) --compile $@ $< ; \
	else \
		cp -f $(srcdir)/%D%/ZetaWatch/Base.lproj/$*.nib $@ ; \
	fi

ZETAWATCH_NIBS = \
	ZetaWatch.app/Contents/Resources/Base.lproj/MainMenu.nib \
	ZetaWatch.app/Contents/Resources/Base.lproj/NewFS.nib \
	ZetaWatch.app/Contents/Resources/Base.lproj/NewVol.nib

dist_noinst_DATA += \
	%D%/ZetaWatch/Base.lproj/MainMenu.xib \
	%D%/ZetaWatch/Base.lproj/MainMenu.nib \
	%D%/ZetaWatch/Base.lproj/NewFS.xib \
	%D%/ZetaWatch/Base.lproj/NewFS.nib \
	%D%/ZetaWatch/Base.lproj/NewVol.xib \
	%D%/ZetaWatch/Base.lproj/NewVol.nib \
	%D%/ZetaWatch/Base.lproj/Localizable.strings \
	%D%/ZetaWatch/Assets.car \
	%D%/ZetaWatch/Info.plist \
	%D%/ZetaWatch/ZetaWatch.entitlements \
	%D%/ZetaAuthorizationHelper/Info.plist \
	%D%/ZetaAuthorizationHelper/Launchd.plist \
	%D%/ZetaLoginItemHelper/Info.plist

# Icon asset catalog. actool has the same Xcode-only availability as
# ibtool above; fall back to the committed pre-compiled Assets.car.
ZetaWatch.app/Contents/Resources/Assets.car: $(srcdir)/%D%/ZetaWatch/Assets.xcassets \
		$(srcdir)/%D%/ZetaWatch/Assets.car
	$(AM_V_GEN)$(MKDIR_P) ZetaWatch.app/Contents/Resources
	$(AM_V_at)if test -n "$(ACTOOL)"; then \
		$(ACTOOL) --output-format human-readable-text --notices --warnings \
			--platform macosx --minimum-deployment-target 11.0 \
			--app-icon AppIcon --output-partial-info-plist /dev/null \
			--compile ZetaWatch.app/Contents/Resources \
			$(srcdir)/%D%/ZetaWatch/Assets.xcassets > /dev/null ; \
	else \
		cp -f $(srcdir)/%D%/ZetaWatch/Assets.car $@ ; \
	fi

# Assemble the app bundle: embeds the privileged helper at
# Contents/Library/LaunchServices and the login item at
# Contents/Library/LoginItems, per SMJobBless()/SMLoginItemSetEnabled()
# convention.
# Built from "all" via ALL_LOCAL rather than bin_PROGRAMS: listing these in
# a PROGRAMS variable makes automake emit its own link rule for the same
# target, which then collides with the recipe below.
ALL_LOCAL += noinst_ZetaWatch.app

noinst_ZetaWatch.app: ZetaWatch_bin ZetaAuthorizationHelper noinst_ZetaLoginItemHelper.app \
		$(ZETAWATCH_NIBS) ZetaWatch.app/Contents/Resources/Assets.car
	$(AM_V_GEN)$(MKDIR_P) ZetaWatch.app/Contents/MacOS
	$(AM_V_at)$(MKDIR_P) ZetaWatch.app/Contents/Library/LaunchServices
	$(AM_V_at)$(MKDIR_P) ZetaWatch.app/Contents/Library/LoginItems
	$(AM_V_at)cp -f $(srcdir)/%D%/ZetaWatch/Info.plist ZetaWatch.app/Contents/Info.plist
	$(AM_V_at)test -x .libs/ZetaWatch_bin && cp -f .libs/ZetaWatch_bin ZetaWatch.app/Contents/MacOS/ZetaWatch \
		|| cp -f ZetaWatch_bin ZetaWatch.app/Contents/MacOS/ZetaWatch
	$(AM_V_at)test -x .libs/ZetaAuthorizationHelper && cp -f .libs/ZetaAuthorizationHelper \
		ZetaWatch.app/Contents/Library/LaunchServices/org.openzfsonosx.ZetaAuthorizationHelper \
		|| cp -f ZetaAuthorizationHelper \
		ZetaWatch.app/Contents/Library/LaunchServices/org.openzfsonosx.ZetaAuthorizationHelper
	$(AM_V_at)rsync -aH --delete ZetaLoginItemHelper.app/ \
		ZetaWatch.app/Contents/Library/LoginItems/ZetaLoginItemHelper.app/
	$(AM_V_at)/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $(ZFS_META_VERSION)" \
		ZetaWatch.app/Contents/Info.plist
	$(AM_V_at)/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $(ZFS_META_VERSION)" \
		ZetaWatch.app/Contents/Info.plist

INSTALL_DATA_HOOKS += zetawatch-install-exec-local
zetawatch-install-exec-local: noinst_ZetaWatch.app
	$(MKDIR_P) $(DESTDIR)/Applications
	rm -rf $(DESTDIR)/Applications/ZetaWatch.app
	rsync -aH ZetaWatch.app/ $(DESTDIR)/Applications/ZetaWatch.app/

CLEANFILES += \
	ZetaWatch.app/Contents/Info.plist \
	ZetaWatch.app/Contents/MacOS/ZetaWatch \
	ZetaWatch.app/Contents/Library/LaunchServices/org.openzfsonosx.ZetaAuthorizationHelper \
	ZetaWatch.app/Contents/Resources/Assets.car \
	ZetaWatch.app/Contents/Resources/AppIcon.icns \
	$(ZETAWATCH_NIBS) \
	ZetaLoginItemHelper.app/Contents/Info.plist \
	ZetaLoginItemHelper.app/Contents/MacOS/ZetaLoginItemHelper

CLEAN_LOCAL += zetawatch_clean_local
zetawatch_clean_local:
	@rm -rf ZetaWatch.app/Contents/Library/LoginItems/ZetaLoginItemHelper.app
	@rmdir -p ZetaWatch.app/Contents/Resources/Base.lproj \
		ZetaWatch.app/Contents/Library/LaunchServices \
		ZetaWatch.app/Contents/Library/LoginItems \
		ZetaWatch.app/Contents/Library \
		ZetaWatch.app/Contents/MacOS \
		ZetaWatch.app/Contents \
		ZetaWatch.app \
		ZetaLoginItemHelper.app/Contents/MacOS \
		ZetaLoginItemHelper.app/Contents \
		ZetaLoginItemHelper.app || true
