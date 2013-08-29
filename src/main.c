#include "mbserver.h"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#if HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define UNUSED __attribute__((unused))

static const char * program;

static void
signal_sink (int signum)
{
    printf ("%s: Got signal %d\n", program, signum);
    exit (1);
}

int
main (int argc UNUSED, char ** argv)
{
    void *rc;
    struct server* server;
    int server_fd = -1;

    program = argv[0];

#if HAVE_SYSTEMD
    if (sd_listen_fds (true) > 0) {
        /* there should be exactly 1 fd waiting for us. */
	server = mbs_init_with_fd (SD_LISTEN_FDS_START);
    } else
#endif
        server = mbs_init (server_fd);

    if (! server)
        exit (EXIT_FAILURE);

    signal (SIGSEGV, signal_sink);
    signal (SIGBUS, signal_sink);

    rc = mbs_main (server);
    free (server);
    return (rc != NULL);
}
