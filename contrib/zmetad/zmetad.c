// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * CDDL HEADER END
 */

/*
 * zmetad - ZFS Metadata Export Daemon
 *
 * Polls ZFS datasets for file events and exports them to a SQLite database
 * for efficient querying, retention management, and external tool integration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <libnvpair.h>

#include "zmetad.h"
#include "zmetad_schema.h"

/* Global state */
static libzfs_handle_t *g_zfs;
static zmetad_config_t g_config;
static zmetad_schema_t *g_schema;
static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_reload = 0;

static void
signal_handler(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		g_shutdown = 1;
		break;
	case SIGHUP:
		g_reload = 1;
		break;
	}
}

static void
setup_signals(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	/* Ignore SIGPIPE */
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
}

static void
config_init(zmetad_config_t *cfg)
{
	memset(cfg, 0, sizeof (*cfg));
	cfg->poll_interval = ZMETAD_DEFAULT_POLL_INTERVAL;
	cfg->retention_days = ZMETAD_DEFAULT_RETENTION_DAYS;
	cfg->max_size_mb = ZMETAD_DEFAULT_MAX_SIZE_MB;
	strlcpy(cfg->db_path, ZMETAD_DEFAULT_DB_PATH, sizeof (cfg->db_path));
	cfg->schema_path[0] = '\0';
	cfg->foreground = B_FALSE;
	cfg->verbose = 0;
}

static int
collect_dataset_events(const char *dataset, zmetad_db_t *db)
{
	nvlist_t *events = NULL;
	uint64_t last_offset;
	int err;

	/* Get last synced offset for this dataset */
	last_offset = zmetad_db_get_last_offset(db, dataset);

	/* Fetch events since last offset */
	err = lzc_get_events(dataset, 0, last_offset, &events);
	if (err != 0) {
		if (err == ENOENT) {
			/* Dataset doesn't exist or events not enabled */
			return (0);
		}
		fprintf(stderr, "Failed to get events for %s: %s\n",
		    dataset, strerror(err));
		return (err);
	}

	if (events == NULL) {
		return (0);
	}

	/*
	 * Negotiate the record schema version.  Kernels that predate
	 * schema version exposure omit the key; wire == 0 is treated as
	 * compatible with any loaded schema.
	 */
	uint64_t wire_version = 0;
	(void) nvlist_lookup_uint64(events, "schema_version", &wire_version);
	if (zmetad_schema_check_version(g_schema, wire_version) != 0) {
		fprintf(stderr, "schema version mismatch: daemon=%llu "
		    "wire=%llu\n",
		    (unsigned long long)zmetad_schema_version(g_schema),
		    (unsigned long long)wire_version);
		nvlist_free(events);
		return (EINVAL);
	}

	/*
	 * The reply nests the records under "events": each child nvlist
	 * is one event record. Iterate that child, not the outer reply
	 * (whose only nvlist-valued pair would otherwise be mistaken
	 * for a single event and yield empty rows).
	 */
	nvlist_t *records = NULL;
	if (nvlist_lookup_nvlist(events, "events", &records) != 0)
		records = NULL;

	uint_t count = 0;
	nvpair_t *elem = NULL;
	nvlist_t *event;

	while (records != NULL &&
	    (elem = nvlist_next_nvpair(records, elem)) != NULL) {
		if (nvpair_type(elem) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(elem, &event) != 0)
			continue;

		err = zmetad_db_insert_event(db, dataset, event);
		if (err != 0) {
			fprintf(stderr, "Failed to insert event: %s\n",
			    strerror(err));
		} else {
			count++;
		}
	}

	if (count > 0 && g_config.verbose) {
		printf("Collected %u events from %s\n", count, dataset);
	}

	/* Update last synced offset from returned next_offset */
	uint64_t next_offset = 0;
	(void) nvlist_lookup_uint64(events, "next_offset", &next_offset);

	if (next_offset > 0) {
		zmetad_db_set_last_offset(db, dataset, next_offset);
	}

	nvlist_free(events);
	return (0);
}

static int
collect_callback(zfs_handle_t *zhp, void *arg)
{
	zmetad_db_t *db = arg;
	const char *name = zfs_get_name(zhp);
	uint64_t events_enabled;

	/* Check if events are enabled on this dataset */
	events_enabled = zfs_prop_get_int(zhp, ZFS_PROP_EVENTS);
	if (events_enabled) {
		collect_dataset_events(name, db);
	}

	/* Recurse into children */
	zfs_iter_filesystems_v2(zhp, 0, collect_callback, arg);

	zfs_close(zhp);
	return (0);
}

static void
collect_all_events(zmetad_db_t *db)
{
	/* Iterate all pools and datasets */
	zfs_iter_root(g_zfs, collect_callback, db);
}

static void
daemon_loop(zmetad_db_t *db)
{
	time_t last_collect = 0;
	time_t last_cleanup = 0;

	while (!g_shutdown) {
		time_t now = time(NULL);

		/* Reload config on SIGHUP */
		if (g_reload) {
			g_reload = 0;
			/* TODO: reload config file */
			if (g_config.verbose) {
				printf("Configuration reloaded\n");
			}
		}

		/* Collect events at poll interval */
		if (now - last_collect >= g_config.poll_interval) {
			collect_all_events(db);
			last_collect = now;
		}

		/* Cleanup old events daily */
		if (now - last_cleanup >= 86400) {
			if (g_config.retention_days > 0) {
				zmetad_db_cleanup(db, g_config.retention_days);
			}
			last_cleanup = now;
		}

		/* Sleep for a bit */
		sleep(1);
	}
}

static void
daemonize(void)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		/* Parent exits */
		exit(EXIT_SUCCESS);
	}

	/* Create new session */
	if (setsid() < 0) {
		perror("setsid");
		exit(EXIT_FAILURE);
	}

	/* Fork again to prevent acquiring a controlling terminal */
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Set working directory */
	if (chdir("/") < 0) {
		perror("chdir");
		exit(EXIT_FAILURE);
	}

	/* Close standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	/* Redirect to /dev/null */
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_WRONLY);
}

static void
usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [options]\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -c, --config <file>    Config file path\n");
	fprintf(stderr, "  -d, --database <path>  SQLite database path\n");
	fprintf(stderr, "  -f, --foreground       Run in foreground\n");
	fprintf(stderr, "  -i, --interval <sec>   Poll interval "
	    "(default: %d)\n", ZMETAD_DEFAULT_POLL_INTERVAL);
	fprintf(stderr, "  -r, --retention <days> Retention days "
	    "(default: %d)\n", ZMETAD_DEFAULT_RETENTION_DAYS);
	fprintf(stderr, "  --schema <file>        Event schema JSON path "
	    "(default: embedded)\n");
	fprintf(stderr, "  -v, --verbose          Verbose output\n");
	fprintf(stderr, "  -h, --help             Show this help\n");
}

static struct option longopts[] = {
	{ "config",	required_argument,	NULL,	'c' },
	{ "database",	required_argument,	NULL,	'd' },
	{ "foreground",	no_argument,		NULL,	'f' },
	{ "interval",	required_argument,	NULL,	'i' },
	{ "retention",	required_argument,	NULL,	'r' },
	{ "schema",	required_argument,	NULL,	's' },
	{ "verbose",	no_argument,		NULL,	'v' },
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0 }
};

int
main(int argc, char **argv)
{
	zmetad_db_t *db = NULL;
	int opt;
	int err;

	config_init(&g_config);

	while ((opt = getopt_long(argc, argv, "c:d:fi:r:s:vh", longopts,
	    NULL)) != -1) {
		switch (opt) {
		case 'c':
			/* TODO: load config file */
			break;
		case 'd':
			strlcpy(g_config.db_path, optarg,
			    sizeof (g_config.db_path));
			break;
		case 'f':
			g_config.foreground = B_TRUE;
			break;
		case 'i':
			g_config.poll_interval = atoi(optarg);
			if (g_config.poll_interval < 1) {
				g_config.poll_interval = 1;
			}
			break;
		case 'r':
			g_config.retention_days = atoi(optarg);
			break;
		case 's':
			strlcpy(g_config.schema_path, optarg,
			    sizeof (g_config.schema_path));
			break;
		case 'v':
			g_config.verbose++;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return (opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE);
		}
	}

	/* Initialize libzfs */
	g_zfs = libzfs_init();
	if (g_zfs == NULL) {
		fprintf(stderr, "Failed to initialize libzfs\n");
		return (EXIT_FAILURE);
	}

	/* Load the event schema before opening the database */
	{
		char errbuf[256];
		const char *path = (g_config.schema_path[0] != '\0') ?
		    g_config.schema_path : NULL;

		g_schema = zmetad_schema_load(path, errbuf);
		if (g_schema == NULL) {
			fprintf(stderr, "Failed to load event schema: %s\n",
			    errbuf);
			libzfs_fini(g_zfs);
			return (EXIT_FAILURE);
		}
	}

	/* Open/create database */
	err = zmetad_db_open(&db, g_config.db_path, g_schema);
	if (err != 0) {
		fprintf(stderr, "Failed to open database: %s\n",
		    strerror(err));
		libzfs_fini(g_zfs);
		return (EXIT_FAILURE);
	}

	/* Setup signal handlers */
	setup_signals();

	/* Daemonize unless foreground mode */
	if (!g_config.foreground) {
		daemonize();
	}

	if (g_config.verbose) {
		printf("zmetad starting (poll=%ds, retention=%dd, db=%s)\n",
		    g_config.poll_interval, g_config.retention_days,
		    g_config.db_path);
	}

	/* Main loop */
	daemon_loop(db);

	if (g_config.verbose) {
		printf("zmetad shutting down\n");
	}

	/* Cleanup */
	zmetad_db_close(db);
	zmetad_schema_free(g_schema);
	libzfs_fini(g_zfs);

	return (EXIT_SUCCESS);
}
