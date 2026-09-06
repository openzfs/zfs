{
  stdenv,
  lib,
  pkgs,
  kernel,
  kernelModuleMakeFlags ? [ ],
  autoreconfHook,
  attr,
  coreutils,
  gawk,
  gnused,
  gnugrep,
  ksh,
  libtirpc,
  libuuid,
  nukeReferences,
  openssl,
  pam,
  pkg-config,
  python3,
  systemd,
  udevCheckHook,
  zlib,
}:

stdenv.mkDerivation {
  pname = "zfs";
  version = "2.3.4-${kernel.version}"; # Specify the version of ZFS you want

  src = ./..;

  preConfigure = ''
    # The kernel module builds some tests during the configurePhase, this envvar controls their parallelism
    export TEST_JOBS=$NIX_BUILD_CORES
    if [ -z "$enableParallelBuilding" ]; then
      export TEST_JOBS=1
    fi
  '';

  postPatch = ''
    patchShebangs scripts tests

    substituteInPlace ./config/user-systemd.m4    --replace-fail "/usr/lib/modules-load.d" "$out/etc/modules-load.d"
    substituteInPlace ./config/zfs-build.m4       --replace-fail "\$sysconfdir/init.d"     "$out/etc/init.d" \
      --replace-fail "/etc/default"            "$out/etc/default"
    substituteInPlace ./contrib/initramfs/Makefile.am \
      --replace-fail "/usr/share/initramfs-tools" "$out/usr/share/initramfs-tools"

    substituteInPlace ./udev/vdev_id \
      --replace-fail "PATH=/bin:/sbin:/usr/bin:/usr/sbin" \
       "PATH=${
         lib.makeBinPath [
           coreutils
           gawk
           gnused
           gnugrep
           systemd
         ]
       }"

    substituteInPlace ./config/zfs-build.m4 \
      --replace-fail "bashcompletiondir=/etc/bash_completion.d" \
        "bashcompletiondir=$out/share/bash-completion/completions" \
      --replace-fail 'DEBUG_CFLAGS="-Werror"' ' '

    # Tests
    # Not required with Nix
    sed -i s/"^constrain_path$"/""/ scripts/zfs-tests.sh
    substituteInPlace ./scripts/zfs-tests.sh \
      --replace-fail '"$STF_PATH/ksh"'            "${lib.getExe pkgs.ksh}"
    substituteInPlace ./tests/test-runner/bin/test-runner.py.in \
      --replace-fail "/usr/share/zfs/"            "$out/share/zfs/" \
      --replace-fail "KILL = 'kill'"            "KILL = '${lib.getExe' pkgs.coreutils "kill"}'" \
      --replace-fail "SUDO = 'sudo'"            "SUDO = '/run/wrappers/bin/sudo'" \
      --replace-fail "TRUE = 'true'"            "TRUE = '${lib.getExe' pkgs.coreutils "true"}'"
  '';

  buildInputs = [
    attr
    libtirpc
    libuuid
    openssl
    pam
    python3
    systemd
    zlib
  ];

  nativeBuildInputs = [
    autoreconfHook
    ksh
    nukeReferences
    pkg-config
    udevCheckHook
    kernel.moduleBuildDependencies
  ];

  enableParallelBuilding = true;

  configureFlags = [
    "--with-linux=${kernel.dev}/lib/modules/${kernel.modDirVersion}/source"
    "--with-linux-obj=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
    "--with-dracutdir=$(out)/lib/dracut"
    "--with-udevdir=$(out)/lib/udev"
    "--with-systemdunitdir=$(out)/etc/systemd/system"
    "--with-systemdpresetdir=$(out)/etc/systemd/system-preset"
    "--with-systemdgeneratordir=$(out)/lib/systemd/system-generator"
    "--with-mounthelperdir=$(out)/bin"
    "--libexecdir=$(out)/libexec"
    "--sysconfdir=/etc"
    "--localstatedir=/var"
    "--enable-systemd"
    "--enable-pam"

    # Debug
    "--enable-debug"
    "--enable-debuginfo"
    "--enable-asan"
    "--enable-ubsan"
    "--enable-debug-kmem"
    "--enable-debug-kmem-tracking"
  ]
  ++ map (f: "KERNEL_${f}") kernelModuleMakeFlags;

  doInstallCheck = true;

  installFlags = [
    "sysconfdir=\${out}/etc"
    "DEFAULT_INITCONF_DIR=\${out}/default"
    "INSTALL_MOD_PATH=\${out}"
  ];

  meta = with lib; {
    description = "OpenZFS on Linux";
    license = licenses.cddl;
    platforms = platforms.linux;
  };
}
