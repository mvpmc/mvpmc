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

#if defined(__unix__) && !defined(__FreeBSD__)
#   include <netinet/in.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include "rtv.h"

// ID Numbers used for communication with RTV's
//
rtv_idns_t rtv_idns = 
{
   .sn_4k   = "RTV4080L1AA5319999",
   .uuid_4k = "506052b0-242e-a2cb-3921-72f0f3319999",
   .sn_5k   = "RTV5040J3TR0209999",
   .uuid_5k = "93da697b-a177-96c3-3858-2c19c9899999"
};


// Globals that can be access by all code.
// Initialized by rtv_init_lib()
//
rtv_globals_t rtv_globals = 
{
   .rtv_emulate_mode = 0,
   .merge_chunk_sz   = 0,
   .log_fd           = NULL,
   .rtv_debug        = 0

}; 



//static unsigned int log_rotate_cnt = 8192;
static unsigned int log_rotate_cnt = 512;
static unsigned int log_count      = 0;
static int          log_to_file    = 0;
static char         log_fn[PATH_MAX];
static char         log_backup_fn[PATH_MAX];

int rtv_route_logs(char *filename);

int split_lines(char * src, char *** pdst)
{
    int num_lines, i;
    char * p;
    char ** dst;

    num_lines = 0;
    p = src;
    while (p) {
        p = strchr(p, '\n');
        if (p) {
            p++;
            num_lines++;
        }
    }

    dst = malloc((num_lines + 1) * sizeof(char *));
    dst[0] = src;
    p = src;
    i = 1;
    while (p) {
        p = strchr(p, '\n');
        if (p) {
            *p = '\0';
            p++;
            dst[i] = p;
            i++;
        }
    }

    *pdst = dst;

    return num_lines;
}

//+*****************************************************************************
// Logging stuff 
//+*****************************************************************************
static int  rename_file(const char *from, const char *to)
{
   if ( link(from, to) < 0 ) {
      if ( errno != EEXIST ) {
         return -1;
      }
      if ( (unlink(to) < 0) || (link(from, to) < 0) ) {
         return -1;
      }  
   }
   return(unlink(from));
}   

static void rotate_logs(void)
{
   FILE *new_fd = NULL;
  
   log_count = 0;
   
   // Move present log file to backup file
   //
   RTV_PRT("Rotating log files...\n");
   if ( rename_file(log_fn, log_backup_fn) == -1 ) {
      RTV_ERRLOG("Rotate LogFile: backup rename error: %s\n      errno=%d=>%s", 
                 log_fn, errno, strerror(errno));
   }

   //open a new log file
   //
   new_fd = fopen(log_fn, "w");
   if ( new_fd == NULL ) {
      RTV_ERRLOG("Rotate LogFile fopen error: %s\n      errno=%d=>%s", 
              log_fn, errno, strerror(errno));
      rtv_globals.log_fd = stderr;
      log_to_file        = 0;         
      return;
   }

   fclose(rtv_globals.log_fd);
   rtv_globals.log_fd = new_fd;
   return;
}


void rtvVLog(const char *format, va_list ap)
{
   if (log_to_file) {
      char    tm_buf[255], newfmt[1024];
      time_t  tim = time(NULL);
      char   *ct  = ctime_r(&tim, tm_buf);

      snprintf(newfmt, sizeof(newfmt), "%15.15s %s", &ct[4], format);
      vfprintf(rtv_globals.log_fd, newfmt, ap);

      // See if it is time to rotate the log file
      //
      fflush(rtv_globals.log_fd);
      if ( ++log_count > log_rotate_cnt ) {
         rotate_logs();
      }
   } 
   else {
      vfprintf(rtv_globals.log_fd, format, ap);
   }
   return;
}

int rtv_route_logs(char *filename)
{
   int   rc = 0;
   FILE *tmpfd;

   log_count = 0;
   if ( (rtv_globals.log_fd != NULL) && (rtv_globals.log_fd != stdout) && (rtv_globals.log_fd != stderr) ) {
      fclose(rtv_globals.log_fd);
   }
   if ( strcmp(filename, "stderr") == 0 ) {
      log_to_file        = 0;
      rtv_globals.log_fd = stderr;
   }
   else if ( strcmp(filename, "stdout") == 0 ) {
      log_to_file        = 0;
      rtv_globals.log_fd = stdout;
   } 
   else {
      tmpfd = fopen(filename, "w");
      if ( tmpfd == NULL ) {
         rc = errno;
         RTV_ERRLOG("Failed to open logfile, fopen: %s\n", filename);
         RTV_ERRLOG("       errno=%d=>%s\n", rc, strerror(rc));
         log_to_file        = 0;
         rtv_globals.log_fd = stderr;
      }
      else {
         snprintf(log_fn, PATH_MAX-1, "%s", filename); 
         snprintf(log_backup_fn, PATH_MAX-1, "%s.old", filename);
         RTV_PRT("Sending logs to: %s\n", filename);
         log_to_file        = 1;         
         rtv_globals.log_fd = tmpfd;
      }
   }
   return(rc);
}

