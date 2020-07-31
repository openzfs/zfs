### Development
- Install OpenZFS test certificate
  - Install `test_sign_cert_nopass.pfx` (password: )
  - Certificate should be installed into
    1. "Personal" in "Current User"

### Target
- Install OpenZFS test certificate
  - Install `test_sign_cert_nopass.pfx` (password: )
  - Certificate should be installed into
    1. "Trusted Root Certification Authority" in "Local Computer" (not current user) *and*
    2. "Trusted Publishers" in "Local Computer" (not current user)
- Enable test signing
  - `> bcdedit.exe /set TESTSIGNING ON`
  - reboot the system to apply
