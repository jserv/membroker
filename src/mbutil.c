#include "mbclient.h"
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

static const char * progname;

static inline double
pages_to_megabytes (int pages)
{
	return (((long long) pages) * EXEC_PAGESIZE) / (1024.0 * 1024.0);
}

static const char *
mb_error_to_string (MbError error)
{
	if (error & MB_BAD_PAGES)
		error &= ~MB_BAD_PAGES;

	switch (error) {
	case MB_SUCCESS:		return "Success";
	case MB_OUT_OF_MEMORY:		return "Out of memory";
	case MB_BAD_CLIENT_TYPE:	return "Bad client type";
	case MB_IO:			return "I/O error";
	case MB_BAD_ID:			return "Bad ID";
	case MB_BAD_CODE:		return "Bad command code";
	case MB_BAD_PARAM:		return "Bad parameter";
	//case MB_LAST_ERROR_CODE = MB_BAD_PARAM,
	case MB_BAD_PAGES:		return "Bad pages";  /* how? */
	}

	return "[unknown]";
}

static void
do_help (FILE * out)
{
	fprintf (out, "%s - interact with membroker\n", progname);
	fprintf (out, "Usage:  %s command [amount]\n", progname);
	fprintf (out, "   reserve AMOUNT   get and hold all-or-nothing\n");
	fprintf (out, "   request AMOUNT   get and hold as much as AMOUNT\n");
	fprintf (out, "   query            print available and exit\n");
	fprintf (out, "   help             this message\n");
	fprintf (out, "\n");
	fprintf (out, "   AMOUNT is a number, optionally followed by units\n");
	fprintf (out, "       G     gigabytes\n");
	fprintf (out, "       M     megabytes\n");
	fprintf (out, "       K     kilabytes\n");
	fprintf (out, "       p     pages (default)\n");
	fprintf (out, "       %%     percentage of total memory (0 to 100)\n");
	fprintf (out, "\n");
}

static void error (const char * format, ...)
	__attribute__ ((__format__ (__printf__, 1, 2)))
	__attribute__ ((__noreturn__));

static void
error (const char * format,
       ...)
{
	va_list ap;
	fprintf (stderr, "%s: ", progname);
	va_start (ap, format);
	vfprintf (stderr, format, ap);
	va_end (ap);
	exit (EXIT_FAILURE);
}

static void
do_query (void)
{
	int total;
	int server;
	int client;

	total = mb_query_total ();
	if (total < 0) {
		error ("mb_query_total() said %s\n",
		       mb_error_to_string (total));;
	}

	server = mb_query_server ();
	if (server < 0) {
		error ("mb_query_server() said %s\n",
		       mb_error_to_string (server));
	}

	client = mb_query ();
	if (client < 0) {
		error ("mb_query() said %s\n",
		       mb_error_to_string (client));
	}

	printf ("total   %9d p (%.1f M)\n",
		total, pages_to_megabytes (total));
	printf ("server  %9d p (%.1f M)\n",
		server, pages_to_megabytes (server));
	printf ("client  %9d p (%.1f M)\n",
		client, pages_to_megabytes (client));
}

static void
do_reserve (int n_pages)
{
	int ret = mb_reserve_pages (n_pages);
	if (ret < 0) {
		error ("mb_reserve_pages() said %s\n",
		       mb_error_to_string (ret));
	}

	printf ("Got %d of %d pages\n", ret, n_pages);
	if (ret == 0) {
		error ("reserve of %d pages failed\n", n_pages);
	}
}

static void
do_request (int n_pages)
{
	int ret = mb_request_pages (n_pages);
	if (ret < 0) {
		error ("mb_request_pages() said %s\n",
		       mb_error_to_string (ret));
	}

	printf ("Got %d of %d pages\n", ret, n_pages);
	if (ret == 0) {
		error ("request failed\n");
	}
}

static int
check_pages (double d)
{
	int pages = (int) d;

	if (d != (double) pages)
		error ("Can't use fractional number of pages\n");

	return pages;
}

static int
percentage_of_total_pages (double d,
                           const char * arg)
{
        int n_pages;
        int total_pages;

        /* Sanity check */
        if (d < 0.0 || d > 100.0) {
                error ("Percentage %g is out of range (0,100)\n", d);
        }

        /*
         * We need to know the total in order to get a fraction of it.
         * When we're called, we haven't connected yet, so connect.
         * And then terminate, because the main line code will attempt
         * to connect, as well.
         */
        mb_register (0 /* sink */);
        total_pages = mb_query_total ();
        mb_terminate ();
        if (total_pages < 0) {
                error ("mb_query_total() said %s\n",
                       mb_error_to_string (total_pages));;
        }

        n_pages = (int) (d * total_pages / 100.0);

        printf ("%s of total %d pages is %d pages (%g M)\n",
                arg, total_pages, n_pages, pages_to_megabytes (n_pages));

        return n_pages;
}

static int
parse_n_pages (const char * arg)
{
	int n_pages = -1;
	double d;
	char multiplier = '\0';

	/*
	 * Look first for a number followed by a units modifier.
	 */
	if (2 == sscanf (arg, "%lf%c", &d, &multiplier)) {
		switch (multiplier) {
		case 'p': /* pages */
			n_pages = check_pages (d);
			break;

		case 'g':
		case 'G':
			d *= 1024;
			/* fall-through */
		case 'm':
		case 'M':
			d *= 1024;
			/* fall-through */
		case 'k':
		case 'K':
			d *= 1024;
			n_pages = (int) (d / EXEC_PAGESIZE);
			break;

                case '%': /* percent of total */
                        n_pages = percentage_of_total_pages (d, arg);
                        break;

		default:
			error ("Unknown multiplier %c\n", multiplier);
		}

	/*
	 * If just a number with no modifer, assume the value is in pages.
	 * We parse this as a floating point number so we can emit a useful
	 * error message in the case that the user gives us a decimal number.
	 * If we just look for %d here, we get back the integer portion if
	 * the user gives us a fraction, and there's no way to know it.
	 */
	} else if (1 == sscanf (arg, "%lf", &d)) {
		n_pages = check_pages (d);

	} else {
		error ("Bad amount '%s'\n", arg);
	}

	return n_pages;
}

static const char *
check_arg (int argc,
	   char *argv[],
	   const char * command,
	   int needed_arg_index)
{
	if (argc < needed_arg_index + 1) {
		error ("%s requires an argument\n", command);
	}

	return argv[needed_arg_index];
}

int
main (int argc,
      char *argv[])
{
	MbCodes command = INVALID;
	int n_pages = -1;

	progname = argv[0];

	if (argc == 1) {
		do_help (stderr);
		return EXIT_FAILURE;
	}

	if (0 == strcmp (argv[1], "help") ||
	    0 == strcmp (argv[1], "--help")) {
		do_help (stdout);
		return EXIT_SUCCESS;

	} else if (0 == strcmp (argv[1], "query")) {
		command = QUERY;

	} else if (0 == strcmp (argv[1], "request")) {
		command = REQUEST;

		n_pages = parse_n_pages (check_arg (argc, argv, argv[1], 2));

		printf ("%s '%s' -> %d pages\n", argv[1], argv[2], n_pages);

	} else if (0 == strcmp (argv[1], "reserve")) {
		command = RESERVE;

		n_pages = parse_n_pages (check_arg (argc, argv, argv[1], 2));

		printf ("%s '%s' -> %d pages\n", argv[1], argv[2], n_pages);

	} else {
		error ("Unknown command '%s'\n", argv[1]);
	}

	if (n_pages <= 0 && command != QUERY) {
		fprintf (stderr, "%s: Ignoring n_pages <= 0 (%d)\n",
			 progname, n_pages);
		return EXIT_SUCCESS;
	}

	mb_register (0 /* sink */);

	switch (command) {
	case QUERY:
		do_query ();
		mb_terminate ();
		return EXIT_SUCCESS;

	case RESERVE:
		do_reserve (n_pages);
		break;

	case REQUEST:
		do_request (n_pages);
		break;

	default:
		error ("Shouldn't get here\n");
		abort ();
	}

	/*
	 * Wait here, until killed by SIGTERM or SIGSEGV.
	 * (We could be nice and clean up on those signals,
	 * but mbserver detects the broken connection, so
	 * don't bother.
	 */
	printf ("Interrupt (^C) to release memory to membroker.\n");
	while (true)
		pause ();

	return 0;
}
