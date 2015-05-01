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
#define _GNU_SOURCE     /* so we can use struct ucred from sys/socket.h */
#include "mb.h"
#include "mbprivate.h"
#include "mbserver.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define min(a,b) ((a) < (b)) ? (a) : (b)
#define max(a,b) ((a) > (b)) ? (a) : (b)

#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0
#if defined(_POSIX_MONOTONIC_CLOCK) && (_POSIX_MONOTONIC_CLOCK >= 0)
#define MB_GET_TIME(t) clock_gettime(CLOCK_MONOTONIC, (t))
#else 
#define MB_GET_TIME(t) clock_gettime(CLOCK_REALTIME, (t))
#endif
#else
#define MB_GET_TIME(t) { time(&((t)->tv_sec)); t->tv_nsec=0; }
#endif
#define LOGFILE 0

static const char * const logfile = "mbserver.log";

typedef enum {
    NORMAL = 0,
    BIDIRECTIONAL = 1
}ClientType;

#define is_normal_client(client) \
    (client->flags == 0) 

#define is_bidirectional(client) \
    (client->flags & BIDIRECTIONAL)

#define is_source(client) \
    (client->source_pages > 0)

#define set_bidirectional(client) \
    (client->flags |= BIDIRECTIONAL)

#define set_normal(client) (client->flags &= ~BIDIRECTIONAL)

#define is_share_pending(client) \
    (client->share_type != INVALID && client->needed_pages < 0)

#define is_share_outstanding(client) \
    (client->share_type != INVALID && client->needed_pages > 0)

#define set_share_outstanding(client) \
    (client->needed_pages = -client->needed_pages)

#define clear_share(client) \
    { \
        client->share_type = INVALID; \
        client->needed_pages = 0; \
    } 

struct request;

struct client{
    int flags;
    int pid;    /* socket-reported pid of client */
    int id;     /* client-supplied id */
    int fd;
    int pages;
    int source_pages;
    char * cmdline;
    struct request * active_request;
    MbCodes share_type;
    int needed_pages;
    struct client * next;
};

typedef struct client Client;

struct clientNode
{
    Client* client;
    MbCodes code;
    struct clientNode* next;
};

typedef struct clientNode ClientNode;

struct request {
    int needed_pages;
    int acquired_pages;
    Client * requesting_client;
    Client * sharing_client;
    ClientNode* responded_clients;
    struct request * next;
    struct timespec stamp;
    MbCodes type;
    int complete;
};

typedef struct request Request;

typedef enum {
    PAGES = 1,
    CLIENT_REQUEST = 1<<1
} ServerUpdateFlags;

struct server{
    struct sockaddr_un sock;
    int client_listen_fd;
    int shutdown;
    int pages;
    unsigned int source_pages;
    Client * client_list;
    Request * queue;
    int updates;
    FILE * fp;

    struct sockaddr_un debug_sock;
    int debug_listen_fd;
};

typedef struct server Server;


static Client *
get_client_by_id( Server * server, int id)
{
    Client * client = NULL;
    Client * needle = server->client_list;
   
    while (needle) {
        if ( needle->id == id ){
            client = needle;
            break;
        }
        needle = needle->next;
    } 

    return client;
}
static Client * 
get_client_by_fd( Server * server, int fd)
{
    Client * client = NULL;
    Client * needle = server->client_list;

    while (needle) {
        if (needle->fd == fd) {
            client = needle;
            break;
        }
        needle = needle->next;
    }

    return client;
}

static inline int 
get_total_pages(Server* server)
{
    int pages = server->source_pages;
    Client* iter = server->client_list;
    while(iter)
    {
        pages += iter->source_pages;
        iter = iter->next;
    }
    return pages;
}

static inline void
give_server_pages(Server* server, int pages)
{
    server->pages += pages;
    if(pages > 0)
        server->updates |= PAGES;
}

static inline MbCodes
has_client_responded(Client* client, Request* request)
{
    ClientNode* needle = request->responded_clients;
    MbCodes rc = INVALID;
    while (needle) {
        if (needle->client == client) {
            rc = needle->code;
            break;
        }
        needle = needle->next;
    }
    return rc;
}

static void
mark_client_responded(Server* server, Request* request, Client* client)
{
    ClientNode* node = request->responded_clients;
    while (node) {
        if (node->client == client) {
            break;
        }
        node = node->next;
    }

    if (node == NULL) {
        node = malloc(sizeof(ClientNode));
    
        if (!node) {
            perror("malloc");
            exit(10);
        }
        node->next = request->responded_clients;
        request->responded_clients = node;
        node->client = client;
    }

    node->code = client->share_type;
    request->sharing_client = NULL;

    server->updates |= CLIENT_REQUEST;
}

static void 
request_complete(Server* server, Request* request)
{
    if (request->type == RESERVE && request->needed_pages) {
        give_server_pages(server, request->acquired_pages);
        request->needed_pages += request->acquired_pages;
        request->acquired_pages = 0;
    }
    
    request->complete = 1;
    
    server->updates |= CLIENT_REQUEST;
}

static inline void
request_pages (Server * server)
{
    Request* request = server->queue;
    Client* client = NULL;

    while (request) {
        /*
         * If the request already has an outstanding share or has already been
         *  marked complete, skip it
         */
        if (request->sharing_client == NULL && !request->complete) {
            int wait = 0;
            client = server->client_list;

            /* For each client... */
            while (client)
            {
                /* 
                 * Only ask clients to share pages that meet these conditions:
                 *
                 * - is bidirectional
                 * - has not already responded to this request at the present 
                 *   anxiety level
                 * - is not the requesting client
                 * - is not already processing an outstanding share query for 
                 *   another request
                 */
                MbCodes last_response = has_client_responded(client, request);
                if (is_bidirectional(client) &&
                    last_response != request->type &&
                    client != request->requesting_client &&
                    request->sharing_client == NULL)
                {
                    /*
                     * If this is a blocking client, set the wait flag so
                     * we don't prematurely mark the request complete.
                     */
                    if (client->active_request) {
                        if (client->active_request->type == REQUEST
                            && request->type == RESERVE)
                            wait = 1;
                    } else if (is_share_outstanding(client)) {
                        if (client->share_type == REQUEST
                            || request->type == RESERVE)
                            wait = 1;
                    } else {
                        MbCodes type = request->type;

                        /*
                         * If the request is RESERVing pages and this is a 
                         * source client that has not already responded, 
                         * downgrade the share query to a REQUEST to start with
                         */
                        if (type == RESERVE && is_source(client) && 
                            last_response == INVALID)
                            type = REQUEST;

                        /*
                         * Initialize the client share parameters if this is
                         * the first request in the queue to ask this client
                         * for pages. 
                         */
                        if (client->share_type == INVALID) {
                            client->share_type = type;
                            client->needed_pages = 0;
                        }

                        /*
                         * Finally!
                         * If this client's share type matches the current
                         * request, then it is OK for the request to query
                         * the client for pages.
                         */
                        if (client->share_type == type) {
                            client->needed_pages -= request->needed_pages;
                            request->sharing_client = client;
                            wait = 1;
                        }
                    }
                }

                client = client->next;
            }

            /**
             * If there are no more clients for this request to query, it's
             * done.
             */
            if (!wait)
                request_complete (server, request);
        }

        request = request->next;
    }
    
    client = server->client_list;

    /*
     * Now that we've decided which requests are going to query which clients
     * for pages, we need to send out the queries to each client...
     */
    while (client)
    {
        /**
         * Skip any clients that don't have a pending share query
         */
        if (is_share_pending(client))
        {
            /*
             * Send the query and mark it outstanding
             */
            set_share_outstanding(client);
            if (mb_encode_and_send (client->id, client->fd,
                                    client->share_type, 
                                    client->needed_pages) == 0 ) {

                fprintf (server->fp, "mbserver: %s %d pages from %s (%d)\n", 
                         client->share_type==REQUEST?"request":"reserve",
                         client->needed_pages, client->cmdline, client->id);
            } 
            else
            {
                /*
                 * If the query fails for some reason, treat the client as
                 * having responded with no pages so the request can move on
                 * to the next client. 
                 *
                 * TODO: Should we unilaterally delete the client in this case?
                 * What about any pages it had?
                 */
                request = server->queue;
                while (request) {
                    if (request->sharing_client == client) {
                        mark_client_responded(server, request, client);
                    }
                    request = request->next;
                }
                clear_share(client);

                fprintf (server->fp, "mbserver: Send error to (%d)-\"%s\"\n",
                         client->id, client->cmdline);
            }
        }

        client = client->next;
    }
}


static char *
get_cmdline_for (pid_t pid)
{
    int fd;
    char fname[64];
    char buffer[1024];

    snprintf (fname, sizeof (fname), "/proc/%d/cmdline", pid);

    do {
        fd = open (fname, O_RDONLY);
    } while (fd == -1 && errno == EINTR);

    if (fd == -1) {
        snprintf (buffer, sizeof (buffer), "unknown");

    } else {
        char * cmd;
        int bytes = 0;

        do {
            /* the cmdline file is not necessarily terminated.  Leave space. */
            bytes = read (fd, buffer, sizeof (buffer) - 1);
        } while (bytes < 0 && errno == EINTR);

        if (bytes < 0) {
            /* holy carp, how'd that happen? */
            snprintf (buffer, sizeof (buffer), "unknown");

        } else {
            /* ensure termination */
            buffer[bytes] = '\0';
        }

        close (fd);

        /*
         * /proc/pid/cmdline contains the entire command line, as fed to
         * main()...  It has nul chars between the arguments.  So, we
         * probably also read quite a lot of the arguments, but strlen(),
         * strrchr(), and other string functions will stop at the first nul,
         * which is the end of the program name.
         *
         * For a client that may have many instances of the same executable,
         * the full command line could be very useful...  but just as easily
         * could be a long string of unhelpful junk.
         */

        /* Strip off the leading path. */
        cmd = strrchr (buffer, '/');

        if (cmd) {
            memmove (buffer, cmd + 1, strlen (cmd));
        }
    }

    assert (buffer);

    return strdup (buffer);
}

static Client *
create_client (Server * server, int id, int fd, unsigned int param)
{
    Client * client = (Client *) calloc (1, sizeof (*client));

    struct ucred credentials;
    socklen_t cred_len = sizeof (credentials);

    if (!client)
    {
        perror("create_client(): calloc()\n");
        exit (1);
    }

    /* The id that comes in the client message may or may not actually be
     * the pid of the client; if the client used the "new" api, he may have
     * used some random number.  We want the real pid so we can provide
     * useful debug and potentially do some credentials verification.
     * Ask the kernel for that info.  Note that this only works because
     * we're using unix domain sockets.
     */
    memset (&credentials, 0, cred_len);
    if (0 != getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &credentials, &cred_len)) {
        /* We lived without this info for a long time, let's not bail out
         * just yet...  but spit out a nastygram. */
        printf ("Membroker WARNING: could not get credentials from socket %d: %s\n",
                fd, strerror (errno));
    }

    client->pid = credentials.pid;
    client->id = id;
    client->fd = fd;
    client->source_pages = param & 0x7fffffff;
    if (param & 0x80000000)
        set_bidirectional(client);
    else
        set_normal(client);

    client->cmdline = get_cmdline_for (client->pid);
    if (client->cmdline == NULL)
    {
        perror("create_client(): strdup()");
        exit(1);
    }
    client->share_type = INVALID;

    // Put source clients at front of list, others at the back
    if (client->source_pages) {
        client->next = server->client_list;
        server->client_list = client;
    } else {
        Client* last = server->client_list;
        client->next = NULL;
        if (last == NULL)
            server->client_list = client;
        else 
        {
            while (last->next)
                last = last->next;
            last->next = client;
        }
    }

    server->updates |= CLIENT_REQUEST;

    return client;
}

static inline Request*
free_request(Server* server, Request* request, Request* previous)
{
    Request* rc = request->next;
    ClientNode* node = request->responded_clients;
    ClientNode* next;
    while(node) {
        next = node->next;
        free(node);
        node = next;
    }
    request->requesting_client->active_request = NULL;

    give_server_pages(server, request->acquired_pages);

    if (previous)
        previous->next = rc;
    else
        server->queue = rc;

    free(request);

    server->updates |= CLIENT_REQUEST;

    return rc;
}

static void
free_client( Server * server, Client * client )
{
    Client * needle = server->client_list;
    Request* request = server->queue;
    Request* previous = NULL;

    give_server_pages(server, client->pages);

    if (needle == client){
        server->client_list = client->next;
    } else {
        while (needle){
            if (needle->next == client ){
                needle->next = client->next;
                break;
            }   
            needle = needle->next;
        }
    }

    while (request) {
        ClientNode* node = request->responded_clients;

        if (request->requesting_client == client) {
            request = free_request(server, request, previous);
            continue;
        }
        if (request->sharing_client == client)
            request->sharing_client = NULL;
            
        while(node) {
            if (node->client == client) {
                node->client = NULL;
                break;
            }
            node = node->next;
        }
        previous = request;
        request = request->next;
    }

    free (client->cmdline);
    free (client);

    server->updates |= CLIENT_REQUEST;
}

static inline void
add_request (Server * server, Client * client, int pages, MbCodes op)
{
    Request * last = server->queue;
    Request * request = (Request *) malloc (sizeof (*request));

    if (!request)
    {
        perror("add_request(): malloc");
        exit (10);
    }
    request->needed_pages = (unsigned)pages;
    request->acquired_pages = 0;
    request->requesting_client = client;
    request->sharing_client = NULL;
    request->responded_clients = NULL;
    request->next = NULL;
    MB_GET_TIME(&(request->stamp));
    request->type = op;
    request->complete = 0;

    if (last == NULL)
        server->queue = request;
    else {
        while (last->next)
            last = last->next;

        last->next = request;
    }

    client->active_request = request;

    server->updates |= CLIENT_REQUEST;
}   

static void
process_request_queue (Server * server)
{
    Request* previous = NULL;
    Request* request = server->queue;

    while(request)
    {
        if (request->complete) 
        {
            struct timespec now;
            MB_GET_TIME(&now);
            now.tv_nsec -= request->stamp.tv_nsec;
            if (now.tv_nsec < 0)
            {
                now.tv_nsec += 1000000000;
                now.tv_sec--;
            }
            now.tv_sec -= request->stamp.tv_sec;

            if (mb_encode_and_send (request->requesting_client->id,
                                    request->requesting_client->fd, 
                                    SHARE , request->acquired_pages ) == 0)
            {
                fprintf (server->fp, "mbserver: processed client (%d)-\"%s\"  - %d of %d pages in %ld.%09ld sec.\n",
                         request->requesting_client->id,
                         request->requesting_client->cmdline,
                         request->acquired_pages, 
                         request->acquired_pages + request->needed_pages,
                         now.tv_sec, now.tv_nsec);
                
                request->requesting_client->pages += request->acquired_pages;
                request->acquired_pages = 0;
            } else {
                fprintf (server->fp, "mbserver: %s: encode_and_send %d pages to (%d)-\"%s\" failed\n", __func__, request->acquired_pages, request->requesting_client->id, request->requesting_client->cmdline);
            }

            request = free_request(server, request, previous);
        }
        else {
            previous = request;
            request = request->next;
        }

    }
}

static void
process_unsolicited_pages(Server* server)
{
    Request* request = server->queue;

    while (request && server->pages > 0) {
        if (request->needed_pages) {
            int pages = min(server->pages, (int)request->needed_pages);
                request->acquired_pages += pages;
                request->needed_pages -= pages;
                server->pages -= pages;            
                if (request->needed_pages == 0)
                    request_complete(server, request);
        }
        request = request->next;
    }
}

static void
process_solicited_pages(Server* server, Client* client, int shared_pages)
{
    Request* request = server->queue;

    while (request)
    {
        if (request->sharing_client == client) {
            int pages = min(shared_pages, request->needed_pages);
            request->acquired_pages += pages;
            request->needed_pages -= pages;
            shared_pages -= pages;
            mark_client_responded(server, request, client);
            if (request->needed_pages == 0)
                request_complete(server, request);
        }
        request = request->next;
    }
    clear_share(client);

    give_server_pages(server, shared_pages);

    process_unsolicited_pages(server);
}

static void
return_shared_pages (Server * server)
{

    if (server->pages == 0) return;

    if (server->queue == NULL){
        Client * iter = server->client_list;
        while (iter) {
            if (is_source(iter) && iter->pages < 0) {
                int pages = min(server->pages, -iter->pages);
                mb_encode_and_send (iter->id, iter->fd, RETURN, pages);  
                fprintf (server->fp, "mbserver: return %d pages to (%d)-\"%s\"\n", pages, iter->id, iter->cmdline);
                server->pages -= pages;
                iter->pages += pages;
            }
            iter = iter->next;
        }
    } else {
        fprintf(server->fp, "mbserver: Can't return shared pages -- request queue non empty\n");
    }

    return;
}

static void
update_server(Server* server)
{
    if (server->pages)
        server->updates |= PAGES;

    while (server->updates)
    {
        int updates = server->updates;
        server->updates = 0;

        if (updates & PAGES)
            process_unsolicited_pages(server);
        if (updates & CLIENT_REQUEST)
            request_pages(server);
        process_request_queue(server);
    }
    return_shared_pages(server);
}


static inline Server *
initialize_server()
{
    Server *server;
    char * env = getenv ("GLIBC_POOL_SIZE");

    server = (Server *) calloc (1,sizeof(*server));

    if (!server)
    {
        perror ("malloc");
        exit (1);
    }

#if LOGFILE
    server->fp = fopen(logfile, "w");
#else
    server->fp = stdout;
#endif
    setlinebuf (server->fp);

    if (env) {
        server->source_pages =
            server->pages = atoi (env) / EXEC_PAGESIZE;

        fprintf (server->fp, "Initialized membroker with %d pages (from %s)\n", server->pages, env);
    }

    return server;
}

static void
dump_status (Server * server,
             FILE * fp)
{
    Client * client;

    fprintf (fp, "mbserver: STATUS server pages = %d of %d;  total pages = %d\n",
             server->pages, server->source_pages,
             get_total_pages(server));
    client = server->client_list;
    fprintf (fp, "mbserver: CLIENTS\n");
    while (client){
        fprintf (fp, "mbserver: (%d)-\"%s\" - %s: %d of %d pages\n",
                 client->id,
                 client->cmdline,
                 is_source(client)?"source":(is_bidirectional(client)?"bidi":"sink"),
                 client->pages,
                 client->source_pages);
        if (client->active_request)
            fprintf (fp, "mbserver:     %s %d of %d pages\n",
                     client->active_request->type==REQUEST?
                     "Requesting":"Reserving",
                     client->active_request->needed_pages,
                     client->active_request->needed_pages +
                     client->active_request->acquired_pages);
        if (client->share_type != INVALID)
            fprintf (fp, "mbserver:     %s to share %d pages\n",
                     client->share_type==REQUEST?"Requested":"Reserved",
                     client->needed_pages);
        client = client->next;
    }

    if (server->queue) {
        Request * request = server->queue;
        fprintf (fp, "mbserver: QUEUE\n");
        while (request){
            fprintf (fp, "mbserver: Client (%d)-\"%s\" %s %d of %d pages since %s",
                     request->requesting_client->id,
                     request->requesting_client->cmdline,
                     request->type==REQUEST?
                     "Requesting":"Reserving",
                     request->needed_pages,
                     request->needed_pages +
                     request->acquired_pages,
                     ctime (&(request->stamp.tv_sec)));
            if (request->sharing_client)
                fprintf (fp, "mbserver:     Actively %s %d pages from client (%d)-\"%s\"\n",
                         request->sharing_client->share_type==REQUEST?
                         "Requesting":"Reserving",
                         request->sharing_client->needed_pages,
                         request->sharing_client->id,
                         request->sharing_client->cmdline);
            if (request->responded_clients) {
                ClientNode* node = request->responded_clients;
                fprintf (fp, "mbserver:     Responded Clients:\n");
                while(node) {
                    if (node->client)
                        fprintf (fp, "mbserver:         %s from (%d)-\"%s\"\n",
                                 node->code==REQUEST?
                                 "Requested":"Reserved",
                                 node->client->id,
                                 node->client->cmdline);
                    node = node->next;
                }

            }
            request = request->next;
        }
    }
}

static inline int
process_connection(Server * server, int fd)
{
    int ret;
    int id;
    MbCodes op;
    int val;
    Client * client; 
    ret = mb_receive_and_decode (fd, &id, &op, (int*)&val);
    if (ret <= 0){
        return -1;
    } else {
        client = get_client_by_id (server, id);
        
        if (!client)
        {
            if (op == REGISTER){
                client = create_client (server, id, fd, val);
                update_server(server);
            } else {
                fprintf(server->fp, "Bad registration op %s\n", 
                        mb_code_name(op));
                return 0;
            }
        }

        if (op == DENY) {
            op = SHARE;
            val = 0;
        }

        switch (op){
            case RESERVE: /* deliberate fall through */
            case REQUEST:
                if (client->active_request)
                    break;

                if (server->pages >= val && server->queue == NULL ){
                    server->pages -= val;
                    client->pages += val;
                    mb_encode_and_send (id, fd, SHARE, val);
                    fprintf (server->fp, "Immediate Request processed: %s (%d) - SHARE %d\n",
                             client->cmdline, client->id, val);
                } else {
                    add_request (server, client, val, (MbCodes)op);
                    update_server(server);
                }
                break;
            case RETURN:
                fprintf (server->fp, "mbserver: Pages Returned: %d\n", val);
                if (client->source_pages + client->pages < val ){
                    printf ("mbserver: (%d)-\"%s\" returns %d pages, but has %d\n", 
                            client->id, client->cmdline, val,
                            client->source_pages + client->pages);
                    exit(10);
                }
                client->pages -= val;
                give_server_pages(server, val);
                update_server(server);
                break;
            case SHARE:
                fprintf (server->fp, "mbserver: Pages Shared: %d\n", val);

                if (!is_bidirectional(client)){
                    printf ("mbserver: %d-\"%s\" shares %d pages, but is not bidirectional\n", client->id, client->cmdline,
                            val);
                    exit(20);
                }
                client->pages -= val;
                process_solicited_pages(server, client, val);
                update_server(server);
                break;

            case TERMINATE:
                fprintf (server->fp, "mbserver: client (%d)-\"%s\" terminated, reclaimed %d pages\n", client->id, client->cmdline, client->pages);
		mb_encode_and_send (id, fd, TERMINATE, 0);
                free_client (server, client);
                update_server(server);

                /* client should close the fd */
                break;
            case STATUS:
                /* This isn't really useful except for interactive debugging.
                 * It's still here for backwards compatibility.
                 * For useful debug collection, read from the debug socket,
                 * instead. */
                dump_status (server, stdout);
                break;
            case QUERY:
                mb_encode_and_send (id, fd, QUERY, server->pages);
                break;
            case REGISTER:
                fprintf (server->fp, "mbserver: Register client (%d)-\"%s\"\n", client->id, client->cmdline);
                break;
            case TOTAL:
                mb_encode_and_send(id, fd, TOTAL, get_total_pages(server));
                break;
            case AVAILABLE:
                break;
            case QUERY_AVAILABLE:
                break;
            case INVALID:
            case DENY:
            default:
                break;
        }
    }

    return 0;

}

Server *
mbs_init()
{
    struct sockaddr_un addr;

    int fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror ("socket");
        return NULL;
    }

    memset (&addr, 0, sizeof (addr));
    addr.sun_family = AF_UNIX;
    mb_socket_name (&addr.sun_path[0], sizeof (addr.sun_path));
    unlink (&addr.sun_path[0]);

    if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1){
        perror ("bind");
        close(fd);
        return NULL;
    }

    if (listen (fd, 20) == -1) {
        perror ("listen");
        close(fd);
        return (NULL);
    }

    return mbs_init_with_fd (fd);
}

Server*
mbs_init_with_fd (int fd)
{
    Server* server = initialize_server ();
    socklen_t socklen;

    server->client_listen_fd = fd;

    if (server->client_listen_fd == -1) {
        fprintf (stderr, "mbserver: invalid fd\n");
        close (fd);
        free (server);
        return NULL;
    }

    socklen = sizeof (server->sock);
    if (0 != getsockname (server->client_listen_fd, (struct sockaddr *)&server->sock, &socklen)) {
        perror ("getsockname");
        close (fd);
        free (server);
        return NULL;
    }
    if (server->sock.sun_family != AF_UNIX) {
        fprintf (stderr, "Bad file descriptor passed to mbs_init_with_fd() -- not a unix domain socket\n");
        close (fd);
        free (server);
        return NULL;
    }

    chmod(server->sock.sun_path, 0777);

    /* Set up debug / status info socket as a side channel.  We don't do this
     * over the main channel because we stream out lots of data for debug,
     * whereas the main channel carries short encoded messages. */
    server->debug_listen_fd = socket (AF_UNIX, SOCK_STREAM, 0);

    if (server->debug_listen_fd == -1) {
        perror ("socket");
        close (fd);
        free (server);
        return NULL;
    }

    memset (&server->debug_sock, 0, sizeof (server->debug_sock));
    server->debug_sock.sun_family = AF_UNIX;
    snprintf (server->debug_sock.sun_path,
              sizeof (server->debug_sock.sun_path),
              "%s/membroker.debug",
              getenv ("LXK_RUNTIME_DIR") ? getenv ("LXK_RUNTIME_DIR") : ".");
    if (0 == strcmp (server->debug_sock.sun_path, server->sock.sun_path)) {
        /* This means we can't create the second socket.  Limp along with
         * no debug. */
        printf ("mbserver: Path truncation caused debug socket and main socket"
                " to have same address.  Dropping the debug socket.\n");
        close (server->debug_listen_fd);
        server->debug_listen_fd = -1;
        return server;
    }

    unlink (server->debug_sock.sun_path);
    if (0 > bind (server->debug_listen_fd,
                  (struct sockaddr *) &server->debug_sock,
                  sizeof (server->debug_sock))) {
        perror ("mbserver: bind debug socket");
        close (server->debug_listen_fd);
        server->debug_listen_fd = -1;
        return server;
    }

    if (0 > listen (server->debug_listen_fd, 10)) {
        perror ("mbserver: listen on debug socket");
        close (server->debug_listen_fd);
        server->debug_listen_fd = -1;
        return server;
    }

    return server;
}

void
mbs_set_pages(Server* server, int pages)
{
    server->source_pages = server->pages = pages;

    if (server->fp)
        fprintf (server->fp, "Set membroker server pages to %d\n",
                 pages);
}

void*
mbs_main(void* param)
{
    struct sockaddr_un remote;
    socklen_t size = sizeof(remote);
    fd_set master;
    fd_set fds;
    int max_fd;
    Server * server = (Server*)param;

    FD_ZERO( &master );
    FD_ZERO( &fds );

    FD_SET(server->client_listen_fd, &master);
    if (server->debug_listen_fd != -1) {
        FD_SET(server->debug_listen_fd, &master);
    }

    max_fd = max (server->client_listen_fd, server->debug_listen_fd);
    fds = master;
    while (-1 != select (max_fd +1, &fds, NULL, NULL, NULL)){
        int i;
        if (server->shutdown) {
            close(server->client_listen_fd);
            unlink(&(server->sock.sun_path[0]));
#if LOGFILE
            fclose(server->fp);
#endif
            break;
        }
        for ( i = 0; i <= max_fd; i++){
            if (FD_ISSET( i, &fds )){
                if (i == server->client_listen_fd){
                    int new_fd;
                    size = sizeof (remote); 
                    new_fd = accept (i, (struct sockaddr *)&remote, &size);

                    if (new_fd == -1) {
                        perror ("accept");
                        return((void*)3);
                    }

                    max_fd = max(max_fd, new_fd);
                    FD_SET( new_fd, &master);

                } else if (i == server->debug_listen_fd){
                    int new_fd;
                    FILE * fp;
                    size = sizeof (remote);

                    new_fd = accept (i, (struct sockaddr *)&remote, &size);
                    if (new_fd == -1) {
                        perror ("accept");
                        return((void*)3);
                    }

                    fp = fdopen (new_fd, "w");
                    if (! fp) {
                        close (new_fd);
                        return ((void *)3);
                    }

                    dump_status (server, fp);
                    fclose (fp);

                } else {
                    if (-1 == process_connection (server, i)){
                        Client * client = get_client_by_fd (server, i);
                        if (client) {
                            fprintf (server->fp, "non terminus close - (%d)-\"%s\"\n", client->id, client->cmdline);
                            free_client (server, client);
                            update_server(server);
                        }
                        FD_CLR (i, &master);
                        close (i);
                    }
                    
                }
            }

        }
        fds = master;
    }
    return 0;
}

void
mbs_shutdown(Server* server)
{
    struct sockaddr_un sock;
    int fd;
    server->shutdown = 1;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd == -1)
    {
        perror("socket");
        exit(1);
    }

    memset (&sock, 0, sizeof(sock));
    sock.sun_family = AF_UNIX;
    mb_socket_name(&sock.sun_path[0], sizeof(sock.sun_path));

    connect (fd, (struct sockaddr *) &sock, sizeof (struct sockaddr_un));

    close(fd);
}
