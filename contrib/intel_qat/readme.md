# Intel_QAT easy install script

This contrib contains community compatibility patches to get Intel QAT working on the following kernel versions:
- 5.6
- 5.7
- 5.8

These patches are based on the following Intel QAT version:
[1.7.l.4.10.0-00014](https://01.org/sites/default/files/downloads/qat1.7.l.4.10.0-00014.tar.gz)

When using QAT with above kernels versions, the following patches needs to be applied using:
patch -p1 < _$PATCH_
_Where $PATCH refers to the path of the patch in question_

### 5.6
/patch/0001-timespec.diff

### 5.7
/patch/0001-pci_aer.diff

### 5.8
/patch/0001-cryptohash.diff


_Patches are supplied by [Storage Performance Development Kit (SPDK)](https://github.com/spdk/spdk)_


