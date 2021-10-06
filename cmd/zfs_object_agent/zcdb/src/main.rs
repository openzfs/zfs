use clap::AppSettings;
use clap::Arg;
use clap::SubCommand;
use zettacache::DumpStructuresOptions;
use zettacache::ZettaCacheDBCommand;

#[tokio::main]
async fn main() {
    // When zcachedb is used in UNIX shell pipeline and its output is not fully
    // consumed a SIGPIPE (e.g. "broken pipe") signal is sent to us. By default,
    // we would abort and generate a core dump which is annoying. The unsafe
    // line below changes that behavior to just terminating as it is expected by
    // other UNIX utilities.
    // reference: https://github.com/rust-lang/rust/issues/46016
    unsafe {
        libc::signal(libc::SIGPIPE, libc::SIG_DFL);
    }

    let matches = clap::App::new("zcachedb")
        .setting(AppSettings::SubcommandRequiredElseHelp)
        .about("ZFS ZettaCache Debugger")
        .version("1.0")
        .arg(
            Arg::with_name("device")
                .help("ZettaCache Device")
                .required(true),
        )
        .subcommand(
            SubCommand::with_name("dump_structures")
                .about("print out on-disk structures")
                .arg(
                    Arg::with_name("nodefaults")
                        .long("nodefaults")
                        .short("n")
                        .help("skip all structures that are printed by default"),
                )
                .arg(
                    Arg::with_name("spacemaps")
                        .long("spacemaps")
                        .short("s")
                        .help("dump block allocator spacemaps"),
                ),
        )
        .get_matches();

    let device = matches.value_of("device").unwrap();
    match matches.subcommand() {
        ("dump_structures", Some(subcommand_matches)) => {
            ZettaCacheDBCommand::issue_command(
                ZettaCacheDBCommand::DumpStructures(
                    DumpStructuresOptions::default()
                        .defaults(!subcommand_matches.is_present("nodefaults"))
                        .spacemaps(subcommand_matches.is_present("spacemaps")),
                ),
                device,
            )
            .await;
        }
        _ => {
            matches.usage();
        }
    };
}
