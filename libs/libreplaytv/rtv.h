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
#include <stdarg.h>

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
typedef unsigned char      __u8;
typedef unsigned short     __u16;
typedef unsigned long      __u32;
typedef unsigned long long __u64;
typedef          long long __s64;
#define __RTV_DATA_SIZES_DEFINED__
#endif

#ifdef _WIN32
typedef unsigned char      __u8;
typedef unsigned short     __u16;
typedef unsigned long      __u32;
typedef unsigned __int64   __u64;
typedef          __int64   __s64;
#define __RTV_DATA_SIZES_DEFINED__
#endif

#ifdef __unix__
#   if ( __BYTE_ORDER == __LITTLE_ENDIAN )
#      define ntohll(x) (((__u64)(ntohl((int)((x << 32) >> 32))) << 32) | (__u32)ntohl(((int)(x >> 32))))
#      define htonll(x) ntohll(x)
#   elif ( __BYTE_ORDER == __BIG_ENDIAN )
#      define ntohll(x) (x)
#      define htonll(x)
#   else
#      error "__BYTE_ORDER MUST BE DEFINED!!!!"
#   endif
#endif
extern int  split_lines(char * src, char *** pdst);


//+*************************************************************************************************

#ifdef __APPLE__
#define U64F "q"
#else
#define U64F "ll"
#endif

struct mapping 
{
    __u32   value;
    char *name;
};

//+********************************************************************
// Device specific ID numbers. (serial#, uuid, etc)
//+********************************************************************
typedef struct rtv_idns_t
{
   char *sn_4k;
   char *uuid_4k;
   char *sn_5k;
   char *uuid_5k;   
} rtv_idns_t;

extern rtv_idns_t rtv_idns;


//+********************************************************************
// Library globals. Encapsulate in a struct.
//+********************************************************************
typedef struct rtv_globals_t
{
   int              rtv_emulate_mode; // Kick dvarchive into 4K or 5K mode
   int              merge_chunk_sz;   // Number or 32K file chunks to merge
   FILE            *log_fd;           // Where to set logs
   volatile __u32   rtv_debug;        // debug log mask
} rtv_globals_t;

extern  rtv_globals_t rtv_globals; 

//+********************************************************************
// Debugging/logging infrastructure
//+********************************************************************
extern void rtvVLog(const char *format, va_list ap);
inline static void rtv_log(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	rtvVLog(format, ap);
	va_end(ap);
}

//+********************************************************************
// Debugging/logging API's
//+********************************************************************
#define RTVLOG_INFO          (rtv_globals.rtv_debug & 0x00000001)
#define RTVLOG_DSCVR         (rtv_globals.rtv_debug & 0x00000002)
#define RTVLOG_GUIDE         (rtv_globals.rtv_debug & 0x00000004)
#define RTVLOG_HTTP          (rtv_globals.rtv_debug & 0x00000008)
#define RTVLOG_HTTP_VERB     (rtv_globals.rtv_debug & 0x00000010)
#define RTVLOG_NET           (rtv_globals.rtv_debug & 0x00000020)
#define RTVLOG_CMD           (rtv_globals.rtv_debug & 0x00000040)
#define RTVLOG_EVTFILE       (rtv_globals.rtv_debug & 0x00000100)
#define RTVLOG_EVTFILE_V2    (rtv_globals.rtv_debug & 0x00000200)
#define RTVLOG_EVTFILE_V3    (rtv_globals.rtv_debug & 0x00000400)
#define RTVLOG_NDX           (rtv_globals.rtv_debug & 0x00001000)
#define RTVLOG_NETDUMP       (rtv_globals.rtv_debug & 0x00002000)

//#define RTV_PRT(fmt, args...)  rtv_log(fmt, ## args)
#define RTV_PRT(fmt, args...) fprintf(stdout, fmt, ## args) 

#define RTV_ERRLOG(fmt, args...) RTV_PRT("rtv:ERROR: " fmt, ## args)
#define RTV_WARNLOG(fmt, args...) RTV_PRT("rtv:WARN: " fmt, ## args)
#define RTV_DBGLOG(flag, fmt, args...) \
          if (flag) { RTV_PRT("rtv:DBG:" fmt, ## args); }

#endif
