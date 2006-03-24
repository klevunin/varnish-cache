/*
 * $Id$
 *
 * Log tailer for Varnish
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <shmlog.h>

/*
 * It would be simpler to use sparse array initialization and put it
 * directly in tagnames, but -pedantic gets in the way
 */

static struct tagnames {
	enum shmlogtag	tag;
	const char	*name;
} stagnames[] = {
#define SLTM(foo)	{ SLT_##foo, #foo },
#include "shmlog_tags.h"
#undef SLTM
	{ SLT_ENDMARKER, NULL}
};

static const char *tagnames[256];

static struct shmloghead *loghead;
static unsigned char *logstart, *logend;

int
main(int argc, char **argv)
{
	int fd;
	int i;
	struct shmloghead slh;
	unsigned char *p;

	fd = open(SHMLOG_FILENAME, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		exit (1);
	}
	i = read(fd, &slh, sizeof slh);
	if (i != sizeof slh) {
		fprintf(stderr, "Cannot read %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		exit (1);
	}
	if (slh.magic != SHMLOGHEAD_MAGIC) {
		fprintf(stderr, "Wrong magic number in file %s\n",
		    SHMLOG_FILENAME);
		exit (1);
	}

	loghead = mmap(NULL, slh.size + sizeof slh,
	    PROT_READ, MAP_HASSEMAPHORE, fd, 0);
	if (loghead == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		exit (1);
	}
	logstart = (unsigned char *)loghead + loghead->start;
	logend = logstart + loghead->size;

	for (i = 0; stagnames[i].tag != SLT_ENDMARKER; i++)
		tagnames[stagnames[i].tag] = stagnames[i].name;

	while (1) {
		p = logstart;
		while (1) {
			if (*p == SLT_WRAPMARKER)
				break;
			while (*p == SLT_ENDMARKER) 
				sleep(1);
			printf("%02x %02d %02x%02x %-12s <",
			    p[0], p[1], p[2], p[3],
			    tagnames[p[0]]);
			if (p[1] > 0)
				fwrite(p + 4, p[1], 1, stdout);
			printf(">\n");
			p += p[1] + 4;
		}
	}
}


#if 0
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>

#include <event.h>
#include <sbuf.h>

#include <cli.h>
#include <cli_priv.h>
#include <libvarnish.h>

#include "mgt.h"
#include "heritage.h"
#include "cli_event.h"

/*--------------------------------------------------------------------*/

struct heritage heritage;
struct event_base *eb;

/*--------------------------------------------------------------------
 * Generic passthrough for CLI functions
 */

void
cli_passthrough_cb(unsigned u, const char *r, void *priv)
{
	struct cli *cli = priv;

	cli_out(cli, "%s\n", r);
	cli_result(cli, u);
	cli_resume(cli);
}

static void
cli_func_passthrough(struct cli *cli, char **av __unused, void *priv)
{

	cli_suspend(cli);
	mgt_child_request(cli_passthrough_cb, cli, &av[2], av[1]);
}

/*--------------------------------------------------------------------*/

static void
cli_func_server_start(struct cli *cli, char **av __unused, void *priv __unused)
{

	mgt_child_start();
}

/*--------------------------------------------------------------------*/

static void
cli_func_server_stop(struct cli *cli, char **av __unused, void *priv __unused)
{

	mgt_child_stop();
}

/*--------------------------------------------------------------------*/

static void
cli_func_verbose(struct cli *cli, char **av __unused, void *priv)
{

	cli->verbose = !cli->verbose;
}


static void
cli_func_ping(struct cli *cli, char **av, void *priv __unused)
{
	time_t t;

	if (av[2] != NULL) {
		cli_out(cli, "Got your %s\n", av[2]);
	} 
	time(&t);
	cli_out(cli, "PONG %ld\n", t);
}

/*--------------------------------------------------------------------*/

static struct cli_proto cli_proto[] = {
	/* URL manipulation */
	{ CLI_URL_QUERY,	cli_func_passthrough, NULL },
	{ CLI_URL_PURGE,	cli_func_passthrough, NULL },
	{ CLI_URL_STATUS,	cli_func_passthrough, NULL },
	{ CLI_CONFIG_LOAD },
	{ CLI_CONFIG_INLINE },
	{ CLI_CONFIG_UNLOAD },
	{ CLI_CONFIG_LIST },
	{ CLI_CONFIG_USE },
	{ CLI_SERVER_FREEZE,	cli_func_passthrough, NULL },
	{ CLI_SERVER_THAW,	cli_func_passthrough, NULL },
	{ CLI_SERVER_SUSPEND,	cli_func_passthrough, NULL },
	{ CLI_SERVER_RESUME,	cli_func_passthrough, NULL },
	{ CLI_SERVER_STOP,	cli_func_server_stop, NULL },
	{ CLI_SERVER_START,	cli_func_server_start, NULL },
	{ CLI_SERVER_RESTART },
	{ CLI_PING,		cli_func_ping, NULL },
	{ CLI_STATS },
	{ CLI_ZERO },
	{ CLI_HELP,		cli_func_help, cli_proto },
	{ CLI_VERBOSE,		cli_func_verbose, NULL },
	{ CLI_EXIT },
	{ CLI_QUIT },
	{ CLI_BYE },
	{ NULL }
};

static void
testme(void)
{
	struct event e_sigchld;
	struct cli *cli;
	int i;

	eb = event_init();
	assert(eb != NULL);

	cli = cli_setup(0, 1, 1, cli_proto);

	signal_set(&e_sigchld, SIGCHLD, mgt_sigchld, NULL);
	signal_add(&e_sigchld, NULL);

	i = event_dispatch();
	if (i != 0)
		printf("event_dispatch() = %d\n", i);

}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishd [options]\n");
	fprintf(stderr, "    %-20s # %s\n", "-d", "debug");
	fprintf(stderr, "    %-20s # %s\n", "-p number", "TCP listen port");
#if 0
	-c clusterid@cluster_controller
	-f config_file
	-m memory_limit
	-s kind[,storage-options]
	-l logfile,logsize
	-b backend ip...
	-u uid
	-a CLI_port
#endif
	exit(1);
}

/*--------------------------------------------------------------------*/

#include "shmlog.h"

static void
init_vsl(const char *fn, unsigned size)
{
	struct shmloghead slh;
	int i;

	heritage.vsl_fd = open(fn, O_RDWR | O_CREAT, 0600);
	if (heritage.vsl_fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}
	i = read(heritage.vsl_fd, &slh, sizeof slh);
	if (i == sizeof slh && slh.magic == SHMLOGHEAD_MAGIC) {
		/* XXX more checks */
		heritage.vsl_size = slh.size + slh.start;
		return;
	}
	slh.magic = SHMLOGHEAD_MAGIC;
	slh.size = size;
	slh.ptr = 0;
	slh.start = sizeof slh;
	AZ(lseek(heritage.vsl_fd, 0, SEEK_SET));
	i = write(heritage.vsl_fd, &slh, sizeof slh);
	assert(i == sizeof slh);
	AZ(ftruncate(heritage.vsl_fd, sizeof slh + size));
	heritage.vsl_size = slh.size + slh.start;
}

/*--------------------------------------------------------------------*/

/* for development purposes */
#include <printf.h>

int
main(int argc, char *argv[])
{
	int o;
	const char *portnumber = "8080";
	unsigned dflag = 1;	/* XXX: debug=on for now */

	register_printf_render_std((const unsigned char *)"HVQ");

	while ((o = getopt(argc, argv, "dp:")) != -1)
		switch (o) {
		case 'd':
			dflag++;
			break;
		case 'p':
			portnumber = optarg;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	/*
	 * XXX: Lacking the suspend/resume facility (due to the socket API
	 * missing an unlisten(2) facility) we may want to push this into
	 * the child to limit the amount of time where the socket(s) exists
	 * but do not answer.  That, on the other hand, would eliminate the
	 * possibility of doing a "no-glitch" restart of the child process.
	 */
	open_tcp(portnumber);

	init_vsl(SHMLOG_FILENAME, 1024*1024);

	testme();


	exit(0);
}
#endif
