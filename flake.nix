{
  description = "Nix flake for OpenZFS (ZFS)";

  # The inputs, such as Nixpkgs and flake-utils
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable"; # Adjust to the desired channel or version

    flake-utils.url = "github:numtide/flake-utils";
  };

  # Outputs from this flake (e.g. packages, system configurations)
  outputs =
    {
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };
        openzfs = pkgs.callPackage ./nix/default.nix {
          kernel = pkgs.linux_6_12;
        };
      in
      {
        packages.default = openzfs;
      }
    );
}
