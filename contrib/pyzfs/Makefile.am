# SPDX-License-Identifier: CDDL-1.0
dist_noinst_DATA += %D%/libzfs_core %D%/README %D%/LICENSE %D%/docs
SUBSTFILES += %D%/pyproject.toml

if PYZFS_ENABLED
ALL_LOCAL += pyzfs-all-local
pyzfs_V_1 = -v

pyzfs-all-local: %D%/pyproject.toml
	cd %D% && $(PYTHON) -m pip install --no-build-isolation --no-deps \
	  $(pyzfs_V_$(V)) -t build .

#
# On Debian (Ubuntu, and other downstream distros) the install location of
# Python packages is "../dist-packages" instead of "../site-packages" [1].
# The install location must match the location specified in the ZFS specfile
# (RPM macro "%{python_sitelib}") to avoid errors during the rpmbuild process.
# We use "--target" with "$(DESTDIR)$(pythonsitedir)" to install to the exact
# site-packages path on every supported system.
#
# [1] https://wiki.debian.org/Python#Deviations_from_upstream
#
install-exec-local:
	-$(RM) -r %D%/build/
	cd %D% && $(PYTHON) -m pip install --no-build-isolation --no-deps \
	  -t $(DESTDIR)$(pythonsitedir) \
	  .

CLEAN_LOCAL += pyzfs-clean-local
pyzfs-clean-local:
	-$(RM) -r %D%/build/ %D%/pyzfs.egg-info/
endif
