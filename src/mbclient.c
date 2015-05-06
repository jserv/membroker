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

#include "mbclient.h"
#include "mb.h"
#include "mbprivate.h"
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define min(a,b) ((a) < (b)) ? (a) : (b)
#define max(a,b) ((a) > (b)) ? (a) : (b)

typedef struct mbclient_struct {
    int id;
    int fd;
    struct sockaddr_un sock;
    int pages;
    unsigned int source_pages;
    int is_bidi;
    struct mbclient_struct * next;
} mbclient;

static mbclient mb_default_client;

static mbclient *
get_client_by_id(int id)
{
    mbclient* needle = &mb_default_client;

    if (needle->fd == 0)
        needle = needle->next;

    while (needle) {
        if (needle->id == id)
            break;
        needle = needle->next;
    }

    return needle;
}

static void
free_client(mbclient* client)
{
    mbclient * needle = &mb_default_client;

    client->fd = 0;
    client->id = 0;
    client->pages = 0;
    client->source_pages = 0;
    client->sock.sun_family = 0;
    
    while (needle){
        if (needle->next == client )
        {
            needle->next = client->next;
            free(client);
            break;
        }   
        needle = needle->next;
    }
}

static MbError validate_send(MbCodes code, int param)
{
  MbError rc = MB_SUCCESS;

  switch (code)
  {
      case REQUEST:
      case RESERVE:
      case RETURN:
      case SHARE:
      case AVAILABLE:
          if (param < 0)
              rc = MB_BAD_PARAM;
          break;
      case TERMINATE:
      case STATUS:
      case REGISTER:
      case QUERY:
      case QUERY_AVAILABLE:
      case TOTAL:
      case DENY:
          break;
      default:
          rc = MB_BAD_CODE;
          break;
  };
  return rc;
}

static MbError validate_receive(MbCodes code, int param)
{
    MbError rc = MB_SUCCESS;
    
    switch (code)
    {
        case REQUEST:
        case RESERVE:
        case RETURN:
        case SHARE:
        case TOTAL:
        case AVAILABLE:
            if (param < 0)
                rc = MB_BAD_PARAM;
            break;
        case TERMINATE:
        case QUERY:
        case QUERY_AVAILABLE:
            break;
        case REGISTER:
        case STATUS:
        case DENY:
        default:
            rc = MB_BAD_CODE;
            break;
    };
    return rc;
}

static inline int 
create_uds(mbclient* client)
{
    int fd = 0;
    
    if (client->fd != 0)
        return client->fd;
    
    fd = socket (AF_UNIX, SOCK_STREAM, 0);
    
    
    if (fd == -1){
        perror ("socket");
        return MB_IO;
    }
    
    if (fcntl (fd, F_SETFD, FD_CLOEXEC) == -1){
        perror ("fcntl");
        close (fd);
        return MB_IO;
    }
    
    client->fd = fd;

    return fd;
}

/*
 * Return the fd of the client connection.
 * Returns -1 (a bad fd) on failure.
 */
static int
contact(mbclient* client)
{
    int fd = create_uds (client);

    if (fd < 0)
        return -1;

    if (client->sock.sun_family != AF_UNIX) 
    {
        memset (&(client->sock), 0, sizeof(client->sock));
        client->sock.sun_family = AF_UNIX;
        mb_socket_name(&(client->sock.sun_path[0]), sizeof(client->sock.sun_path));
        
        if (connect (fd, (struct sockaddr *) &(client->sock), sizeof (struct sockaddr_un)) == -1) {
            perror ("connect");
            return -1;
        }
    }

    return fd;
}

static int
remote_page_request(mbclient* client, MbCodes type, int pages)
{
    int fd;
    int ret, param=0;

    if (client->is_bidi)
        return MB_BAD_CLIENT_TYPE;
    
    if (pages < 0)
        return MB_BAD_PARAM;
    
    if (pages > 0) {
        fd = contact(client);
        
	if (fd == -1)
	    return MB_IO;
	
	if ((ret = mb_encode_and_send (client->id, fd, type, pages)) < 0)
  	    return ret;
	
	ret = mb_receive_response_and_decode (fd, client->id, SHARE, &param);
	
	if (ret <= 0 )
	    return ret;
	
	client->pages += param;
    }
    return param;
}

int
mb_client_request_pages(MbClientHandle client, int pages)
{
    return remote_page_request((mbclient*)client, REQUEST, pages);
}

int 
mb_client_reserve_pages(MbClientHandle client, int pages)
{
    return remote_page_request((mbclient*)client, RESERVE, pages);    
}


int
mb_client_return_pages(MbClientHandle client, int pages)
{
    int fd, ret=0;
    
    if (pages < 0)
        return MB_BAD_PARAM;
    
    if (pages > 0) {
        fd = create_uds ((mbclient*)client);
        
        if (fd < 0)
            return MB_IO;
    
        pages = min(pages, ((mbclient*)client)->pages);
        ((mbclient*)client)->pages -= pages;
        ret = mb_encode_and_send (((mbclient*)client)->id, fd, RETURN, pages);
    }
    return ret;
}
int
mb_client_terminate(MbClientHandle client)
{
    int ret, param;
    MbCodes code;
    int fd;
    
    fd = create_uds ((mbclient*)client);
    
    if (fd < 0)
        return MB_IO;
    
    ret = mb_encode_and_send (((mbclient*)client)->id, fd, TERMINATE, 0);

    if (ret != 0) {
	do {
	    ret = mb_client_receive(client, &code, &param);
	} while (ret > 0 && code != TERMINATE);
    }
    
    if (ret < 0)
        return MB_IO;
        
    close (fd);
    
        
    free_client(client);
    return 0;
}
int 
mb_client_status(MbClientHandle client)
{
    int fd;
    
    fd = contact ((mbclient*)client);
    
    if (fd == -1)
        return MB_IO;
    
    return mb_encode_and_send (((mbclient*)client)->id, fd, STATUS, 0);
}

static int
client_register(mbclient* client)
{
    unsigned int arg;
    int fd, ret;

    fd = contact (client);

    if (fd == -1)
        return MB_IO;

    client->source_pages &= 0x7fffffff;

    arg = (client->is_bidi << 31) | client->source_pages;

    if ((ret = mb_encode_and_send (((mbclient*)client)->id, fd, REGISTER, arg)) < 0)
        return ret;

    if (!client->is_bidi)
        fd = 0;

    return fd;
}

int
mb_register(int is_bidi)
{
    mb_default_client.id = getpid();
    mb_default_client.is_bidi = is_bidi ? 1 : 0;
    mb_default_client.source_pages = 0;
    return client_register(&mb_default_client);
}
int
mb_register_source(int pages)
{
    mb_default_client.id = getpid();
    mb_default_client.is_bidi = 1;
    mb_default_client.source_pages = pages < 0 ? 0 : pages;
    return client_register(&mb_default_client);
}   
MbClientHandle
mb_client_register(int id, int is_bidi)
{
    mbclient* client = get_client_by_id(id);
    if (client == NULL) {
        client = malloc(sizeof(mbclient));
	if (client == NULL) {
	    return NULL;
	}
        memset(&(client->sock), 0, sizeof(struct sockaddr_un));
        client->id = id;
        client->pages = 0;
        client->source_pages = 0;
        client->fd = 0;
        client->is_bidi = is_bidi ? 1 : 0;
        if (client_register(client) < 0) {
            free (client);
            return NULL;
        } else {
	    client->next = mb_default_client.next;
	    mb_default_client.next = client;
	}
    } else {
        if (client->is_bidi != is_bidi)
            client = NULL;
    }
    return client;
}
MbClientHandle
mb_client_register_source(int id, int pages)
{
    mbclient* client = get_client_by_id(id);
    if (client == NULL) {
        client = malloc(sizeof(mbclient));
	if (client == NULL) {
	    return NULL;
	}
        memset(&(client->sock), 0, sizeof(struct sockaddr_un));
        client->id = id;
        client->pages = pages;
        client->source_pages = pages < 0 ? 0 : pages;
        client->fd = 0;
        client->is_bidi = 1;
        if (client_register(client) < 0) {
            free(client);
            return NULL;
        } else {
	    client->next = mb_default_client.next;
	    mb_default_client.next = client;
	}
    } else {
        if (!(client->is_bidi != 1) || 
            client->source_pages != (unsigned int)pages)
            client = NULL;
    }
    return client;
}   
int
mb_client_query_server(MbClientHandle client)
{
    int ret, param;
    int fd;
    
    if (((mbclient*)client)->is_bidi)
        return MB_BAD_PAGES + MB_BAD_CLIENT_TYPE;
    
    fd = contact ((mbclient*)client);
    
    if (fd == -1)
        return MB_BAD_PAGES + MB_IO;
    
    
    if ((ret = mb_encode_and_send (((mbclient*)client)->id, fd, QUERY, 0)) < 0)
        return MB_BAD_PAGES + ret;
    
    ret = mb_receive_response_and_decode (fd, ((mbclient*)client)->id, 
                                          QUERY, &param);
    
    if (ret < 0)
        return MB_BAD_PAGES + ret;

    return param;
}
int
mb_client_query_total(MbClientHandle client)
{
    int ret, param;
    int fd;
    
    if (((mbclient*)client)->is_bidi)
        return MB_BAD_CLIENT_TYPE;
    
    fd = contact ((mbclient*)client);
    
    if (fd == -1)
        return MB_IO;
    
    if ((ret = mb_encode_and_send (((mbclient*)client)->id, fd, TOTAL, 0)) < 0)
        return ret;
    
    ret = mb_receive_response_and_decode (fd, ((mbclient*)client)->id,
                                          TOTAL, &param);
    
    if (ret < 0)
        param = ret;

    return param;
}
int
mb_client_query(MbClientHandle client)
{
    return ((mbclient*)client)->pages;
}
int
mb_client_fd(MbClientHandle client)
{
    if (((mbclient*)client)->is_bidi)
        return ((mbclient*)client)->fd;
    else
        return 0;
}

int
mb_client_id(MbClientHandle client)
{
    return ((mbclient*)client)->id;
}

int mb_client_is_bidi(MbClientHandle client)
{
    return ((mbclient*)client)->is_bidi;
}

int mb_client_send(MbClientHandle client, MbCodes code, int param)
{
    int rc = validate_send(code, param);

    if (rc < 0)
        return rc;

    rc =  mb_encode_and_send(((mbclient*)client)->id, 
                                 ((mbclient*)client)->fd,
                                 code, param);

    if (!rc && (code == RETURN || code == SHARE))
        ((mbclient*)client)->pages -= param;
      
    return rc;
}

int mb_client_receive(MbClientHandle client, MbCodes* code, int* param)
{
    int id;
    int ret;

    ret = mb_receive_and_decode(((mbclient*)client)->fd, 
                                    &id, code, param);
    if (ret > 0) {
         if (id != ((mbclient*)client)->id)
	     ret = MB_BAD_ID;
	 else if (!(ret = validate_receive(*code, *param)) &&
                  (*code == SHARE || *code == RETURN)) 
             ((mbclient*)client)->pages += *param;
    }

    return ret;
}

int mb_request_pages( int pages )
{
    return mb_client_request_pages(&mb_default_client, pages);
}

int mb_reserve_pages( int pages )
{
    return mb_client_reserve_pages(&mb_default_client, pages);
}

int mb_return_pages( int pages )
{
    return mb_client_return_pages(&mb_default_client, pages);
}

int mb_terminate()
{
    return mb_client_terminate(&mb_default_client);
}

int mb_status()
{
    return mb_client_status(&mb_default_client);
}

int mb_query()
{
    return mb_client_query(&mb_default_client);
}

int mb_query_server()
{
    return mb_client_query_server(&mb_default_client);
}

int mb_query_total()
{
    return mb_client_query_total(&mb_default_client);
}

int mb_send(MbCodes code, int param)
{
    return mb_client_send(&mb_default_client, code, param);
}

int mb_receive(MbCodes* code, int* param)
{
    return mb_client_receive(&mb_default_client, code, param);
}

