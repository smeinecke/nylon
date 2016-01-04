/*
 * nylon.c
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: nylon.c,v 1.18 2003/06/08 05:56:47 marius Exp $
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <event.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "access.h"
#include "cfg.h"
#include "cleanup.h"
#include "misc.h"
#include "nylon.h"
#include "net.h"
#include "print.h"

#define CONF_SAVE(w, f)        \
            do {               \
                char *p = f;   \
                if (p != NULL) \
                    (w) = p;   \
            } while (0)

void usage(void);

struct event sigchldev, sighupev, sigtermev, sigintev;

char  *conf_path;		/* Used by cfg.c  */
char **xargv;
int    xargc;
int    noresolve;
int    verbose_dump;

#ifdef HAVE___PROGNAME
extern char *__progname;
#else
char *__progname;
#endif

cleanup_t *cleanup, *cleanup_exit;

void        sigchld_cb(int, short, void *);
void        sighup_cb(int, short, void *);
void        gensig_cb(int, short, void *);
void        signal_setup(void);
static void unlink_pidfile_cb(void *);

int
main(int argc, char **argv)
{
	int opt, foreground, verbose, use_syslog, support;
	static int servsock;
	char *bind_ifip, *connect_ifip, *pidfilenam, *allow_hosts, *deny_hosts,
	    *mirror_addr, *bind_port;
	struct stat sb;

	__progname = get_progname(argv[0]);

	xargv = argv;
	xargc = argc;

	/* Defaults */
	support = NET_SUPPORT_SOCKS4 | NET_SUPPORT_SOCKS5;
	conf_path = SYSCONFDIR "/nylon.conf";
	use_syslog = noresolve = verbose = verbose_dump = foreground = 0;
	pidfilenam = "/var/run/nylon.pid";
	bind_port = mirror_addr = connect_ifip = bind_ifip = NULL;
	allow_hosts = "127.0.0.1";
	deny_hosts = "";

#define GETOPT_STR "hvVfsn45p:i:I:P:c:m:a:d:"
	while ((opt = getopt(argc, argv, GETOPT_STR)) != -1)
		if (opt == 'c')
			conf_path = optarg;
	optind = 1;

	if (stat(conf_path, &sb) == -1 && errno == ENOENT) {
		warnv(1, "Skipping configuration file: %s", conf_path);
	} else {
		/* Read from configuration file */
		conf_init();
		CONF_SAVE(bind_ifip, conf_get_str("Server", "Binding-Interface"));
		CONF_SAVE(bind_port, conf_get_str("Server", "Port"));
		CONF_SAVE(connect_ifip, conf_get_str("Server", "Connecting-Interface"));
		CONF_SAVE(allow_hosts, conf_get_str("Server", "Allow-IP"));
		CONF_SAVE(deny_hosts, conf_get_str("Server", "Deny-IP"));
		CONF_SAVE(mirror_addr, conf_get_str("Server", "Mirror-Address"));
		CONF_SAVE(pidfilenam, conf_get_str("General", "PIDFile"));
		verbose = conf_get_num("General", "Verbose", 0);
		use_syslog = conf_get_num("General", "Syslog", 0);
	}

	while ((opt = getopt(argc, argv, GETOPT_STR)) != -1)
 		switch (opt) {
		case 'c':
			break;
		case 'h':
			usage();
		case 'v':
			verbose++;
			break;
		case 'V':
			warnxv(0, PACKAGE " version " VERSION);
			exit(0);
		case 'f':
			foreground = 1;
			break;
		case 'a':
			allow_hosts = optarg;
			break;
		case 'd':
			deny_hosts = optarg;
			break;
		case 's':
 			use_syslog = 1;
			break;
		case 'n':
			noresolve = 1;
			break;
		case 'm':
			mirror_addr = optarg;
			break;
		case 'p':
			bind_port = optarg;
			break;
		case 'i':
			bind_ifip = optarg;
			break;
		case 'I':
			connect_ifip = optarg;
			break;
		case 'P':
			pidfilenam = optarg;
			break;
		case '4':
			CLR(support, NET_SUPPORT_SOCKS4);
			break;
		case '5':
			CLR(support, NET_SUPPORT_SOCKS5);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
#undef GETOPT_STR

	if (bind_port == NULL && mirror_addr == NULL)
		bind_port = "1080";

	if (!foreground) {
		/*
		 * We retain curdir here, so that SIGHUP works
		 * properly (exec and path), of course, xargv could
		 * simply be resolved to contain the absolute path of
		 * the image.
		 */
		if (daemon(1, 0) == -1)
			errv(0, 1, "daemon()");
		use_syslog = 1;
	}
	event_init();

	if ((cleanup = cleanup_new()) == NULL)
		errxv(0, 1, "Failed setting up cleanup functionality");
	print_setup(verbose, use_syslog);
	servsock = net_setup(bind_ifip, connect_ifip, bind_port, mirror_addr,
	    NULL, support);
	access_setup(allow_hosts, deny_hosts);
	signal_setup();

	signal_set(&sighupev, SIGHUP, sighup_cb, &servsock);
	if (signal_add(&sighupev, NULL) == -1)
		errv(0, 1, "signal_add()");
	signal_set(&sigchldev, SIGCHLD, sigchld_cb, NULL);
	if (signal_add(&sigchldev, NULL) == -1)
		errv(0, 1, "signal_add()");

	/* By now, we might have a new PID, so we store our pidfile */
	if (stat(pidfilenam, &sb) != -1 && errno == ENOENT) {
		warnxv(1, "PIDfile %s already exists, skipping", pidfilenam);
	} else {
		FILE *pidf;

		if ((pidf = fopen(pidfilenam, "w")) != NULL) {
			fprintf(pidf, "%d\n", getpid());
			fclose(pidf);
			cleanup_add(cleanup, unlink_pidfile_cb, pidfilenam);
		} else {
			warnv(1, "Failed creating PIDfile %s", pidfilenam);
		}
	}

	event_dispatch();

	cleanup_cleanup(cleanup);
	return (1);
}

void
signal_setup(void)
{

	signal_set(&sigtermev, SIGTERM, gensig_cb, NULL);
	if (signal_add(&sigtermev, NULL) == -1)
		errv(0, 1, "signal_add()");
	signal_set(&sigintev, SIGINT, gensig_cb, NULL);
	if (signal_add(&sigintev, NULL) == -1)
		errv(0, 1, "signal_add()");
	
}

void
gensig_cb(int sig, short ev, void *data)
{
	char *sigstr;

	switch (sig) {
	case SIGTERM:
		sigstr = "SIGTERM";
		break;
	case SIGINT:
		sigstr = "SIGINT";
		break;
	default:
		sigstr = "an unknown signal";
		break;
	}

	cleanup_cleanup(cleanup);
	errxv(0, 0, "Received %s; quitting", sigstr);
}

void
sighup_cb(int sig, short ev, void *data)
{
	int fd = *(int *)data;

	/* Restart and re-read configuration */
	warnxv(0, "Received SIGHUP; restarting");
	/* XXX cleanup */
	close(fd);
	execv(xargv[0], xargv);
	errv(0, 1, "Restart FAILED");
}

void
sigchld_cb(int sig, short ev, void *data)
{
	int status;
	pid_t pid;

	/* The Grim Children Reaper */
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0 ||
	    (pid < 0 && errno == EINTR));
}

static void
unlink_pidfile_cb(void *handler)
{
	char *pidfilenam = handler;

	unlink(pidfilenam);
}

void
usage(void)
{
	fprintf(stderr,
	    "Usage: %s [-hvVfds] [-p <port>] [-i <if/ip>] [-I <if/ip>] "
	    "[-P <file>] [-m <addr>] [-c <file>]\n"
	    "\t-h         Help (this)\n"
	    "\t-v         Increase verbosity level\n"
	    "\t-V         Print %s version\n"
	    "\t-f         Run %s in the foreground\n"
	    "\t-s         Use syslog instead of stderr to print messages\n"
	    "\t-n         Do not resolve IP addresses\n"
	    "\t-a <list>  Set IP allow list to <list>\n"
	    "\t-d <list>  Set IP deny list to <list>\n"
	    "\t-m <addr>  Mirror address/port pair <addr> in the format \"address:port\"\n"
	    "\t-p <port>  Bind to <port> instead of the default 1080\n"
	    "\t-i <if/ip> Bind to interface or IP address <if/ip>\n"
	    "\t-I <if/ip> Make outgoing connections on interface or IP address <if/ip>\n"
	    "\t-P <file>  Use PID file <file>\n"
	    "\t-c <file>  Use configuration file <file>\n",
	    __progname, __progname, __progname);

	exit(1);
}
