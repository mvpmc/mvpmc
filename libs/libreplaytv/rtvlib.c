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

static int fprintf_flush(FILE *stream, const char *format, ...)
{
   int rc;
   va_list ap;
   va_start(ap, format);
   rc = vfprintf(stream, format, ap);
   va_end(ap);
   fflush(stream);
   return(rc);
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
   log_fd    = stdout;
   rtvlogfxn = fprintf;
   return(0);
}

int rtv_route_logs(char *filename)
{
   int   rc = 0;
   FILE *tmpfd;

   if ( (log_fd != NULL) && (log_fd != stdout) && (log_fd != stderr) ) {
      fclose(log_fd);
   }
   if ( strcmp(filename, "stderr") == 0 ) {
      log_fd    = stderr;
      rtvlogfxn = fprintf;
   }
   else if ( strcmp(filename, "stdout") == 0 ) {
      log_fd    = stdout;
      rtvlogfxn = fprintf;
   } 
   else {
      tmpfd = fopen(filename, "w");
      if ( tmpfd == NULL ) {
         rc = errno;
         RTV_ERRLOG("Failed to open logfile, fopen: %s\n", filename);
         RTV_ERRLOG("       errno=%d=>%s\n", rc, strerror(rc));
      }
      else {
         RTV_PRT("Sending logs to: %s\n", filename);
         rtvlogfxn = fprintf_flush;
         log_fd    = tmpfd;
      }
   }
   return(rc);
}
