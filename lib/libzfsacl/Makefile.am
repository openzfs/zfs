dist_noinst_DATA += \
	%D%/libpyzfsacl.c \
	%D%/zfsacltests

SUBSTFILES += %D%/setup.py

ALL_LOCAL += libzfsacl-all-local
libzfsacl-all-local: %D%/setup.py
	cd %D% && $(PYTHON) setup.py egg_info -e . build

PYDIR = $(shell if [ -f /etc/debian_version ]; then \
    $(PYTHON) -c 'import site; print(site.getsitepackages()[0])'; \
    else echo $(pythonsitedir); fi)

INSTALL_DATA_HOOKS += libzfsacl-install-data-hook
libzfsacl-install-data-hook:
	cd %D% && $(PYTHON) setup.py egg_info -e . install \
	  --prefix $(prefix) \
	  --root $(DESTDIR)/ \
	  --install-lib $(PYDIR) \
	  --verbose

CLEAN_LOCAL += libzfsacl-clean-local
libzfsacl-clean-local:
	-$(RM) -r %D%/build/
