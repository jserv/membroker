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
#ifndef MBSERVER_H
#define MBSERVER_H
#ifdef __cplusplus
extern "C"
{
#endif

struct server;

struct server* mbs_init();
struct server * mbs_init_with_fd (int fd);
void mbs_set_pages(struct server* server, int pages);
void* mbs_main(void* param);
void mbs_shutdown(struct server* server);

#ifdef __cplusplus
}
#endif
#endif
