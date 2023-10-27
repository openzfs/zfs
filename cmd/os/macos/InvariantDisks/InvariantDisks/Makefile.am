sbin_PROGRAMS   += InvariantDisks
CPPCHECKTARGETS += InvariantDisks

AM_LIBTOOLFLAGS += --tag=CXX

InvariantDisks_SOURCES = \
	%D%/IDLogUtils.cpp \
	%D%/IDLogUtils.hpp \
	%D%/IDBaseLinker.cpp \
	%D%/IDBaseLinker.hpp \
	%D%/IDCLI.cpp \
	%D%/IDCLI.hpp \
	%D%/IDDAHandlerIdle.cpp \
	%D%/IDDAHandlerIdle.hpp \
	%D%/IDDiskArbitrationDispatcher.cpp \
	%D%/IDDiskArbitrationDispatcher.hpp \
	%D%/IDDiskArbitrationHandler.hpp \
	%D%/IDDiskArbitrationUtils.cpp \
	%D%/IDDiskArbitrationUtils.hpp \
	%D%/IDDiskInfoLogger.cpp \
	%D%/IDDiskInfoLogger.hpp \
	%D%/IDDispatchUtils.cpp \
	%D%/IDDispatchUtils.hpp \
	%D%/IDException.cpp \
	%D%/IDException.hpp \
	%D%/IDFileUtils.mm \
	%D%/IDFileUtils.hpp \
	%D%/IDImagePathLinker.cpp \
	%D%/IDImagePathLinker.hpp \
	%D%/IDMediaPathLinker.cpp \
	%D%/IDMediaPathLinker.hpp \
	%D%/IDSerialLinker.cpp \
	%D%/IDSerialLinker.hpp \
	%D%/IDSymlinkHandle.cpp \
	%D%/IDSymlinkHandle.hpp \
	%D%/IDUUIDLinker.cpp \
	%D%/IDUUIDLinker.hpp \
	%D%/git-version.h \
	%D%/main.cpp

InvariantDisks_CXXFLAGS = -std=c++11 -stdlib=libc++
InvariantDisks_OBJCXXFLAGS = -std=c++11 -stdlib=libc++
InvariantDisks_LDFLAGS = -static -framework IOKit -framework DiskArbitration -framework CoreFoundation -framework Foundation
