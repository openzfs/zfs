{ openzfs, ... }:

{
  name = "zts";

  globalTimeout = 24 * 60 * 60;

  nodes = {
    machine =
      { pkgs, lib, ... }:
      {
        virtualisation = {
          cores = 4;
          diskSize = 10 * 1024;
          memorySize = 16 * 1024;
        };

        boot.kernelPatches = [
          {
            name = "enable KASAN";
            patch = null;
            extraConfig = ''
              KASAN y
              DEBUG_KMEMLEAK y
              GCOV_KERNEL y
            '';
          }
        ];

        boot.kernelParams = [
          "kasan.fault=report"
        ];

        boot.loader.systemd-boot.enable = true;
        boot.initrd.systemd.enable = true;

        networking.hostId = "deadbeef";
        boot.zfs.package = openzfs;
        boot.zfs.modulePackage = openzfs;
        boot.supportedFilesystems = [ "zfs" ];

        environment.systemPackages =
          let
            zfsTestsScript = pkgs.writeShellScriptBin "zfs-tests" ''
              export STF_PATH=${
                lib.makeBinPath [
                  pkgs.bash
                  pkgs.coreutils
                  pkgs.bzip2
                  pkgs.gawk
                  pkgs.ksh
                  pkgs.lvm2
                  pkgs.su
                  pkgs.sudo
                  pkgs.systemd
                  pkgs.util-linux
                  openzfs
                ]
              };

              export LOSETUP="${lib.getExe' pkgs.util-linux "losetup"}"
              export DMSETUP="${lib.getExe' pkgs.lvm2.bin "dmsetup"}"

              ${openzfs}/share/zfs/zfs-tests.sh -K -m -v -x
            '';
          in
          [
            zfsTestsScript
            openzfs
          ];

        users.users.tester = {
          isNormalUser = true;
          description = "ZFS tester";
          password = "foobar";
          extraGroups = [ "wheel" ];
        };

        security.sudo.wheelNeedsPassword = false;
      };

  };

  testScript = ''
    machine.wait_for_unit("multi-user.target")

    machine.log(machine.succeed(
      "sudo -u tester -- zfs-tests"
    ))

    _, report_location = machine.execute("find /tmp -name zts-report")

    machine.copy_from_vm(report_location, "")
  '';
}
