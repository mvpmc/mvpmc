/*
 * Copyright (C) 2004 John Honeycutt
 * Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */
#include <stdio.h>

#ifndef RTV_H
#define RTV_H

#define TRUE  (1)
#define FALSE (0)

//+**********************************************************
// Define architecture specific data types
// Define 64-bit byte swapping routines
// Make sure to include <netinet/in.h> for ntohl, ...
//+**********************************************************
#ifdef __unix__
#define _LARGEFILE64_SOURCE
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned long      u32;
typedef unsigned long long u64;
typedef          long long s64;
#endif

#ifdef _WIN32
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned long      u32;
typedef unsigned __int64   u64;
typedef          __int64   s64;
#endif

#ifdef __unix__
#   if ( __BYTE_ORDER == __LITTLE_ENDIAN )
#      define ntohll(x) (((u64)(ntohl((int)((x << 32) >> 32))) << 32) | (u32)ntohl(((int)(x >> 32))))
#      define htonll(x) ntohll(x)
#   elif ( __BYTE_ORDER == __BIG_ENDIAN )
#      define ntohll(x) (x)
#      define htonll(x)
#   else
#      error "__BYTE_ORDER MUST BE DEFINED!!!!"
#   endif
#endif

extern void hex_dump(char * tag, unsigned char * buf, unsigned int sz);
extern int  split_lines(char * src, char *** pdst);


//+*************************************************************************************************

#ifdef __APPLE__
#define U64F "q"
#else
#define U64F "ll"
#endif


struct mapping 
{
    u32   value;
    char *name;
};

//+********************************************************************
// Debugging
//+********************************************************************
typedef int (*rtvlogfxn_t)(FILE *, const char *, ...);
extern rtvlogfxn_t rtvlogfxn; 
extern FILE *log_fd;
extern u32   rtv_debug;

#define RTVLOG_INFO          (rtv_debug & 0x00000001)
#define RTVLOG_DSCVR         (rtv_debug & 0x00000002)
#define RTVLOG_GUIDE         (rtv_debug & 0x00000004)
#define RTVLOG_HTTP          (rtv_debug & 0x00000008)
#define RTVLOG_HTTP_VERB     (rtv_debug & 0x00000010)
#define RTVLOG_NET           (rtv_debug & 0x00000020)
#define RTVLOG_CMD           (rtv_debug & 0x00000040)
#define RTVLOG_NETDUMP       (rtv_debug & 0x10000000)

#define RTV_PRT(fmt, args...)  rtvlogfxn(log_fd, fmt, ## args)

#define RTV_ERRLOG(fmt, args...) RTV_PRT("rtv:ERROR: " fmt, ## args)
#define RTV_WARNLOG(fmt, args...) RTV_PRT("rtv:WARN: " fmt, ## args)
#define RTV_DBGLOG(flag, fmt, args...) \
          if (flag) { RTV_PRT("rtv:DBG:" fmt, ## args); }

#endif
