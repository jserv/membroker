/* membroker - A service to cooperatively manage memory usage system-wide
 *
 * Copyright © 2013 Lexmark International
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL").
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied.
 *
 * The Original Code is the membroker service, and client library.
 *
 * The Initial Developer of the Original Code is Lexmark International, Inc.
 * Author: Ian Watkins
 *
 * Commercial licensing is available. See the file COPYING for contact
 * information.
 */

#include "mb.h"
#include "mbclient.h"
#include "mbprivate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


void 
client_loop(void)
{
    int pages = 0;
    int failcnt = 0;
    int termcount =0;
    int page_ceiling;
    int mode = 0;
    
    srand48( time (0) );

    page_ceiling = lrand48() % 40 * 1024 * 1024 / 4096;
    mb_register(0);

    while(1){

        if ( pages < page_ceiling && mode == 0 ){
            int ask = lrand48() % 200;
            int req;

            req = mb_request_pages ( ask );
            if ( req < 0 )
            {
                printf("transmission error\n");
                exit(0);
            } else if (req == 0 ) {
                if (failcnt > 10 ){
                    sleep(1);
                    failcnt = 0;
                    mode = 1;
                    termcount++;
                } else {
                    usleep(100);
                }
                failcnt++;
            } else {
                if (req != ask){
                    printf ("requested %d pages, got %d\n", ask, req);
                    failcnt++;
                    usleep(10);
                } else if(termcount > 0)
                    termcount--;
                pages += req;
            }
        } else if (pages > 2) {
            int ret = lrand48() % pages; 
            printf ("returning %d of %d pages\n", ret, pages);
            if( mb_return_pages ( ret ) < 0 ) {
                printf ("transmission error\n");
                exit(0);
            } else {
               pages -=ret;
            } 

        } else {
            usleep(50);
            mode = mode == 0 ? 1:0;
            termcount++;
        }

        if(termcount > 10 ){
            printf("failed to get pages 10 times.\n");
            mb_terminate();
            exit(0);
        }
    }

}
void
status_loop(void)
{
    mb_register (0);
    while (1){
        sleep (10);
        mb_status ();
    }
}

void
bidi_loop(void)
{
   int fd = mb_register (1); 
   fd_set fds;

   assert (fd >= 0);

   FD_ZERO (&fds);
   FD_SET (fd, &fds);

   while (-1 != select (fd + 1, &fds, NULL, NULL, NULL)){
        MbCodes code;
        int pid;  
        int pages;
        int ret;

        if (FD_ISSET( fd, &fds)){

            ret = mb_receive_and_decode (fd, &pid, &code, &pages); 

            if (ret == -1){
                printf("receive returned -1\n");
                exit(5);
            }

            switch(code){
                case REQUEST:
                case RESERVE:
                    printf ("got request for %d pages\n", pages);
                    mb_send (SHARE, pages);
                    break;
                case RETURN:
                    printf("got %d pages back", pages);
                    break;
                default:
                    break;
            }
        }

        FD_SET (fd, &fds);
   } 

}

#define UNUSED __attribute__((unused))

int 
main(int argc UNUSED, char ** argv UNUSED)
{
    unsigned int i = 0;
    pid_t pids[1];
    pid_t status = fork ();
    pid_t bidi;
    char buffer[1024];

    if (status == 0)
        status_loop();

    bidi = fork();

    if (bidi == 0 )
        bidi_loop();

    for ( ; i < sizeof(pids)/sizeof(pids[0]); i++){
        pid_t pid =  0; /* fork(); */
        if( pid == 0 )
        {
            client_loop(); 
        }
        pids[i] = pid;
    }

    for(i = 0; i < sizeof(pids)/sizeof(pids[0]); i++)
        wait ( NULL );

    waitpid (status, NULL, 0);
    waitpid (bidi, NULL, 0);
    mb_socket_name(buffer, 1024);
    printf("%s\n", buffer);
    return 0;
}
