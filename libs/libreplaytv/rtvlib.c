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
#include "rtv.h"
#include "rtvlib.h"

#define MAX_RTVS 10

// ReplayTV device list
rtv_device_list_t rtv_devices;

rtv_device_t *rtv_get_device_struct(const char *ipaddr, int *new) 
{
   int x;
   
   *new = 0;
   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_devices.rtv[x].device.ipaddr == NULL ) {
         *new = 1;
         return(&(rtv_devices.rtv[x])); //New entry
      }
      if ( strcmp(rtv_devices.rtv[x].device.ipaddr, ipaddr) == 0 ) {
         return(&(rtv_devices.rtv[x])); //Existing entry
      }
   }
   return(NULL);
}

int rtv_free_devices(void)
{
   int x;
   for ( x=0; x < rtv_devices.num_rtvs; x++ ) {
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
   printf("ReplayTV device list:\n");
   for ( x=0; x < rtv_devices.num_rtvs; x++ ) {
      printf("  idx=%2d  ", x);
      if ( rtv_devices.rtv[x].device.ipaddr != NULL ) {
         printf("ip=%s  model=%s  name=%s\n", 
                rtv_devices.rtv[x].device.ipaddr,rtv_devices.rtv[x].device.modelNumber, rtv_devices.rtv[x].device.name);
      }
      else {
         printf("ip=NULL\n");
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
   log_fd = stdout;
   rtv_devices.num_rtvs = 0;
   rtv_devices.rtv      = malloc(sizeof(rtv_device_t) * MAX_RTVS);
   memset(rtv_devices.rtv, 0, sizeof(rtv_device_t) * MAX_RTVS); 
   return(0);
}
