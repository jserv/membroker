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
#ifndef MBCLIENT_H
#define MBCLIENT_H

#include "mb.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef void * MbClientHandle;

/**
 * There are 2 parallel versions of the membroker client API. The mb_*
 * functions assume a single instance of the membroker client in the calling
 * process, which should normally be the case. The mb_client_* APIs should be
 * used only if there is a need to have multiple membroker client connections
 * within the same process. They mirror the standard API except that they take
 * a MbClientHandle as a context object. This was required because Scanmgr and 
 * Hostsend require their own memory pools and were running inside hydra at one
 * point. 
 */

/**
 * Establishes a sink client connection with membroker
 *
 * @param id the id that will uniquely identify this client to membroker.
 *           Because it cannot duplicate another client id, it must be based
 *           on the process id or selected from a globally maintained set of
 *           ids. The mb_register() call uses the PID.
 *
 * @param is_bidi non-zero if this client will be bi-directional (i.e. can 
 *                accept asynchronous, unsolicited requests initiated by 
 *                membroker) or zero if it is a normal synchronous client.
 *
 * @return a handle (pointer) to the membroker client, or NULL if there was an
 *         error establishing the connection, memory could not be allocated
 *         for the internal data structures, or a client has already registered
 *         in this process under the same id but a different set of parameters.
 *         If a client has already been registered in this process with all the
 *         same parameters, this function will return a reference to that
 *         client rather than establish a new one. 
 */
MbClientHandle mb_client_register(int id, int is_bidi);

/**
 * Establishes a source client connection with membroker. Source clients are
 * always bidirectional.
 *
 * @param id the id that will uniquely identify this client to membroker.
 *           Because it cannot duplicate another client id, it must be based
 *           on the process id or selected from a globally maintained set of
 *           ids. The mb_register_source() call uses the PID.
 *
 * @param pages the maximum number of pages of memory that this client can loan
 *              to membroker
 *
 * @return a handle (pointer) to the membroker client, or NULL if there was an
 *         error establishing the connection, memory could not be allocated
 *         for the internal data structures, or a client has already registered
 *         in this process under the same id but a different set of parameters.
 *         If a client has already been registered in this process with all the
 *         same parameters, this function will return a reference to that
 *         client rather than establish a new one. 
 */
MbClientHandle mb_client_register_source(int id, int pages);

/**
 * Establishes a sink client connection with membroker
 *
 * @param is_bidi non-zero if this client will be bi-directional (i.e. can 
 *                accept asynchronous, unsolicited requests initiated by 
 *                membroker) or zero if it is a normal synchronous client.
 *
 * @return a handle (pointer) to the membroker client, or NULL if there was an
 *         error establishing the connection, or a client has already been
 *         registered in this process using a different set of parameters.
 *         If a client has already been registered in this process with all the
 *         same parameters, this function will return a reference to that
 *         client rather than establish a new one. 
 */
int mb_register(int is_bidi);

/**
 * Establishes a source client connection with membroker. Source clients are
 * always bidirectional.
 *
 * @param pages the maximum number of pages of memory that this client can loan
 *              to membroker
 *
 * @return a handle (pointer) to the membroker client, or NULL if there was an
 *         error establishing the connection, or a client has already been
 *         registered in this process using a different set of parameters.
 *         If a client has already been registered in this process with all the
 *         same parameters, this function will return a reference to that
 *         client rather than establish a new one. 
 */
int mb_register_source(int pages);

/**
 * Makes a low-anxiety request for memory pages from membroker. Membroker may 
 * return fewer pages than requested and will only attempt to procure easily
 * available memory but will not block indefinitely. This function may only
 * be used by non-bidi clients.
 *
 * @param pages a non-negative number of pages to request
 *
 * @return if successful returns the number of pages from 0 to pages, or one
 *         of the following error codes:
 *         MB_BAD_CLIENT_TYPE if this is a bidi client
 *         MB_IO if there was an error communicating with membroker (see errno
 *             for more details)
 *         MB_BAD_ID or MB_BAD_CODE if the response did not match the request
 *         MB_BAD_PARAM if pages is negative
 */
int mb_client_request_pages(MbClientHandle client, int pages);
int mb_request_pages( int pages );

/**
 * Makes a high-anxiety request for memory pages from membroker. Membroker will 
 * return either 0 pages or the full amount requested,  and will make every
 * effort to rpcoure memory from other clients, possibly blocking for an
 * indefinitely long period of time. This function may only be used by non-bidi
 * clients.
 *
 * @param pages a non-negative number of pages to request
 *
 * @return if successful returns 0 or pages, or one of the following error 
 *         codes:
 *         MB_BAD_CLIENT_TYPE if this is a bidi client
 *         MB_IO if there was an error communicating with membroker (see errno
 *             for more details)
 *         MB_BAD_ID or MB_BAD_CODE if the response did not match the request
 *         MB_BAD_PARAM if pages is negative
 */
int mb_client_reserve_pages(MbClientHandle client, int pages);
int mb_reserve_pages( int pages );

/**
 * Returns unneeded pages to membroker.
 *
 * @param pages a non-negative number of pages to return to membroker. In no
 *              case will more pages be returned to membroker than the number
 *              obtained from it.
 *
 * @return 0 if the command was successful, or one of the following error codes:
 *         MB_IO if there was an error communicating with membroker (see errno
 *             for more details)
 *         MB_BAD_PARAM if pages is negative
 */
int mb_client_return_pages(MbClientHandle client, int pages);
int mb_return_pages( int pages );

/**
 * Terminates the client connection with membroker, returning all borrowed
 * pages to membroker. If this is a source client, membroker assumes all source
 * pages loaned to it at registration are returned to the client. However, it 
 * may not be possible for membroker to actually release those pages if the 
 * clients they were loaned to hold on to them. In this case membroker may run 
 * a negative page balance and the source client should not expect to be able
 * to use its pages.
 *
 * @return 0 if successful, or one of the following error codes:
 *         MB_IO if there was an error communicating with membroker (see errno
 *             for more details)
 *         MB_BAD_ID if the client received an unexpected response
 */
int mb_client_terminate(MbClientHandle client);
int mb_terminate();

/**
 * Instructs membroker to dump its current state to stdout. Used primarily for
 * interactive debugging.
 *
 * @return 0 if successful or MB_IO if there was an error communicating with
 *         membroker (see errno for more details)
 */
int mb_client_status(MbClientHandle client);
int mb_status();

/**
 * @return this client's current page balance (i.e. the number of source pages
 *         it has made available to membroker - the number of pages membroker
 *         has borrowed + the number of pages the client has borrowed)
 */
int mb_client_query(MbClientHandle client);
int mb_query();

/**
 * @return the total number of pages currently in membroker's own pool.
 */
int mb_client_query_server(MbClientHandle client);
int mb_query_server();

/**
 * The total number of pages membroker could theorectically loan out; equal to
 * the sum of its own pages plus the maximum number of source pages contributed
 * by all the source clients.
 *
 * @return the total number of source pages or one of the following error codes:
 *         MB_BAD_CLIENT_TYPE if this is a bidi client
 *         MB_IO if there was an error communicating with membroker (see errno
 *         for more details)
 *         MB_BAD_ID or MB_BAD_CODE if the response did not match the request   
 */
int mb_client_query_total(MbClientHandle client);
int mb_query_total();

/**
 * Sends a command to membroker. This is intended for use by bidi clients (in
 * conjunction with mb_receive()/mb_client_receive()) that cannot use the 
 * synchronous APIs described above because they must be prepared to receive 
 * asynchronous requests from membroker.
 *
 * @param code the command code to send
 * @param param the command parameter to send
 *
 * @return 0 on success, or one of the following error codes:
 *         MB_IO if there was an error communicating with membroker (see errno
 *         for more details)
 *         MB_BAD_CODE if the code was invalid
 *         MB_BAD_PARAM if the param was invalid
 */
int mb_client_send(MbClientHandle client, MbCodes code, int param);
int mb_send(MbCodes code, int param);

/**
 * Receives a command from membroker. This is intended for use by bidi clients
 * (in conjunction with a poll/select loop and mb_send()/mb_client_send()) that
 * cannot use the synchronous APIs described above because they must be 
 * prepared to receive asynchronous requests from membroker. This function will
 * block until it has read a command or encountered an error.
 *
 * @param code a pointer to a variable that will contain the received command
 *             code on return
 * @param param a pointer to a variable that will contain the received command
 *              parameter on return
 *
 * @return 0 on success, or one of the following error codes:
 *         MB_IO if there was an error communicating with membroker (see errno
 *         for more details)
 *         MB_BAD_ID if the client id received from membroker did not match
 *             this client
 *         MB_BAD_CODE if the code was invalid
 *         MB_BAD_PARAM if the param was invalid
 */
int mb_client_receive(MbClientHandle client, MbCodes* code, int* param);
int mb_receive(MbCodes* code, int* param);

/**
 * Provides access to the file descriptor used to communicate with membroker.
 * This is necessary to include the membroker client connection in a poll/select
 * loop.
 *
 * @param client the client handle
 *
 * @return the file descriptor connected to membroker, or 0 if the client is
 *         not bidi, or -1 if the client connection has been closed
 */
int mb_client_fd(MbClientHandle client);

/**
 * Accessor for the client id. This should not normally be required unless a
 * process is managing multiple client connections.
 *
 * @param client the client handle
 *
 * @return the id of this client (normally the process id).
 */
int mb_client_id(MbClientHandle client);

/**
 * Accessor for the client bidi attribute. This should not normally be necessary
 * unless a process is managing multiple client connections.
 *
 * @param client the client handle
 *
 * @return non-zero if the client is bidi, otherwise 0
 */
int mb_client_is_bidi(MbClientHandle client);

const char* mb_code_name(MbCodes code);

#ifdef __cplusplus
}
#endif

#endif
