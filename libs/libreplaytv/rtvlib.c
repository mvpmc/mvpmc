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
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "rtv.h"
#include "rtvlib.h"

char local_ip_address[INET_ADDRSTRLEN] = "";
char local_hostname[255]   = "NoHostName";

// ReplayTV device list
rtv_device_list_t rtv_devices;

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

void rtv_print_device_list( void ) 
{
   int x;
   printf("ReplayTV device list: num_devices=%d\n", rtv_devices.num_rtvs);
   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_devices.rtv[x].device.ipaddr != NULL ) {
         printf("  idx=%2d  ip=%-16s  model=%s  name=%s\n", 
                x, rtv_devices.rtv[x].device.ipaddr,rtv_devices.rtv[x].device.modelNumber, rtv_devices.rtv[x].device.name);
      }
   }
}

void rtv_set_dbgmask(__u32 mask)
{
   rtv_debug = mask;
}
__u32 rtv_get_dbgmask(void)
{
   return(rtv_debug);
}

char *rtv_format_time(__u64 ttk) 
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
//   sprintf(results + 19, ".%03d", msec);
   return results;
}

int rtv_init_lib(void) 
{
   struct utsname     myname;
   int                bogus_fd, len, errno_sav;
   struct sockaddr_in ssdp_addr, local_addr;

   log_fd = stdout;
   rtv_devices.num_rtvs = 0;
   rtv_devices.rtv      = malloc(sizeof(rtv_device_t) * MAX_RTVS);
   memset(rtv_devices.rtv, 0, sizeof(rtv_device_t) * MAX_RTVS); 

   rtv_emulate_mode = RTV_DEVICE_5K;
      
   // Determine our IP address & hostname
   // gethostbyname doesn't work on the mvp so setup a bogus socket 
   // to figure out the ip address.
   //
   if ( uname(&myname) < 0 ) {
      RTV_ERRLOG("%s: Unable to determine local Hostname\n");
   }
   else {
      strncpy(local_hostname, myname.nodename, 254);
   }
   
   bzero(&ssdp_addr, sizeof(ssdp_addr));
   ssdp_addr.sin_family = AF_INET;
   ssdp_addr.sin_port   = htons(1900);
   inet_aton("239.255.255.250", &(ssdp_addr.sin_addr));
   bogus_fd = socket(AF_INET, SOCK_DGRAM, 0);
   if ( (connect(bogus_fd, (struct sockaddr*)&ssdp_addr, sizeof(ssdp_addr))) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: Unable to determine local IP address: %d==>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav));
   }
   else {
      len = sizeof(local_addr);
      getsockname(bogus_fd, (struct sockaddr*)&local_addr, &len);
      inet_ntop(AF_INET, &(local_addr.sin_addr), local_ip_address, INET_ADDRSTRLEN);  
      RTV_PRT("rtv: ipaddress: %s   hostname: %s\n", local_ip_address, local_hostname);
   }
   close(bogus_fd);
   return(0);
}
