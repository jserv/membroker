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
    printf("Valid Commands: reserve|request|return [pages], query, end\n");
}

int 
main(void)
{

    char buf[500];
    char command[500];
    int pages;

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
