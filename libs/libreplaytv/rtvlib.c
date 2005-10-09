/*
 *  Copyright (C) 2004, John Honeycutt
 *  http://mvpmc.sourceforge.net/
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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "rtv.h"
#include "rtvlib.h"

char local_ip_address[INET_ADDRSTRLEN] = "";
char local_hostname[255]   = "NoHostName";

// ReplayTV device list
rtv_device_list_t rtv_devices;

// rtv_get_device_struct()
//
rtv_device_t *rtv_get_device_struct(const char *ipaddr, int *new) 
{
   int first_null_idx = -1;
   int x;
   
   *new = 0;
   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_devices.rtv[x].device.ipaddr == NULL ) {
         if ( first_null_idx == -1 ) {
            first_null_idx = x;
         }
      }
      else if ( strcmp(rtv_devices.rtv[x].device.ipaddr, ipaddr) == 0 ) {
         return(&(rtv_devices.rtv[x])); //Existing entry
      }
   }
   if ( first_null_idx != -1 ) {
      *new = 1;
      return(&(rtv_devices.rtv[first_null_idx]));
   }
   return(NULL);
}

// rtv_free_devices()
//
int rtv_free_devices(void)
{
   int x;
   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_devices.rtv[x].device.ipaddr != NULL ) {
         rtv_free_device_info(&(rtv_devices.rtv[x].device));
         rtv_free_guide(&(rtv_devices.rtv[x].guide));
      }
   }
   rtv_devices.num_rtvs = 0;
   return(0);
}

// rtv_print_device_list()
//
void rtv_print_device_list( void ) 
{
   int x;
   RTV_PRT("ReplayTV device list: num_devices=%d\n", rtv_devices.num_rtvs);
   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_devices.rtv[x].device.ipaddr != NULL ) {
         RTV_PRT("  idx=%2d  ip=%-16s  model=%s  name=%s\n", 
                 x, rtv_devices.rtv[x].device.ipaddr,rtv_devices.rtv[x].device.modelNumber, rtv_devices.rtv[x].device.name);
      }
   }
}

// rtv_set_32k_chunks_to_merge()
//
void rtv_set_32k_chunks_to_merge(int chunks)
{
   rtv_globals.merge_chunk_sz = chunks;
}

// rtv_set_dbgmask()
// rtv_get_dbgmask()
//
void rtv_set_dbgmask(__u32 mask)
{
   rtv_globals.rtv_debug = mask;
}
__u32 rtv_get_dbgmask(void)
{
   return(rtv_globals.rtv_debug);
}


//  rtv_set_discover_options
//
int rtv_set_discover_options(int timeout, int maxnum_rtv, rtv_vintage_t dva_mode)
{
   rtv_globals.discover_tmo     = timeout;
   rtv_globals.max_num_rtv      = maxnum_rtv;
   rtv_globals.rtv_emulate_mode = dva_mode;
   if ( maxnum_rtv ) {
      RTV_PRT("rtv: discover options: timeout=%d dva_mode=%s max_num_rtv=%d\n", timeout, ((dva_mode==RTV_DEVICE_4K) ? "4K" : "5K"), maxnum_rtv);
   }
   else {
      RTV_PRT("rtv: discover options: timeout=%d dva_mode=%s\n", timeout, ((dva_mode==RTV_DEVICE_4K) ? "4K" : "5K"));
   }
   return(0);
}

// Time formatting api's
//

//+*********************************************************************
//  Name: rtv_format_ts_sec32_hms
//        Convert seconds to HH:MM:SS
//+*********************************************************************
char* rtv_format_ts_sec32_hms(__u32 ts, char *time_str)
{
   int h = ts / 3600;
   int m = (ts - (h * 3600)) / 60; 
   int s = ts % 60;

   sprintf (time_str, "%02d:%02d:%02d", h, m, s);
   return time_str;
}

//+*********************************************************************
//  Name: rtv_format_ts_ms32_min_sec_ms
//        Convert mS to MMM:SS.mS
//+*********************************************************************
char* rtv_format_ts_ms32_min_sec_ms(__u32 ts, char *time_str)
{
   __u32 ms = ts;
   __u32 sec = ms / 1000;
   __u32 min = sec / 60;
   
   sec -= (min*60);
   ms %= 1000;
   sprintf (time_str, "%03d:%02d.%03d", (int)min, (int)sec, (int)ms);
   return time_str;
}

//+*********************************************************************
//  Name: rtv_format_datetime_sec32
//        Convert 32 bit value (sec since Jan 1 1970) to date time string
//+*********************************************************************
char *rtv_format_datetime_sec32(__u32 t) 
{
   char      *result = malloc(20);
   struct tm *tm;
    
   tm = localtime(&t);
   strftime(result, 20, "%Y-%m-%d %H:%M:%S", tm);
   return result;
}

//+*********************************************************************
//  Name: rtv_format_datetime_sec32_2
//        Convert 32 bit value (sec since Jan 1 1970) to date time string
//+*********************************************************************
char *rtv_format_datetime_sec32_2(__u32 t) 
{
   char      *result = malloc(256);
   struct tm *tm;
    
   tm = localtime(&t);
   strftime(result, 255, "%m/%d at %l:%M %p", tm);   
   return result;
}

//+*********************************************************************
//  Name: rtv_format_datetime_ms64_1
//        Convert 64 bit value (mS since Jan 1 1970) to date time string
//+*********************************************************************
char *rtv_format_datetime_ms64_1(__u64 ttk) 
{
   char      *results;   
   time_t     tt;
   int        msec;
   struct tm  tm_st;

   results = malloc(256);
   tt      = ttk / 1000;
   msec    = ttk % 1000;
   localtime_r(&tt, &tm_st);
  
   strftime(results, 255, "%Y-%m-%d %H:%M:%S", &tm_st);   
   return results;
}

//+*********************************************************************
//  Name: rtv_format_datetime_ms64_2
//        Convert 64 bit value (mS since Jan 1 1970) to date time string
//+*********************************************************************
char *rtv_format_datetime_ms64_2(__u64 ttk) 
{
   char      *results;   
   time_t     tt;
   int        msec;
   struct tm  tm_st;

   results = malloc(256);
   tt      = ttk / 1000;
   msec    = ttk % 1000;
   localtime_r(&tt, &tm_st);
  
   strftime(results, 255, "%m/%d at %l:%M %p", &tm_st);   
   return results;
}

//+*********************************************************************
// Name: rtv_format_nsec64()
// Convert 64 bit nanoseconds to minute second string. User must free string.
//+*********************************************************************
char *rtv_format_nsec64(__u64 nsec)
{
   char         *result; 
   unsigned int  minutes;
   unsigned int  seconds;
   unsigned int  msec;

   result  = malloc(20);
   msec    = nsec / 1000000;
   seconds = msec / 1000;
   minutes = seconds / 60;
   seconds %= 60;
   msec    %= 1000;
   if (minutes) {
      sprintf(result, "%03d:", minutes);
   }
   else {
      strcpy(result, "    ");
   }
   sprintf(result + 4, "%02u.%03u", seconds, msec);
   
   return(result);
}


//+*********************************************************************
// Name: *rtv_sec_to_hr_mn_str
// Convert 32 bit seconds to minute second string. User must free string.
//+*********************************************************************
char *rtv_sec_to_hr_mn_str(unsigned int seconds)
{
   char *result = malloc(30);
   char *pos;
   unsigned int minutes, hours;

   if ( seconds < 60 ) {
      sprintf(result, "0 minutes");
      return(result);
   }

   minutes = seconds / 60;
   if ( (seconds % 60) > 30 ) {
      minutes++;
   }
   hours = minutes / 60;
   minutes %= 60;
      
   pos = result;
   if ( hours == 1 ) {
      pos += sprintf(pos, "1 hour");
   }
   else if ( hours > 1 ) { 
      pos += snprintf(pos, 29, "%u hours", hours);      
   }
   
   if ( (pos != result) && (minutes) ) {
      pos += snprintf(pos, 29-(pos-result), " ");
   } 
   
   if ( minutes > 1 ) {
      pos += snprintf(pos, 29-(pos-result), "%u minutes", minutes);
   }
   else if ( minutes == 1 ) {
      pos += snprintf(pos, 29-(pos-result), "1 minute");
   }
   
   return(result);
}

// rtv_init_lib()
//
int rtv_init_lib(void) 
{
   struct utsname     myname;
   struct sockaddr_in local_addr;
   struct hostent    *hptr;

   rtv_globals.discover_tmo     = 6;
   rtv_globals.max_num_rtv      = 0;
   rtv_globals.rtv_emulate_mode = RTV_DEVICE_5K;
   rtv_globals.merge_chunk_sz   = 3;                // 3 - 32K chunks
   rtv_globals.log_fd           = stdout;
   rtv_globals.rtv_debug        = 0x00000001;
   //rtv_globals.rtv_debug        = 0x100000ff;

   rtv_devices.num_rtvs = 0;
   rtv_devices.rtv      = malloc(sizeof(rtv_device_t) * MAX_RTVS);
   memset(rtv_devices.rtv, 0, sizeof(rtv_device_t) * MAX_RTVS); 

      
   // Determine our IP address & hostname
   // gethostbyname doesn't work on the mvp so setup a bogus socket 
   // to figure out the ip address.
   //
   if ( uname(&myname) < 0 ) {
      RTV_ERRLOG("%s: Unable to determine local Hostname\n", __FUNCTION__);
      return(-1);
   }
   strncpy(local_hostname, myname.nodename, 254);

   
   if((hptr = gethostbyname( myname.nodename )) == NULL)
   {
      RTV_ERRLOG("%s: Unable to determine local IP address: h_errno=%d\n", __FUNCTION__, h_errno);
      return(-1);
   }

   memcpy(&local_addr.sin_addr.s_addr, *hptr->h_addr_list, sizeof( struct in_addr));
   inet_ntop(AF_INET, &(local_addr.sin_addr), local_ip_address, INET_ADDRSTRLEN);  
   RTV_PRT("rtv: ipaddress: %s   hostname: %s\n", local_ip_address, local_hostname);

   return(0);
}

// rtv_convert_22_ndx_rec()
//
void rtv_convert_22_ndx_rec(rtv_ndx_22_record_t *rec)
{
   rec->video_offset      = ntohs(rec->video_offset);
   rec->macrovision_count = ntohs(rec->macrovision_count);
   rec->audio_offset      = ntohl(rec->audio_offset);
   rec->timestamp         = ntohll(rec->timestamp);
   rec->stream_position   = ntohll(rec->stream_position);
   return;
}

// rtv_convert_30_ndx_rec()
//
void rtv_convert_30_ndx_rec(rtv_ndx_30_record_t *rec)
{
   rec->timestamp      = ntohll(rec->timestamp);
   rec->filepos_iframe = ntohll(rec->filepos_iframe);
   rec->iframe_size    = ntohl(rec->iframe_size);
   return;
}

// rtv_convert_evt_rec()
//
void rtv_convert_evt_rec(rtv_evt_record_t *rec)
{
   rec->timestamp   = ntohll(rec->timestamp);
   rec->data_type   = ntohl(rec->data_type);
   rec->audiopower  = ntohl(rec->audiopower);
   rec->blacklevel  = ntohl(rec->blacklevel);
   return;
}


// rtv_hex_dump()
//
void rtv_hex_dump(char *tag, unsigned long address, unsigned char *buf, size_t sz, int ascii_decode_bool)
{
    unsigned int  rows, row, col, i, c;
    unsigned long addr;
    char          tmpstr[512];
    char         *strp = tmpstr;

    RTV_PRT("HEXDUMP: %s\n", tag);
    rows = (sz + 15)/16;
    for (row = 0; row < rows; row++) {
        addr = address + (row*16);
        strp += sprintf(strp, "0x%08lx | ", addr);
        for (col = 0; col < 16; col++) {
            i = row * 16 + col;
            if (i < sz) {
               strp += sprintf(strp, "%02x", buf[i]);
            }
            else {
                strp += sprintf(strp, "  ");
            }
            if ((i & 3) == 3) {
               strp += sprintf(strp, " ");
            }
        }
        if ( ascii_decode_bool ) {
           strp += sprintf(strp, "  |  ");
           for (col = 0; col < 16; col++) {
              i = row * 16 + col;
              if (i < sz) {
                 c = buf[i];
                 strp += sprintf(strp, "%c", (c >= ' ' && c <= '~') ? c : '.');
              } else {
                 strp += sprintf(strp, " ");
              }
              if ((i & 3) == 3) {
                 strp += sprintf(strp, " ");
              }
           }
           RTV_PRT("%s |\n", tmpstr);
        }
        else { 
           RTV_PRT("%s\n", tmpstr);
        }
        strp = tmpstr;
    }
}

