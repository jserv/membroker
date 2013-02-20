#include "mbserver.h"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

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
    int rc;
    struct server * server = mbs_init ();
    if (!server)
        exit (1);

    program = argv[0];
    signal (SIGSEGV, signal_sink);
    signal (SIGBUS, signal_sink);

    rc = (int) mbs_main (server);
    free (server);
    return rc;
}
