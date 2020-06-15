
AUTOMAKE_OPTIONS = subdir-objects

noinst_PROGRAMS = kextsymboltool

kextsymboltool_SOURCES = \
	kextsymboltool.c

kextsymboltool_CPPFLAGS = 
kextsymboltool_CFLAGS = 
kextsymboltool_LDFLAGS = -lstdc++

kernelexports: zfs.exports | kextsymboltool
	./kextsymboltool -arch x86_64 -import allsymbols -export zfs.exports -output kernelexports_64
	./kextsymboltool -arch i386 -import allsymbols -export zfs.exports -output kernelexports_32
	lipo -create kernelexports_32 kernelexports_64 -output kernelexports

clean:
	rm -f kernelexports kernelexports_32 kernelexports_64 allsymbols
	rm -f kextsymboltool.o kextsymboltool

allsymbols:
	$(NM) -gj $(MACH_KERNEL) > allsymbols

all:kextsymboltool allsymbols kernelexports
