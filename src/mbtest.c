#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include "mbclient.h"

#define min(a,b) a < b ? a:b

void printHelp()
{
    printf("Valid Commands: reserve|request|return [pages], reserve-all,\n"
           "                query, query-server, query-total, end\n");
}

void printUsage(const char* name)
{
    printf("%s [--help]\n"
           "\n"
           "This program acts as an interactive client for membroker, allowing\n"
           "one to request and return pages to the membroker pool, and query\n"
           "the current state of membroker and available pages.\n"
           "\n"
           "Below is a summary of the commands available:\n"
           "  reserve [pages]: Makes a high-anxiety request for memory pages from\n"
           "                   membroker. Membroker will return either 0 pages or\n"
           "                   the full amount requested, and will make every effort\n"
           "                   to procure memory from other clients, possibly\n"
           "                   blocking for an indefinitely long period of time.\n"
           "\n"
           "  reserve-all      Iteratively makes high anxiety requests to membroker,\n"
           "                   eventually reserving all possible pages.\n"
           "\n"
           "  request [pages]: Makes a low-anxiety request for memory pages from\n"
           "                   membroker. Membroker may return fewer pages than\n"
           "                   requested and will only attempt to procure easily\n"
           "                   available memory but will not block indefinitely.\n"
           "\n"
           "  return [pages]:  Return a number of pages previously requested to\n"
           "                   membroker.\n"
           "\n"
           "  query:           Print this client's current page balance (i.e. the\n"
           "                   number of source pages it has made available to\n"
           "                   membroker - the number of pages membroker has\n"
           "                   borrowed + the number of pages the client has\n"
           "                   borrowed)\n"
           "\n"
           "  query-server:    print the total number of pages currently in\n"
           "                   membroker's own pool.\n"
           "\n"
           "  query-total:     print the total number of pages membroker could\n"
           "                   theorectically loan out; equal to the sum of its own\n"
           "                   pages plus the maximum number of source pages\n"
           "                   contributed by all the source clients.\n"
           "\n"
           "  end:             Terminate the connection to membroker and exit\n"
           "\n",
           name
    );
}

int
main(int argc, const char** argv)
{

    char buf[500];
    char command[500];
    int pages;

    if(argc > 1)
    {
        printUsage(argv[0]);
        return 0;
    }

    int my_pages = 0;

    if (mb_register (0) < 0){
        printf("Failed to register\n");
        return -1;
    }
    do
    {
        int conv = 0;
        memset (command, 0, 500);
        memset (buf, 0, 500);
        pages = 0;
        
        printf ("Enter Command, '?' for help:\n> ");
        fflush(stdout);

        do{
            conv += read ( fileno(stdin), &buf[conv] , sizeof(char));
            if (conv == 0 && errno == 0 ) break;
        } while ( buf[conv -1 ] != '\n');

        buf[conv] = '\0';
        
        conv = sscanf(buf, "%s %d", command, &pages);

        if (conv == 2 && 0 == strcmp (command, "request")) {
            int reaped = 0;
            printf("Requesting %d pages\n", pages);
            reaped = mb_request_pages (pages);
            if (reaped > 0){
                my_pages += reaped;
                printf("Got %d pages.  Total: %d\n", reaped, my_pages);
            } else {
                printf ("request pages returns %d\n", reaped);
            }
        }else if (conv == 2 && 0 == strcmp (command, "reserve")) {
            int reaped = 0;
            printf("Requesting %d pages\n", pages);
            reaped = mb_reserve_pages (pages);
            if (reaped > 0){
                my_pages += reaped;
                printf("Got %d pages.  Total: %d\n", reaped, my_pages);
            } else {
                printf ("request pages returns %d\n", reaped);
            }
        }else if (conv == 1 && 0 == strcmp (command, "reserve-all")) {
            int total_reaped = 0;
            int incremetal_reaped = 0;

            int attempt = mb_query_total();

            // Keep getting pages until we cannot get anymore
            while(attempt > 0)
            {
                incremetal_reaped = mb_reserve_pages (attempt);
                total_reaped += incremetal_reaped;

                // Could not get this many pages, try half as many
                if(incremetal_reaped == 0)
                {
                    attempt /= 2;
                }
            }

            if (total_reaped > 0){
                my_pages += total_reaped;
                printf("Got %d pages.  Total: %d\n", total_reaped, my_pages);
            } else {
                printf ("request pages returns %d\n", total_reaped);
            }
        } else if (conv == 2 && 0 == strcmp (command, "return")) {
            int ret_pages = min(my_pages, pages);

            printf ("Returning %d pages\n", ret_pages);
            if (!mb_return_pages (ret_pages))
                my_pages -= ret_pages;

            printf ("Total Pages: %d\n", my_pages);
        } else if (conv == 1 && 0 == strcmp (command, "query")){
            int mb_pages = mb_query ();

            if (mb_pages >= 0)
                printf("membroker pages: %d.  My Pages: %d\n", mb_pages, my_pages);
        } else if (conv == 1 && 0 == strcmp (command, "query-server")){
            int mb_pages = mb_query_server ();

            if (mb_pages >= 0)
                printf("membroker server available pages: %d.\n", mb_pages);
        } else if (conv == 1 && 0 == strcmp (command, "query-total")){
            int mb_pages = mb_query_total ();

            if (mb_pages >= 0)
                printf("membroker server theoretical total pages: %d.\n", mb_pages);
        } else if (conv == 1 && 0 == strcmp (command, "status")){
            mb_status ();
        } else if ( conv == 1 && 0 == strcmp (command, "?")) {
            printHelp();
        } else if ( conv == 1 && 0 != strcmp (command, "end")) {
            printf ("Unknown command\n");
            printHelp();
        }

        printf("\n");
    } 
    while (0 != strcmp (command, "end"));

    if (my_pages > 0){
        mb_return_pages (my_pages);
    }

    mb_terminate ();

    return 0;
}
