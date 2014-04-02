dist_man_MANS = \
	fsck.zfs.8 \
	mount.zfs.8 \
	vdev_id.8 \
	zdb.8 \
	zfs.8 \
	zinject.8 \
	zpool.8 \
	zstreamdump.8

nodist_man_MANS = \
	zed.8

EXTRA_DIST = \
	zed.8.in

zed.8: $(srcdir)/zed.8.in

do_subst = $(SED) \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	-e 's|@runstatedir[@]|$(runstatedir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g'

$(nodist_man_MANS): Makefile
	$(RM) $@ $@.tmp
	srcdir=''; \
	  test -f ./$@.in || srcdir=$(srcdir)/; \
	  $(do_subst) $${srcdir}$@.in >$@.tmp
	mv $@.tmp $@

install-data-local:
	$(INSTALL) -d -m 0755 "$(DESTDIR)$(mandir)/man8"

CLEANFILES = \
	$(nodist_man_MANS)
