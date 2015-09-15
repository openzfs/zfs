include $(top_srcdir)/config/Rules.am

install-exec-hook:
	$(LN_S) -f libzfs_core-$(LIBZFS_CORE_VARIANT).so $(DESTDIR)$(libdir)/libzfs_core.so
	$(LN_S) -f libzfs_core-$(LIBZFS_CORE_VARIANT).so.1 $(DESTDIR)$(libdir)/libzfs_core.so.1
	$(LN_S) -f libzfs_core-$(LIBZFS_CORE_VARIANT).so.1.0.0 $(DESTDIR)$(libdir)/libzfs_core.so.1.0.0
