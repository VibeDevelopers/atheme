/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2005-2015 Atheme Project (http://atheme.org/)
 * Copyright (C) 2017-2018 Atheme Development Group (https://atheme.github.io/)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * atheme-services: A collection of minimalist IRC services
 * atheme.c: Initialization and startup of the services system
 */

#include "atheme.h"
#include "internal.h"

#ifdef HAVE_GETRLIMIT
#  include <sys/resource.h>
#endif

#include <ext/getopt_long.h>

#if !defined(HAVE_MEMSET_S) && !defined(HAVE_EXPLICIT_BZERO) && !defined(HAVE_LIBSODIUM_MEMZERO)
void *(* volatile volatile_memset)(void *, int, size_t) = &memset;
#endif /* !HAVE_MEMSET_S && !HAVE_EXPLICIT_BZERO && !HAVE_LIBSODIUM_MEMZERO */

struct ConfOption config_options;

struct chansvs chansvs;
struct nicksvs nicksvs;

mowgli_list_t taint_list = { NULL, NULL, 0 };

mowgli_eventloop_t *base_eventloop = NULL;

struct me me;
struct cnt cnt;

/* XXX */
struct claro_state claro_state;
int runflags;

char *config_file;
char *log_path;
char *datadir;
bool cold_start = false;
bool readonly = false;
bool strict_mode = true;
bool offline_mode = false;
bool permissive_mode = false;
bool database_create = false;

void (*db_save) (void *arg, enum db_save_strategy strategy) = NULL;
void (*db_load) (const char *name) = NULL;

/* *INDENT-OFF* */
static void
print_help(void)
{
	printf("usage: atheme [-dhnvr] [-c conf] [-l logfile] [-p pidfile]\n\n"
	       "-c <file>    Specify the config file\n"
	       "-d           Start in debugging mode\n"
	       "-h           Print this message and exit\n"
	       "-r           Start in read-only mode\n"
	       "-l <file>    Specify the log file\n"
	       "-n           Don't fork into the background (log screen + log file)\n"
	       "-p <file>    Specify the pid file (will be overwritten)\n"
	       "-D <dir>     Specify the data directory\n"
	       "-t           Don't run the integrated digest test suite\n"
	       "-T           Exit after running the integrated digest test suite\n"
	       "-v           Print version information and exit\n");
}
/* *INDENT-ON* */

static void
print_version(void)
{
	int i;

	printf("%s (%s), build-id %s\n", PACKAGE_NAME, PACKAGE_VERSION, revision);

	for (i = 0; infotext[i] != NULL; i++)
		printf("%s\n", infotext[i]);
}

static void
process_mowgli_log(const char *line)
{
	slog(LG_ERROR, "%s", line);
}

static inline bool
atheme_set_mowgli_allocator(void)
{
	mowgli_allocation_policy_t *const policy = mowgli_allocation_policy_create("libathemecore", &smalloc, &sfree);

	if (! policy)
	{
		(void) fprintf(stderr, "Error: mowgli_allocation_policy_create() failed!\n");
		return false;
	}

	(void) mowgli_allocator_set_policy(policy);
	return true;
}

static void
daemonize(int *daemonize_pipe)
{
#ifdef HAVE_FORK
	int i;
	char c;

	/* fork into the background */
	if (!(runflags & RF_LIVE))
	{
		if (pipe(daemonize_pipe) < 0)
		{
			slog(LG_ERROR, "can't create a pipe");
			exit(EXIT_FAILURE);
		}
		if ((i = fork()) < 0)
		{
			slog(LG_ERROR, "can't fork into the background");
			exit(EXIT_FAILURE);
		}

		/* parent */
		else if (i != 0)
		{
			close(daemonize_pipe[1]);
			if (read(daemonize_pipe[0], &c, 1) == 1)
			{
				slog(LG_INFO, "pid %d", i);
				slog(LG_INFO, "running in background mode from %s", PREFIX);
				exit(EXIT_SUCCESS);
			}
			else
				exit(EXIT_FAILURE);
		}

		close(daemonize_pipe[0]);
	}
#endif
}

static bool
detach_console(int *daemonize_pipe)
{
#ifdef HAVE_FORK
	close(0);
	if (open("/dev/null", O_RDWR) != 0)
	{
		slog(LG_ERROR, "unable to open /dev/null??");
		exit(EXIT_FAILURE);
	}
	if (setsid() < 0)
	{
		slog(LG_ERROR, "unable to create new session: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* notify parent that initialization was successful */
	if (write(daemonize_pipe[1], "", 1) != 1)
		slog(LG_ERROR, "failed to notify parent; continuing anyway");
	close(daemonize_pipe[1]);
	dup2(0, 1);
	dup2(0, 2);

	return true;
#elif defined(MOWGLI_OS_WIN)
	FreeConsole();
	return true;
#else
	return false;
#endif
}

void
atheme_bootstrap(void)
{
#ifdef HAVE_GETRLIMIT
	struct rlimit rlim;
#endif

	/* shutdown mowgli threading support */
	mowgli_thread_set_policy(MOWGLI_THREAD_POLICY_DISABLED);

	/* Prepare gettext */
#ifdef ENABLE_NLS
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE_TARNAME, LOCALEDIR);
	textdomain(PACKAGE_TARNAME);
#endif

	/* change to our local directory */
	if (chdir(PREFIX) < 0)
	{
		perror(PREFIX);
		exit(EXIT_FAILURE);
	}

#ifdef HAVE_GETRLIMIT
	/* it appears certian systems *ahem*linux*ahem*
	 * don't dump cores by default, so we do this here.
	 */
	if (!getrlimit(RLIMIT_CORE, &rlim))
	{
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}
#endif

	curr_uplink = NULL;
}

void
atheme_init(char *execname, char *log_p)
{
	me.execname = execname;
	me.kline_id = 0;
	me.start = time(NULL);
	CURRTIME = me.start;
	srand(atheme_random());

	/* set signal handlers */
	init_signal_handlers();

	/* initialize strshare */
	strshare_init();

	/* open log */
	log_path = log_p;

	log_open();
	mowgli_log_set_cb(process_mowgli_log);
}

void
atheme_setup(void)
{
	base_eventloop = mowgli_eventloop_create();
        hooks_init();
	db_init();

	translation_init();
#ifdef ENABLE_NLS
	language_init();
#endif
	init_nodes();
	init_confprocess();
	init_newconf();
	servtree_init();

	register_email_canonicalizer(canonicalize_email_case, NULL);

	modules_init();
	pcommand_init();

	authcookie_init();
	common_ctcp_init();
}

static void
db_save_periodic(void *unused)
{
	slog(LG_DEBUG, "db_save_periodic(): initiating periodic database write");
	db_save(unused, DB_SAVE_BG_REGULAR);
}

int
atheme_main(int argc, char *argv[])
{
	int daemonize_pipe[2] = { -1, -1 };
	bool run_testsuite = true;
	bool exit_after_testsuite = false;
	bool have_conf = false;
	bool have_log = false;
	bool have_datadir = false;
	char buf[32];
	int pid, r;
	FILE *pid_file;
	const char *pidfilename = RUNDIR "/atheme.pid";
	char *log_p = NULL;
	mowgli_getopt_option_t long_opts[] = {
		{ NULL, 0, NULL, 0, 0 },
	};

	if (geteuid() == (uid_t) 0)
	{
		(void) fprintf(stderr, "Error: Do not run me as root!\n");
		exit(EXIT_FAILURE);
	}

	if (! atheme_set_mowgli_allocator())
		exit(EXIT_FAILURE);

	atheme_bootstrap();

	/* do command-line options */
	while ((r = mowgli_getopt_long(argc, argv, "c:bdhrtTl:np:D:v", long_opts, NULL)) != -1)
	{
		switch (r)
		{
		  case 'b':
			  database_create = true;
			  break;
		  case 'c':
			  config_file = sstrdup(mowgli_optarg);
			  have_conf = true;
			  break;
		  case 'd':
			  log_force = true;
			  break;
		  case 'h':
			  print_help();
			  exit(EXIT_SUCCESS);
		  case 'r':
			  readonly = true;
			  break;
		  case 'l':
			  log_p = sstrdup(mowgli_optarg);
			  have_log = true;
			  break;
		  case 'n':
			  runflags |= RF_LIVE;
			  break;
		  case 't':
			  run_testsuite = false;
			  break;
		  case 'T':
			  exit_after_testsuite = true;
			  break;
		  case 'p':
			  pidfilename = mowgli_optarg;
			  break;
		  case 'D':
			  datadir = mowgli_optarg;
			  have_datadir = true;
			  break;
		  case 'v':
			  print_version();
			  exit(EXIT_SUCCESS);
		  default:
			  fprintf(stderr, "usage: atheme [-bdhnvr] [-t|-T] [-c conf] [-l logfile] [-p pidfile]\n");
			  exit(EXIT_FAILURE);
		}
	}

	if (! run_testsuite && exit_after_testsuite)
	{
		fprintf(stderr, "Error: specify exactly one of -t / -T\n");
		exit(EXIT_FAILURE);
	}

	if (!have_conf)
		config_file = sstrdup(SYSCONFDIR "/atheme.conf");

	if (!have_log)
		log_p = sstrdup(LOGDIR "/atheme.log");

	if (!have_datadir)
		datadir = sstrdup(DATADIR);

	cold_start = true;

	runflags |= RF_STARTING;

	atheme_init(argv[0], log_p);

	slog(LG_INFO, "%s is starting up...", PACKAGE_STRING);
	slog(LG_INFO, "Using Digest API frontend: %s", digest_get_frontend_info());
	slog(LG_INFO, "Using Random API frontend: %s", random_get_frontend_info());

	/* check for pid file */
#ifndef MOWGLI_OS_WIN
	if ((pid_file = fopen(pidfilename, "r")))
	{
		if (fgets(buf, 32, pid_file))
		{
			pid = atoi(buf);

			if (!kill(pid, 0))
			{
				fprintf(stderr, "atheme: daemon is already running\n");
				exit(EXIT_FAILURE);
			}
		}

		fclose(pid_file);
	}
#endif

	if (run_testsuite)
	{
		(void) slog(LG_INFO, "running digest testsuite...");

		if (! digest_testsuite_run())
		{
			(void) slog(LG_ERROR, "digest testsuite failed");
			exit(EXIT_FAILURE);
		}

		(void) slog(LG_INFO, "digest testsuite passed");

		if (exit_after_testsuite)
		{
			(void) slog(LG_INFO, "exiting due to -T");
			exit(EXIT_SUCCESS);
		}
	}
	else
		(void) slog(LG_INFO, "digest testsuite skipped due to -t");

	if (!(runflags & RF_LIVE))
		daemonize(daemonize_pipe);

	atheme_setup();

	conf_init();
	if (!conf_parse(config_file))
	{
		slog(LG_ERROR, "Error loading config file %s, aborting",
				config_file);
		exit(EXIT_FAILURE);
	}

	if (config_options.languagefile)
	{
		slog(LG_DEBUG, "Using language: %s", config_options.languagefile);
		if (!conf_parse(config_options.languagefile))
			slog(LG_INFO, "Error loading language file %s, continuing",
					config_options.languagefile);
	}

	if (!backend_loaded && authservice_loaded)
	{
		slog(LG_ERROR, "atheme: no backend modules loaded, see your configuration file.");
		exit(EXIT_FAILURE);
	}

	/* we've done the critical startup steps now */
	cold_start = false;

	/* load our db */
	if (db_load)
		db_load(NULL);
	else if (backend_loaded)
	{
		slog(LG_ERROR, "atheme: backend module does not provide db_load()!");
		exit(EXIT_FAILURE);
	}
	db_check();

	if (db_save && database_create)
	{
		db_save(NULL, DB_SAVE_BLOCKING);
		return 0;
	}

#ifdef HAVE_GETPID
	/* write pid */
	if ((pid_file = fopen(pidfilename, "w")))
	{
		fprintf(pid_file, "%d\n", getpid());
		fclose(pid_file);
	}
	else
	{
		fprintf(stderr, "atheme: unable to write pid file\n");
		exit(EXIT_FAILURE);
	}
#endif

	/* detach from terminal */
	if ((runflags & RF_LIVE) || !detach_console(daemonize_pipe))
	{
#ifdef HAVE_GETPID
		slog(LG_INFO, "pid %d", getpid());
#endif
		slog(LG_INFO, "running in foreground mode from %s", PREFIX);
	}

	/* no longer starting */
	runflags &= ~RF_STARTING;

	/* we probably have a few open already... */
	me.maxfd = 3;

	/* DB commit interval is configurable */
	if (db_save && !readonly)
		mowgli_timer_add(base_eventloop, "db_save", db_save_periodic, NULL, config_options.commit_interval);

	/* check expires every hour */
	mowgli_timer_add(base_eventloop, "expire_check", expire_check, NULL, 3600);

	/* check k/x/q line expires every minute */
	mowgli_timer_add(base_eventloop, "kline_expire", kline_expire, NULL, 60);
	mowgli_timer_add(base_eventloop, "xline_expire", xline_expire, NULL, 60);
	mowgli_timer_add(base_eventloop, "qline_expire", qline_expire, NULL, 60);

	/* check authcookie expires every ten minutes */
	mowgli_timer_add(base_eventloop, "authcookie_expire", authcookie_expire, NULL, 600);

	me.connected = false;
	uplink_connect();

	/* main loop */
	io_loop();

	/* we're shutting down */
	hook_call_shutdown();

	if (db_save && !readonly)
		db_save(NULL, DB_SAVE_BLOCKING);

	remove(pidfilename);
	errno = 0;
	if (curr_uplink != NULL && curr_uplink->conn != NULL)
		sendq_flush(curr_uplink->conn);
	connection_close_all();

	me.connected = false;

	/* should we restart? */
	if (runflags & RF_RESTART)
	{
		slog(LG_INFO, "main(): restarting");

#ifdef HAVE_EXECVE
		execv(BINDIR "/atheme-services", argv);
#endif
	}

	slog(LG_INFO, "main(): shutting down");

	mowgli_eventloop_destroy(base_eventloop);
	log_shutdown();

	return 0;
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
