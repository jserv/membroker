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
#include "mbprivate.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static inline void
i32_encode( unsigned char * buf, unsigned int i)
{
    buf[0] = i >> 24;
    buf[1] = i >> 16;
    buf[2] = i >> 8;
    buf[3] = i;
}
static inline unsigned int
i32_decode( unsigned char * buf)
{
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];

}
int
mb_encode_and_send(int id, int fd, MbCodes code, int param)
{
    static const int size = sizeof(int) *3;
    unsigned char buf[size];
    int total = 0;

    i32_encode (buf, id);
    i32_encode (&buf[sizeof(int)], code);
    i32_encode (&buf[sizeof(int) * 2], param);

    while ( total < size ){
        int ret = send (fd, buf + total, size - total, MSG_NOSIGNAL);
        if (ret == -1 ){
            perror ("send");
            return MB_IO;
        }
        total += ret;
    }

    return 0;
}

int
mb_receive_and_decode(int fd, int* id, MbCodes* code, int* param)
{    
    static const int size = sizeof(int) *3;
    unsigned char buf[size];
    int total = 0;

    while (total < size){
        int ret = recv (fd, buf + total, size - total, 0);
        if (ret == -1 && errno != EINTR ) {
            perror("recv");  
            return MB_IO;
        }
        else if (ret == 0 && total == 0) {
	    return MB_IO;
	}
        if (ret > 0)
            total += ret;
    }
    
    *id = (int)i32_decode (buf);
    *code = (int)i32_decode (&buf[sizeof(int)]);
    *param = (int)i32_decode (&buf[sizeof(int) *2]);
    
    return total;
}

int
mb_receive_response_and_decode(int fd, int id, MbCodes code, int* param)
{
    int ret_id;
    MbCodes ret_code;
    int ret = mb_receive_and_decode (fd, &ret_id, &ret_code, param);
    if (ret > 0) {
        if (ret_id != id)
            ret = MB_BAD_ID;
        else if(ret_code != code)
            ret = MB_BAD_CODE;
    }
    return ret;
}

#define MB_SOCKET_NAME "membroker"

void 
mb_socket_name(char* buffer, size_t length)
{
    const char* dir = getenv("LXK_RUNTIME_DIR");
    if (!dir)
        dir = ".";

    snprintf (buffer, length, "%s/%s", dir, MB_SOCKET_NAME);
}

const char* 
mb_code_name(MbCodes code)
{
    static const char* names[] = {
        "INVALID",
        "REQUEST",
        "RESERVE",
        "RETURN",
        "TERMINATE",
        "STATUS",
        "REGISTER",
        "SHARE",
        "QUERY",
        "QUERY_AVAILABLE",
        "AVAILABLE",
        "TOTAL",
        "DENY"
    };

    if (code >= NUM_MB_CODES)
        return "unknown";
    else
        return names[code];
}
