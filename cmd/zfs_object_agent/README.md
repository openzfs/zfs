This is a prototype of the ZFS Object Storage Agent.

# Project info
 * [Scoping Overview](https://docs.google.com/document/d/12o9xypFRhvH9MFxR0n0Z1VVBCXoMtptOrxJaWrlFNww/edit)
 * [Implementation Design](https://docs.google.com/document/d/1i4a3y5hM5bUNzx0OD8iPumt1EU8XdopkITStD1WDj1g/edit#)
 * [CP-4892](https://jira.delphix.com/browse/CP-4892)

# Developent quick start

## Setup:

On laptop (optional; assumes OSX but probably works on linux too):
```sh
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
# choose option 1, default installation
source $HOME/.cargo/env #or logout and log back in
```

## To test:
On Laptop:
```sh
git commit ...
git zfs-make ...
```

On Linux:
```sh
export AWS_ACCESS_KEY_ID=...
export AWS_SECRET_ACCESS_KEY=...
cd zfs/cmd/zfs_object_agent
cargo run ...
```

## AWS credentials
Ask Matt or George for the `AWS_*` credentials

## Arguments to `cargo run`
See [main()](https://github.com/delphix/zfs-object-agent/blob/main/src/main.rs)
```bash
cargo run create
cargo run free
```
You can also execute the binary directly:
```bash
cargo build
target/debug/agent create
```
For an optimized build, use `cargo build --release`, `cargo run --release ...`, or `target/release/agent`

## IDE
It's pretty easy to set up [VS Code](https://code.visualstudio.com/download) for Rust development, using the [Official Rust plugin](https://marketplace.visualstudio.com/items?itemName=rust-lang.rust) or the (recommended, more sophisticated but beta quality) [rust-analyzer plugin](https://marketplace.visualstudio.com/items?itemName=matklad.rust-analyzer), and optinally the [Vim plugin](https://marketplace.visualstudio.com/items?itemName=vscodevim.vim).
