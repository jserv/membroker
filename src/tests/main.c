#include "mbclient.h"
#include "mbserver.h"
#include "mbprivate.h"
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define min(a,b) a < b ? a:b
#define max(a,b) a > b ? a:b

#define FAIL_UNLESS(condition) \
do { \
    if (! ( condition) ) { \
      printf ("%s:%d: assertion `%s' failed\n", \
        __func__, __LINE__, #condition); \
      exit (1);            \
     } \
   } while(0)

typedef struct
{
    const char* name;
    int (*test)();
    int pages;
} TestLookup;

typedef struct
{
    MbClientHandle client;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int pages;
    int reservable_pages;
    int requestable_pages;
    int shutdown;
    MbCodes preresponse;
    MbCodes postresponse;
    int pause;
    MbCodes pauseCode;
} TestClient;

static pthread_t serverThread;
static struct server* server;

static int startServer(int pages)
{
    pthread_attr_t attr;
    int rc;

    server = mbs_init();
    if (!server)
        return 1;

    mbs_set_pages(server, pages);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
    rc = pthread_create(&serverThread, &attr, &mbs_main, server);
    if (rc < 0) {
        perror("pthread_create");
        return -1;
    }
    return 0;
}

static int stopServer()
{
    int rc;

    mbs_shutdown(server);

    rc = pthread_join(serverThread, NULL);
    if (rc < 0) {
        perror("pthread_join");
        return -1;
    }

    free(server);

    return 0;
}

static void* clientThread(void* param)
{
    TestClient* tc = (TestClient*)param;
    fd_set fds;
    int fd = mb_client_fd(tc->client);
    struct timeval timeout = { 0, 10000 };
    FD_ZERO (&fds);
    FD_SET (fd, &fds);

    printf("Started client %d thread\n", 
           mb_client_id(tc->client));

    while (-1 != select (fd + 1, &fds, NULL, NULL, &timeout)){
        MbCodes code;
        int ret;
        int pages;
        int reaped_pages;

        pthread_mutex_lock(&(tc->mutex));

        if (FD_ISSET( fd, &fds)){

            ret = mb_client_receive (tc->client, &code, &pages);

            FAIL_UNLESS(ret == 0);

            printf("Client %d received code %s(%d)\n", 
                   mb_client_id(tc->client),
                   mb_code_name(code), pages);

            tc->preresponse = code;
            pthread_cond_signal(&(tc->cond));

            if (tc->pause && 
                (tc->pauseCode == INVALID || tc->pauseCode ==code)) {
                printf("Pausing client %d\n", mb_client_id(tc->client));
                while (tc->pause && !tc->shutdown)
                    pthread_cond_wait(&(tc->cond), &(tc->mutex));
                if (tc->shutdown) {
                    pthread_mutex_unlock(&(tc->mutex));
                    break;
                }
                printf("Resuming client %d\n", mb_client_id(tc->client));
            }

            switch (code) {
                case REQUEST:
                    reaped_pages = min(pages, tc->requestable_pages);
                    tc->pages -= reaped_pages;
                    tc->requestable_pages -= reaped_pages;
                    tc->reservable_pages -= reaped_pages;
                    if (reaped_pages)
                        ret = mb_client_send (tc->client, SHARE, reaped_pages);
                    else
    		        ret = mb_client_send (tc->client, DENY, pages);
		    FAIL_UNLESS(ret == 0);
                    break;
                case RESERVE:
                    reaped_pages = min(pages, tc->requestable_pages);
                    if (reaped_pages < pages)
                        reaped_pages = min(pages, tc->reservable_pages);
                    if (reaped_pages == pages)
                    {
                        tc->requestable_pages -= min(reaped_pages, tc->requestable_pages);
                        tc->reservable_pages -= reaped_pages;
                        tc->pages -= reaped_pages;
                        ret = mb_client_send (tc->client, SHARE, reaped_pages);
                    }
                    else 
		        ret = mb_client_send (tc->client, DENY, pages);
		    FAIL_UNLESS(ret == 0);
                    break;
                case SHARE:
                    tc->pages += pages;
                    tc->reservable_pages += pages;
                    tc->requestable_pages += pages;                    
                    break;
                case RETURN:
                    tc->pages += pages;
                    tc->reservable_pages += pages;
                    tc->requestable_pages += pages;
                    break;
                case QUERY_AVAILABLE:
                    pages = tc->reservable_pages;
                    ret = mb_client_send (tc->client, AVAILABLE, pages);
		    FAIL_UNLESS(ret == 0);
                    break;
                case QUERY:
                    break;
                case INVALID:
                    printf("INVALID code\n");
                    exit(-1);
                    break;
                default:
                    printf ("Unhandled code %d\n", code);
                    break;
            }

            printf("Client %d processed code %s(%d)\n", 
                   mb_client_id(tc->client),
                   mb_code_name(code), pages);

            tc->postresponse = code;
            pthread_cond_signal(&(tc->cond));

        }
        else if (tc->shutdown) {
            pthread_mutex_unlock(&(tc->mutex));
            break;
        }

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        pthread_mutex_unlock(&(tc->mutex));

        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;
    }

    printf("Exiting client %d thread: %d %s\n", 
           mb_client_id(tc->client), errno, strerror(errno));

    return NULL;
}

static void set_pages(TestClient* tc, int pages)
{
    tc->requestable_pages = tc->reservable_pages = tc->pages = pages;
}

static TestClient* createTestClient(int id, int is_bidi, int pages)
{
    TestClient* tc = malloc(sizeof(TestClient));
    FAIL_UNLESS(is_bidi || !pages);
    set_pages(tc, pages);
    tc->shutdown = 0;
    tc->preresponse = INVALID;
    tc->postresponse = INVALID;
    tc->pause = 0;
    if (pages)
        tc->client = mb_client_register_source(id, pages);
    else
        tc->client = mb_client_register(id, is_bidi);
    if (!(tc->client)) {
        free(tc);
        return NULL;
    }

    pthread_mutex_init(&(tc->mutex), NULL);
    pthread_cond_init(&(tc->cond), NULL);
    if (is_bidi) {
        pthread_attr_t attr;
        int rc;

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
        printf("Starting client %d thread...\n", 
               mb_client_id(tc->client));

        rc = pthread_create(&(tc->thread), &attr, &clientThread, tc);
        if (rc < 0) {
            perror("pthread_create");
            mb_client_terminate(tc->client);
            free(tc);
            return NULL;
        }
    }
    return tc;
}

static void destroyTestClient(TestClient* tc, int terminate)
{
    int rc, id;

    printf("Destroying client %d...\n", 
           mb_client_id(tc->client));

    if (mb_client_is_bidi(tc->client)) {
        pthread_mutex_lock(&(tc->mutex));
        tc->shutdown = 1;
        pthread_cond_signal(&(tc->cond));
        pthread_mutex_unlock(&(tc->mutex));
        rc = pthread_join(tc->thread, NULL);
        if (rc < 0) {
            perror("pthread_join");
            exit(-1);
        }
        printf("Joined client %d thread\n", 
               mb_client_id(tc->client));
    }

    id = mb_client_id(tc->client);

    if (terminate) {
        FAIL_UNLESS(mb_client_terminate(tc->client) == 0);
	tc->client = NULL;
    }
    else {
        close(mb_client_fd(tc->client));
    }

    pthread_mutex_destroy(&(tc->mutex));
    pthread_cond_destroy(&(tc->cond));

    printf("Destroyed client %d\n", id);

    free(tc);    
}

static void terminateTestClient(TestClient* tc)
{
    destroyTestClient(tc, 1);
}

static void closeTestClient(TestClient* tc)
{
    destroyTestClient(tc, 0);
}

static int page_count(TestClient* tc)
{
    int rc;
    pthread_mutex_lock(&(tc->mutex));
    rc = mb_client_query(tc->client);
    pthread_mutex_unlock(&(tc->mutex));
    return rc;
}

static void clearServerPostResponse(TestClient* tc)
{
    pthread_mutex_lock(&(tc->mutex));
    tc->postresponse = INVALID;
    pthread_mutex_unlock(&(tc->mutex));
}

static MbCodes waitForServerPostResponse(TestClient* tc)
{
    MbCodes rc = INVALID;
    pthread_mutex_lock(&(tc->mutex));
    printf("Waiting on client %d response\n", mb_client_id(tc->client));
    while(tc->postresponse == INVALID)
    {
        pthread_cond_wait(&(tc->cond), &(tc->mutex));
    }
    rc = tc->postresponse;
    tc->postresponse = INVALID;
    printf("Received client %d response %s\n", mb_client_id(tc->client),
           mb_code_name(rc));
    pthread_mutex_unlock(&(tc->mutex));
    return rc;
}

static void waitUntilServerPostResponse(TestClient* tc, MbCodes response)
{
    pthread_mutex_lock(&(tc->mutex));
    printf("Waiting on client %d response %s\n", mb_client_id(tc->client),
           mb_code_name(response));
    while(tc->postresponse != response)
    {
        pthread_cond_wait(&(tc->cond), &(tc->mutex));
        printf("Saw client %d response %s\n", mb_client_id(tc->client),
               mb_code_name(response));
    }
    tc->postresponse = INVALID;
    printf("Received client %d response %s\n", mb_client_id(tc->client),
           mb_code_name(response));
    pthread_mutex_unlock(&(tc->mutex));
}

static void clearServerPreResponse(TestClient* tc)
{
    pthread_mutex_lock(&(tc->mutex));
    tc->preresponse = INVALID;
    pthread_mutex_unlock(&(tc->mutex));
}

MbCodes waitForServerPreResponse(TestClient* tc)
{
    MbCodes rc = INVALID;
    pthread_mutex_lock(&(tc->mutex));
    printf("Waiting on client %d response\n", mb_client_id(tc->client));
    while(tc->preresponse == INVALID)
    {
        pthread_cond_wait(&(tc->cond), &(tc->mutex));
    }
    rc = tc->preresponse;
    tc->preresponse = INVALID;
    printf("Received client %d response %s\n", mb_client_id(tc->client),
           mb_code_name(rc));
    pthread_mutex_unlock(&(tc->mutex));
    return rc;
}

static void waitUntilServerPreResponse(TestClient* tc, MbCodes response)
{
    pthread_mutex_lock(&(tc->mutex));
    printf("Waiting on client %d response %s\n", mb_client_id(tc->client),
           mb_code_name(response));
    while(tc->preresponse != response)
    {
        pthread_cond_wait(&(tc->cond), &(tc->mutex));
        printf("Saw client %d response %s\n", mb_client_id(tc->client),
               mb_code_name(response));
    }
    tc->preresponse = INVALID;
    printf("Received client %d response %s\n", mb_client_id(tc->client),
           mb_code_name(response));
    pthread_mutex_unlock(&(tc->mutex));
}

static void flushClient(TestClient* tc)
{
    if (mb_client_is_bidi(tc->client)) {
        int rc;
        clearServerPostResponse(tc);
	rc = mb_client_send(tc->client, QUERY, 0);
	FAIL_UNLESS(rc == 0);
        waitUntilServerPostResponse(tc, QUERY);
    }
    else {
        FAIL_UNLESS(mb_client_query_server(tc->client) > MB_BAD_PAGES);
    }
}

static void dumpStatus(TestClient* tc)
{
    int rc;
    flushClient(tc);
    rc = mb_client_send(tc->client, STATUS, 0);
    FAIL_UNLESS(rc == 0);
    flushClient(tc);
}

static void pauseClientOn(TestClient* tc, MbCodes code)
{
    pthread_mutex_lock(&(tc->mutex));
    tc->pause = 1;
    tc->pauseCode = code;
    pthread_mutex_unlock(&(tc->mutex));
}

static void pauseClient(TestClient* tc)
{
    pauseClientOn(tc, INVALID);
}

static void resumeClient(TestClient* tc)
{
    pthread_mutex_lock(&(tc->mutex));
    tc->pause = 0;
    pthread_cond_signal(&(tc->cond));
    pthread_mutex_unlock(&(tc->mutex));
}

int initAndTerminate()
{
    int rc;
    MbClientHandle source = mb_client_register(1, 1);
    MbClientHandle client = mb_client_register(2, 0);

    MbClientHandle duplicate = mb_client_register(2, 0);

    // Verify duplicate client registrations are identified
    FAIL_UNLESS(duplicate == client);

    // Verify incompatible duplicate client registrations are rejected
    duplicate = mb_client_register(2, 1);
    FAIL_UNLESS(duplicate == NULL);

    duplicate = mb_client_register_source(1, 10);
    FAIL_UNLESS(duplicate == NULL);

    // Verify client attributes
    FAIL_UNLESS(mb_client_fd(source) != 0);
    FAIL_UNLESS(mb_client_is_bidi(source));
    FAIL_UNLESS(mb_client_id(source) == 1);

    FAIL_UNLESS(mb_client_fd(client) == 0);
    FAIL_UNLESS(!mb_client_is_bidi(client));
    FAIL_UNLESS(mb_client_id(client) == 2);

    // Verify that no-ops succeed
    rc = mb_client_request_pages(client, 0);
    FAIL_UNLESS(rc == 0);

    rc = mb_client_return_pages(client, 0);
    FAIL_UNLESS(rc == 0);

    mb_client_terminate(source);
    mb_client_terminate(client);

    return 0;
}

int testNormalRequest()
{
    int rc;
    TestClient* source = createTestClient(1, 1, 10);
    TestClient* sink = createTestClient(2, 0, 0);

    // Check total pages and server pages
    FAIL_UNLESS(mb_client_query_total(sink->client) == 15);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);
    FAIL_UNLESS(page_count(sink) == 0);
    FAIL_UNLESS(page_count(source) == 10);

    // Request server pages
    rc = mb_client_request_pages(sink->client, 4);

    FAIL_UNLESS(rc == 4);
    FAIL_UNLESS(page_count(sink) == 4);
    FAIL_UNLESS(page_count(source) == 10);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 1);

    // Over-return pages to server
    rc = mb_client_return_pages(sink->client, 5);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 0);
    FAIL_UNLESS(page_count(source) == 10);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);

    // Request pages from server and source
    rc = mb_client_request_pages(sink->client, 8);

    FAIL_UNLESS(rc == 8);
    FAIL_UNLESS(page_count(sink) == 8);
    FAIL_UNLESS(page_count(source) == 7);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);

    // Return some source pages
    clearServerPostResponse(source);
    rc = mb_client_return_pages(sink->client, 2);
    FAIL_UNLESS(waitForServerPostResponse(source) == RETURN);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 6);
    FAIL_UNLESS(page_count(source) == 9);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);

    // Return rest of source pages and some server pages
    clearServerPostResponse(source);
    rc = mb_client_return_pages(sink->client, 3);
    waitForServerPostResponse(source);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 3);
    FAIL_UNLESS(page_count(source) == 10);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 2);

    // Return remaining server pages
    rc = mb_client_return_pages(sink->client, 3);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 0);
    FAIL_UNLESS(page_count(source) == 10);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);

    terminateTestClient(sink);
    terminateTestClient(source);

    
    return 0;
}

int testNormalReserve()
{
    int rc;
    TestClient* sink = createTestClient(2, 0, 0);
    TestClient* source = createTestClient(1, 1, 10);

    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);

    // Limit requestable/reservable pages
    source->requestable_pages = 4;
    source->reservable_pages = 8;

    // Request more pages than are requestable
    rc = mb_client_request_pages(sink->client, 10);

    FAIL_UNLESS(rc == 9);
    FAIL_UNLESS(page_count(sink) == 9);
    FAIL_UNLESS(page_count(source) == 6);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);
    
    // Reserve pages
    rc = mb_client_reserve_pages(sink->client, 4);

    FAIL_UNLESS(rc == 4);
    FAIL_UNLESS(page_count(sink) == 13);
    FAIL_UNLESS(page_count(source) == 2);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);

    // Return all pages to server
    clearServerPostResponse(source);
    rc = mb_client_return_pages(sink->client, 13);
    FAIL_UNLESS(waitForServerPostResponse(source) == RETURN);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 0);
    FAIL_UNLESS(page_count(source) == 10);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);

    // Reserve more pages than are reservable
    clearServerPostResponse(source);
    rc = mb_client_reserve_pages(sink->client, 15);
    waitUntilServerPostResponse(source, RETURN);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 0);
    FAIL_UNLESS(page_count(source) == 10);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);
    
    terminateTestClient(sink);
    terminateTestClient(source);    

    return 0;
}

static int testQueryOnQuerying(MbCodes query1, MbCodes query2)
{
    int rc;
    TestClient* source1 = createTestClient(1, 1, 10);
    TestClient* source2 = createTestClient(2, 1, 5);
    TestClient* sink = createTestClient(3, 0, 0);
    
    // Check total pages and server pages
    FAIL_UNLESS(mb_client_query_total(sink->client) == 20);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);
    FAIL_UNLESS(page_count(source1) == 10);
    FAIL_UNLESS(page_count(source2) == 5);
    FAIL_UNLESS(page_count(sink) == 0);
    
    // query2 should not wait for a client's prior query1 to complete.
    // It should skip that client and return if there are no other clients
    // to query.
    pauseClient(source1);

    rc = mb_client_send(source2->client, query1, 15);

    FAIL_UNLESS(rc == 0);

    flushClient(sink);

    clearServerPostResponse(source1);
    clearServerPostResponse(source2);

    rc = mb_client_send(source1->client, query2, 18);

    FAIL_UNLESS(rc == 0);

    dumpStatus(sink);

    resumeClient(source1);

    waitUntilServerPostResponse(source1, SHARE);

    waitUntilServerPostResponse(source2, SHARE);

    FAIL_UNLESS(page_count(source1) == 0);
    FAIL_UNLESS(page_count(source2) == 20);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);

    terminateTestClient(source1);
    terminateTestClient(source2);
    terminateTestClient(sink);
    return 0;
}

int testReserveOnRequesting()
{
    int rc;
    TestClient* source1 = createTestClient(1, 1, 10);
    TestClient* source2 = createTestClient(2, 1, 5);
    TestClient* sink = createTestClient(3, 0, 0);
    
    // Check total pages and server pages
    FAIL_UNLESS(mb_client_query_total(sink->client) == 20);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);
    FAIL_UNLESS(page_count(source1) == 10);
    FAIL_UNLESS(page_count(source2) == 5);
    FAIL_UNLESS(page_count(sink) == 0);

    // a RESERVing client should block while a client's prior REQUEST completes
    // before querying that client for pages
    pauseClient(source1);

    rc = mb_client_send(source2->client, REQUEST, 15);

    FAIL_UNLESS(rc == 0);

    flushClient(sink);

    rc = mb_client_send(source1->client, RESERVE, 18);

    FAIL_UNLESS(rc == 0);

    flushClient(sink);

    clearServerPostResponse(source1);
    clearServerPostResponse(source2);

    dumpStatus(sink);

    resumeClient(source1);

    waitUntilServerPostResponse(source1, SHARE);

    FAIL_UNLESS(page_count(source1) == 18);
    FAIL_UNLESS(page_count(source2) == 2);   
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);
    
    terminateTestClient(source1);
    terminateTestClient(source2);
    terminateTestClient(sink);
    return 0;
}

int testRequestOnRequesting()
{
    return testQueryOnQuerying(REQUEST, REQUEST);
}

int testRequestOnReserving()
{
    return testQueryOnQuerying(RESERVE, REQUEST);
}

int testReserveOnReserving()
{
    return testQueryOnQuerying(RESERVE, RESERVE);
}

int testRequestOnReserved()
{
    int rc;
    TestClient* bidi1 = createTestClient(1, 1, 0);
    TestClient* bidi2 = createTestClient(2, 1, 0);
    TestClient* sink = createTestClient(3, 0, 0);

    // Move 10 pages to bidi1
    clearServerPostResponse(bidi1);
    rc = mb_client_send(bidi1->client, REQUEST, 10);
    FAIL_UNLESS(rc == 0);
    waitUntilServerPostResponse(bidi1, SHARE);
    
    // Check total pages and server pages
    FAIL_UNLESS(mb_client_query_total(sink->client) == 15);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);
    FAIL_UNLESS(page_count(bidi1) == 10);
    FAIL_UNLESS(page_count(bidi2) == 0);
    FAIL_UNLESS(page_count(sink) == 0);

    // A REQUESTing client should not block while a client services a prior
    // RESERVE. It should skip that client and return if there are no other
    // clients to query.
    pauseClient(bidi1);

    rc = mb_client_send(bidi2->client, RESERVE, 10);
    
    FAIL_UNLESS(rc == 0);

    flushClient(sink);

    rc = mb_client_request_pages(sink->client, 5);

    FAIL_UNLESS(rc == 0);

    clearServerPostResponse(bidi2);

    dumpStatus(sink);

    resumeClient(bidi1);

    waitUntilServerPostResponse(bidi2, SHARE);

    FAIL_UNLESS(page_count(bidi1) == 5);
    FAIL_UNLESS(page_count(bidi2) == 10);
    FAIL_UNLESS(page_count(sink) == 0);   
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);
    
    terminateTestClient(bidi1);
    terminateTestClient(bidi2);
    terminateTestClient(sink);
    return 0;
}

static int testQueryOnQueried(MbCodes query1, MbCodes query2)
{
    TestClient* bidi1 = createTestClient(1, 1, 0);
    TestClient* bidi2 = createTestClient(2, 1, 0);
    TestClient* sink = createTestClient(3, 0, 0);
    MbCodes code;
    int param;
    int rc;

    // Move 10 pages to bidi1
    clearServerPostResponse(bidi1);
    rc = mb_client_send(bidi1->client, REQUEST, 10);
    FAIL_UNLESS(rc == 0);
    waitUntilServerPostResponse(bidi1, SHARE);

    // Check total pages and server pages
    FAIL_UNLESS(mb_client_query_total(sink->client) == 15);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 5);
    FAIL_UNLESS(page_count(bidi1) == 10);
    FAIL_UNLESS(page_count(bidi2) == 0);
    FAIL_UNLESS(page_count(sink) == 0);

    // query2 should block while a client services query1 before querying that
    // client for pages
    pauseClient(bidi1);

    rc = mb_client_send(bidi2->client, query1, 10);
    FAIL_UNLESS(rc == 0);

    flushClient(sink);

    rc = mb_client_send(sink->client, query2, 5);
    FAIL_UNLESS(rc == 0);

    dumpStatus(sink);

    clearServerPostResponse(bidi2);

    resumeClient(bidi1);

    rc = mb_client_receive(sink->client, &code, &param);
    FAIL_UNLESS(rc == 0);

    waitUntilServerPostResponse(bidi2, SHARE);

    FAIL_UNLESS(page_count(bidi1) == 0);
    FAIL_UNLESS(page_count(bidi2) == 10);   
    FAIL_UNLESS(page_count(sink) == 5);   
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 5);

    terminateTestClient(bidi1);
    terminateTestClient(bidi2);
    terminateTestClient(sink);
    return 0;
}

int testRequestOnRequested()
{
    return testQueryOnQueried(REQUEST, REQUEST);
}

int testReserveOnRequested()
{
    return testQueryOnQueried(REQUEST, RESERVE);
}

int testReserveOnReserved()
{
    return testQueryOnQueried(RESERVE, RESERVE);
}

int testReturnOnRequest()
{
    TestClient* source = createTestClient(1, 1, 10);
    TestClient* sink1 = createTestClient(2, 0, 0);
    TestClient* sink2 = createTestClient(3, 0, 0);
    MbCodes code;
    int param;
    int rc;

    rc = mb_client_request_pages(sink1->client, 5);
    FAIL_UNLESS(rc == 5);

    flushClient(source);
    flushClient(sink1);
    flushClient(sink2);

    FAIL_UNLESS(mb_client_query_total(sink1->client) == 10);
    FAIL_UNLESS(mb_client_query_server(sink1->client) == 0);
    FAIL_UNLESS(page_count(sink1) == 5);
    FAIL_UNLESS(page_count(sink2) == 0);
    FAIL_UNLESS(page_count(source) == 5);

    pauseClient(source);

    clearServerPreResponse(source);

    rc = mb_client_send(sink2->client, REQUEST, 4);
    FAIL_UNLESS(rc == 0);

    waitUntilServerPreResponse(source, REQUEST);

    rc = mb_client_return_pages(sink1->client, 5);
    FAIL_UNLESS(rc == 0);

    rc = mb_client_receive(sink2->client, &code, &param);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 4);

    clearServerPostResponse(source);
    resumeClient(source);

    while(page_count(source) != 6)
        waitUntilServerPostResponse(source, RETURN);

    flushClient(source);

    FAIL_UNLESS(page_count(source) == 6);

    terminateTestClient(source);
    terminateTestClient(sink1);
    terminateTestClient(sink2);
    return 0;
}

int testMultipleRequests()
{
    TestClient* source = createTestClient(1, 1, 100);
    TestClient* bidi1 = createTestClient(2, 1, 0);
    TestClient* bidi2 = createTestClient(3, 1, 0);
    TestClient* sink1 = createTestClient(4, 0, 0);
    TestClient* sink2 = createTestClient(5, 0, 0);
    TestClient* sink3 = createTestClient(6, 0, 0);
    TestClient* sink4 = createTestClient(7, 0, 0);
    MbCodes code;
    int param;
    int rc;

    source->requestable_pages = 50;

    flushClient(sink4);

    pauseClient(source);

    rc = mb_client_send(sink1->client, REQUEST, 10);
    FAIL_UNLESS(rc == 0);

    flushClient(sink1);

    clearServerPostResponse(bidi1);
    clearServerPostResponse(bidi2);
    rc = mb_client_send(sink2->client, REQUEST, 20);
    FAIL_UNLESS(rc == 0);
    waitUntilServerPostResponse(bidi1, REQUEST);
    waitUntilServerPostResponse(bidi2, REQUEST);

    clearServerPostResponse(bidi1);
    clearServerPostResponse(bidi2);
    rc = mb_client_send(sink3->client, RESERVE, 30);
    FAIL_UNLESS(rc == 0);
    waitUntilServerPostResponse(bidi1, RESERVE);
    waitUntilServerPostResponse(bidi2, RESERVE);

    clearServerPostResponse(bidi1);
    clearServerPostResponse(bidi2);
    rc = mb_client_send(sink4->client, RESERVE, 40);
    FAIL_UNLESS(rc == 0);
    waitUntilServerPostResponse(bidi1, RESERVE);
    waitUntilServerPostResponse(bidi2, RESERVE);

    flushClient(sink1);
    flushClient(sink2);
    flushClient(sink3);
    dumpStatus(sink4);

    terminateTestClient(bidi1);

    dumpStatus(sink4);

    terminateTestClient(bidi2);

    dumpStatus(sink4);

    resumeClient(source);

    rc = mb_client_receive(sink1->client, &code, &param);
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink1) == 10);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 10);

    rc = mb_client_receive(sink2->client, &code, &param);
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink2) == 20);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 20);

    rc = mb_client_receive(sink3->client, &code, &param);
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink3) == 30);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 30);

    rc = mb_client_receive(sink4->client, &code, &param);
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink4) == 40);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 40);

    terminateTestClient(sink4);
    terminateTestClient(sink3);
    terminateTestClient(sink2);
    terminateTestClient(sink1);
    terminateTestClient(source);

    return 0;
}

int testClientTermination()
{
    TestClient* source = createTestClient(1, 1, 10);
    TestClient* bidi = createTestClient(2, 1, 0);
    TestClient* sink = createTestClient(3, 0, 0);
    MbCodes code;
    int param;
    int rc;

    // Remove a client with an active request
    flushClient(bidi);

    pauseClient(source);

    rc = mb_client_send(bidi->client, REQUEST, 10);
    FAIL_UNLESS(rc == 0);

    flushClient(bidi);

    terminateTestClient(bidi);

    clearServerPostResponse(source);

    resumeClient(source);

    waitUntilServerPostResponse(source, RETURN);

    FAIL_UNLESS(page_count(source) == 10);

    // Remove a client servicing a request
    flushClient(sink);

    clearServerPreResponse(source);
    pauseClientOn(source, REQUEST);

    rc = mb_client_send(sink->client, REQUEST, 10);
    FAIL_UNLESS(rc == 0);

    flushClient(sink);

    waitUntilServerPreResponse(source, REQUEST);

    terminateTestClient(source);

    rc = mb_client_receive(sink->client, &code, &param);

    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 0);

    terminateTestClient(sink);

    return 0;
}

int testIoErrors()
{
    TestClient* source = createTestClient(1, 1, 10);
    TestClient* sink = createTestClient(2, 0, 0);
    TestClient* bidi = createTestClient(3, 1, 0);
    MbClientHandle* badfd;
    MbCodes code;
    int param;
    int rc;

    //Make sure bidi clients are not allowed to use synchronous APIs
    rc = mb_client_request_pages(bidi->client, 1);
    FAIL_UNLESS(rc == MB_BAD_CLIENT_TYPE);

    rc = mb_client_reserve_pages(bidi->client, 1);
    FAIL_UNLESS(rc == MB_BAD_CLIENT_TYPE);

    rc = mb_client_query_server(bidi->client);
    FAIL_UNLESS(rc == MB_BAD_PAGES + MB_BAD_CLIENT_TYPE);

    rc = mb_client_query_total(bidi->client);
    FAIL_UNLESS(rc == MB_BAD_CLIENT_TYPE);

    terminateTestClient(bidi);

    // Test invalid codes and parameters
    rc = mb_client_send(bidi->client, INVALID, 0);
    FAIL_UNLESS(rc == MB_BAD_CODE);

    rc = mb_client_send(bidi->client, REQUEST, -1);
    FAIL_UNLESS(rc == MB_BAD_PARAM);

    rc = mb_client_reserve_pages(sink->client, -3);
    FAIL_UNLESS(rc == MB_BAD_PARAM);

    rc = mb_client_return_pages(sink->client, -3);
    FAIL_UNLESS(rc == MB_BAD_PARAM);
    
    // Make sure a disconnected client returns the correct error
    badfd = mb_client_register(4, 1);
    close(mb_client_fd(badfd));
    rc = mb_client_return_pages(badfd, 1);
    FAIL_UNLESS(rc == MB_IO);

    
    // Client issues request while request already active
    flushClient(source);
    flushClient(sink);

    pauseClient(source);

    rc = mb_client_send(sink->client, REQUEST, 1);
    FAIL_UNLESS(rc == 0);

    rc = mb_client_send(sink->client, REQUEST, 2);
    FAIL_UNLESS(rc == 0);

    flushClient(sink);

    resumeClient(source);

    rc = mb_client_receive(sink->client, &code, &param);
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 1);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 1);

    flushClient(sink);

    // Client closes connection without terminating
    closeTestClient(source);

    rc = mb_client_request_pages(sink->client, 5);
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 1);
    FAIL_UNLESS(mb_client_query_total(sink->client) == 0);
    FAIL_UNLESS(mb_client_query_server(sink->client) == -1);

    rc = mb_client_return_pages(sink->client, 1);
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(mb_client_query_total(sink->client) == 0);
    FAIL_UNLESS(mb_client_query_server(sink->client) == 0);

    terminateTestClient(sink);
    return 0;
}

int testDumpDebug()
{
    TestClient* source = createTestClient(1, 1, 10);
    TestClient* sink = createTestClient(2, 0, 0);

    MbCodes code;
    int param, rc;
    int debug_client;
    struct sockaddr_un debug_addr;
    int n_read;
    const char * socket_dir;

    // Client issues request while request already active

    // Making source pages reservable only ensures that mbserver must
    // query the source twice for pages and ensures that it will see the
    // second request before the first one is complete.
    source->requestable_pages = 0;
    source->reservable_pages = 10;

    flushClient(source);
    flushClient(sink);

    pauseClient(source);

    mb_client_send(sink->client, RESERVE, 1);

    mb_client_send(sink->client, RESERVE, 2);

    // Submit a debug request in the middle of this transaction
    debug_client = socket (AF_UNIX, SOCK_STREAM, 0);
    assert (debug_client > -1);
    memset (&debug_addr, 0, sizeof (debug_addr));
    debug_addr.sun_family = AF_UNIX;
    socket_dir = getenv ("LXK_RUNTIME_DIR");
    if (! socket_dir)
        socket_dir = ".";
    snprintf (debug_addr.sun_path, sizeof (debug_addr.sun_path),
              "%s/membroker.debug", socket_dir);
    if (0 > connect (debug_client,
                     (struct sockaddr *) &debug_addr,
                     sizeof (debug_addr))) {
        abort ();
    }
    do {
        char buf[1024];
        do {
            n_read = read (debug_client, buf, sizeof (buf));
        } while (n_read != -1 && errno == EINTR);
        fwrite (buf, n_read, 1, stdout);
    } while (n_read > 0);
    close (debug_client);

    // Now allow the transaction to complete
    resumeClient(source);

    rc = mb_client_receive(sink->client, &code, &param);
    
    FAIL_UNLESS(rc == 0);
    FAIL_UNLESS(page_count(sink) == 1);
    FAIL_UNLESS(code == SHARE);
    FAIL_UNLESS(param == 1);

    flushClient(sink);
    flushClient(source);

    terminateTestClient(source);
    terminateTestClient(sink);

    return 0;
}

static TestLookup testTable[] = {
    { "initAndTerminate", &initAndTerminate, 0},
    { "testNormalRequest", &testNormalRequest, 5 },
    { "testNormalReserve", &testNormalReserve, 5 },
    { "testRequestOnRequesting", &testRequestOnRequesting, 5 },
    { "testReserveOnRequesting", &testReserveOnRequesting, 5 },
    { "testRequestOnReserving", &testRequestOnReserving, 5 },
    { "testReserveOnReserving", &testReserveOnReserving, 5 },
    { "testRequestOnReserved", &testRequestOnReserved, 15 },
    { "testRequestOnRequested", &testRequestOnRequested, 15 },
    { "testReserveOnRequested", &testReserveOnRequested, 15 },
    { "testReserveOnReserved", &testReserveOnReserved, 15 },
    { "testReturnOnRequest", &testReturnOnRequest, 0 },
    { "testMultipleRequests", &testMultipleRequests, 0 },
    { "testClientTermination", &testClientTermination, 0 },
    { "testIoErrors", &testIoErrors, 0 },
    { "testDumpDebug", &testDumpDebug, 0 }
};

#define N_ELEMENTS(ary)  (sizeof (ary) / sizeof (ary[0]))

int main(int argc, char ** argv)
{
    int rc = 0;
    int index;

    setlinebuf(stdout);

    if (argc < 2)
    {
        printf("Missing test argument\n");
        return -1;
    }

    for (index=0; index<(int)N_ELEMENTS(testTable); index++)
        if (strcmp(testTable[index].name, argv[1]) == 0)
            break;

    if (index == N_ELEMENTS(testTable)) {
        printf("Cound not find test %s\n", argv[1]);
        return -1;
    }

    printf("Running test: %s\n", argv[1]);

    if ((rc = startServer(testTable[index].pages)))
        return rc;

    if ((rc = testTable[index].test()))
        return rc;
    
    if ((rc = stopServer()))
        return rc;

    return 0;
}
