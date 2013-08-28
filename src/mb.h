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

#ifndef MB_H
#define MB_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

typedef enum{
    INVALID=0,
    REQUEST=1,
    RESERVE,
    RETURN,
    TERMINATE,
    STATUS,
    REGISTER,
    SHARE,
    QUERY,
    QUERY_AVAILABLE,
    AVAILABLE,
    TOTAL,
    DENY,
    NUM_MB_CODES
}MbCodes; 

typedef enum {
    MB_SUCCESS=0,
    MB_OUT_OF_MEMORY = -1,
    MB_BAD_CLIENT_TYPE = -2,
    MB_IO = -3,
    MB_BAD_ID = -4,
    MB_BAD_CODE = -5,
    MB_BAD_PARAM = -6,
    MB_LAST_ERROR_CODE = MB_BAD_PARAM,
    MB_BAD_PAGES = (int32_t)(0x80000000) - MB_LAST_ERROR_CODE
} MbError;

#ifdef __cplusplus
}
#endif

#endif
