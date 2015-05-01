/* vim: set expandtab softtabstop=4 shiftwidth=4 : */
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/param.h>
#include "mbserver.h"

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

struct option options[] = {
    { "help", 0, NULL, 'h' },
    { "memsize", required_argument, NULL, 'm' },
    { "all-except", required_argument, NULL, 'x' },
    { NULL, 0, NULL, 0 }
};

static void
help (void)
{
    printf ("usage: %s [options]\n", program);
    printf ("    --help               show this message\n");
    printf ("    --memsize AMOUNT     server owns this much memory\n");
    printf ("    --all-except AMOUNT  use MemTotal minus this much\n");
    printf ("\n");
    printf ("    AMOUNT is a positive number with a modifier:\n");
    printf ("       p     pages\n");
    printf ("       M     megabytes\n");
    printf ("       G     gigabytes\n");
    exit (0);
}

char *
make_optstring (void)
{
    unsigned int n_options = sizeof (options) / sizeof (options[0]);
    unsigned int i;
    char * optstring = malloc (3 * n_options + 1); /* max 3 per option */
    char * p = optstring;
    for (i = 0 ; i < n_options ; i++) {
        if (! options[i].name)
            continue;
        *p++ = options[i].val;
        if (options[i].has_arg)
            *p++ = ':'; /* colon for having an argument */
        if (optional_argument == options[i].has_arg)
            *p++ = ':'; /* two colons for optional arguments */
    }
    *p = '\0';

    return optstring;
}

static int
parse_memsize (const char * arg)
{
    char *endptr;
    long num;
    int multiplier;

    errno = 0;
    num = strtol (arg, &endptr, 10);

    if (errno) {
        if (errno == ERANGE) {
            fprintf (stderr, "%s: %s is out of range\n", program, arg);
        } else {
            fprintf (stderr, "%s: %s\n", program, strerror (errno));
        }
        return -1;
    }
    if (endptr == arg) {
        fprintf (stderr, "%s: %s is not a number\n", program, arg);
        return -1;
    }

    switch (*endptr) {
    case '\0':
        fprintf (stderr, "%s: '%s' has no unit modifier\n", program, arg);
        return -1;

    case 'G':
        multiplier = (1024 * 1024 * 1024) / EXEC_PAGESIZE;
        break;
    case 'M':
        multiplier = (1024 * 1024) / EXEC_PAGESIZE;
        break;
    case 'p':
        multiplier = 1;
        break;

    default:
        fprintf (stderr, "%s: bad memory size modifier '%s'\n",
                 program, endptr);
        return -1;
    }
    if (endptr[1] != '\0') {
        fprintf (stderr, "%s: bad memory size modifier '%s'\n",
                 program, endptr);
        return -1;
    }

    if (num < 0) {
        fprintf (stderr, "%s: memory size must be positive\n", program);
        return -1;
    }

    return (int) (num * multiplier);
}

static unsigned long
get_kernel_mem_total (void)
{
    char buf[1024];
    char *valp;
    char *endptr;
    FILE *fp;
    int cnt;
    unsigned long kmem_kb;

    fp = fopen ("/proc/meminfo", "r");
    if (NULL == fp)
        goto out_error;
    cnt = fread (buf, 1, sizeof (buf) - 1, fp);
    buf[cnt] = '\0';
    fclose (fp);

    /* We're looking for a line that should have the form
     *
     *      MemTotal:      1234567 kB
     */

    valp = strstr (buf, "MemTotal: ");
    if (NULL == valp)
        goto out_error;

    /* Skip past "MemTotal: " */
    valp += strlen ("MemTotal: ");

    /* Turn newline into end of string, if found */
    endptr = strchr (valp, '\n');
    if (endptr)
        *endptr = '\0';

    /* Now convert. */
    endptr = NULL;
    errno = 0;
    kmem_kb = strtoul (valp, &endptr, 0);

    /* Lots of error checking... */
    if (0 == kmem_kb && 0 != errno)
        goto out_error;
    if (endptr == valp)
        goto out_error;
    while (*endptr && isspace (*endptr))
        endptr++;
    if (0 != strcmp (endptr, "kB")) {
        printf ("Unexpected units in MemTotal: %s\n", endptr);
        goto out_error;
    }

    return kmem_kb;

out_error:
    fprintf (stderr, "Cannot read MemTotal from /proc/meminfo\n");
    return (unsigned long) -1;
}

static double
pages_to_gb (int pages)
{
    return pages * (EXEC_PAGESIZE / 1024.0 / 1024.0 / 1024.0);
}

static int
calc_all_pages_except (const char * arg)
{
    unsigned long kmem_kb;
    int kmem_pages;
    int except_pages;

    except_pages = parse_memsize (arg);
    if (except_pages < 0)
        return -1;

    kmem_kb = get_kernel_mem_total ();
    if (kmem_kb == (unsigned long) -1)
        return -1;

    kmem_pages = (int) (((long long) kmem_kb) * 1024 / EXEC_PAGESIZE);

    printf ("MemTotal: %lu kB -> %d p  -> %.3f G\n", kmem_kb, kmem_pages,
            pages_to_gb (kmem_pages));
    printf ("Except pages: %s -> %d p  -> %.3f G\n", arg, except_pages,
            pages_to_gb (except_pages));
    printf ("Result: %d p -> %.3f G\n", kmem_pages - except_pages,
            pages_to_gb (kmem_pages - except_pages));

    return kmem_pages - except_pages;
}


int main(int argc, char ** argv)
{
    char * optstring;
    int c;
    void *rc;
    struct server* server;
    int server_fd = -1;
    int init_pages = -1;

    setlinebuf(stdout);

    program = argv[0];

    optstring = make_optstring ();
    while (-1 != (c = getopt_long (argc, argv, optstring, options, NULL))) {
        switch (c) {
        case 'h':
            help ();
            free (optstring);
            return EXIT_SUCCESS;

        case 'm':
            init_pages = parse_memsize (optarg);
            if (init_pages < 0) {
                free (optstring);
                return EXIT_FAILURE;
            }
            break;

        case 'x':
            init_pages = calc_all_pages_except (optarg);
            if (init_pages < 0) {
                free (optstring);
                return EXIT_FAILURE;
            }
            break;

        default:
            fprintf (stderr, "%s: unknown option %s\n", program, optarg);
            break;
        }
    }
    free (optstring);

#if HAVE_SYSTEMD
    if (sd_listen_fds (true) > 0) {
        /* there should be exactly 1 fd waiting for us. */
	server = mbs_init_with_fd (SD_LISTEN_FDS_START);
    } else
#endif
        server = mbs_init (server_fd);

    if (! server)
        exit (EXIT_FAILURE);

    if (init_pages == -1)
        printf ("Initialized membroker server with no pages.  "
                "(A client must provide pages)\n");
    else
        mbs_set_pages (server, init_pages);

    signal(SIGSEGV, signal_sink);
    signal(SIGBUS, signal_sink);

    rc = mbs_main (server);
    free (server);
    return (rc != NULL);
}
