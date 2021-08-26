use std::env;
use std::path::PathBuf;
fn main() {
    let clang_args = {
        let capture_data = subprocess::Exec::cmd("make")
            .arg("print_clang_args_for_bindgen")
            .capture()
            .expect("exec make to get DEFAULT_INCLUDES");
        assert_eq!(capture_data.exit_status, subprocess::ExitStatus::Exited(0));
        let stdout = capture_data.stdout_str();
        println!("debug: makefile stdout: {:?}", stdout);
        let magic = "CLANG_ARGS_FOR_BINDGEN";
        let line = stdout
            .lines()
            .find(|l| l.starts_with(magic))
            .expect(&format!("did not find magic line ({:?})", magic));
        // FIXME: whitespace handling -,-
        let clang_args = line
            .split_whitespace()
            .skip(1) // magic
            .map(|s| s.to_owned())
            .collect::<Vec<_>>();
        clang_args
    };

    println!("clang args: {:?}", clang_args);

    // Tell cargo to invalidate the built crate whenever the wrapper changes
    println!("cargo:rerun-if-changed=wrapper.h");
    let bindings = bindgen::Builder::default()
        .header("wrapper.h")
        .clang_args(&clang_args)
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // the parts of the libzpool API that we want to expose
        .whitelist_function(".*zfs.*")
        .whitelist_function("zfs_btree.*")
        .whitelist_function("kernel_init")
        .whitelist_function("zilpmem.*")
        .whitelist_function("prb_.*")
        .whitelist_type("prb_.*")
        .whitelist_function("fletcher.*")
        .whitelist_function("dprintf_setup")
        .whitelist_type("spa_mode_t")
        .whitelist_function("libspl_set_alternative_abort_handler")
        .whitelist_function("zil.*")
        .whitelist_type("zil_header.*")
        .whitelist_type("entry_header.*")
        .generate()
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
