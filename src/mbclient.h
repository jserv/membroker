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

MbClientHandle mb_client_register(int id, int is_bidi);
MbClientHandle mb_client_register_source(int id, int pages);
int mb_client_request_pages(MbClientHandle client, int pages);
int mb_client_reserve_pages(MbClientHandle client, int pages);
int mb_client_return_pages(MbClientHandle client, int pages);
int mb_client_terminate(MbClientHandle client);
int mb_client_status(MbClientHandle client);
int mb_client_query(MbClientHandle client);
int mb_client_query_server(MbClientHandle client);
int mb_client_query_total(MbClientHandle client);
int mb_client_fd(MbClientHandle client);
int mb_client_id(MbClientHandle client);
int mb_client_is_bidi(MbClientHandle client);
int mb_client_send(MbClientHandle client, MbCodes code, int param);
int mb_client_receive(MbClientHandle client, MbCodes* code, int* param);

int mb_register();
int mb_register_source(int pages);
int mb_request_pages(int pages );
int mb_reserve_pages(int pages );
int mb_return_pages(int pages );
int mb_terminate();
int mb_status();
int mb_query();
int mb_query_server();
int mb_query_total();
int mb_send(MbCodes code, int param);
int mb_receive(MbCodes* code, int* param);

const char* mb_code_name(MbCodes code);

#ifdef __cplusplus
}
#endif

#endif
