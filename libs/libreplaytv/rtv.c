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
#include "rtv.h"


FILE        *log_fd;
volatile u32 rtv_debug = 0x00000000;
//volatile u32 rtv_debug = 0x100000ff;

//static unsigned int log_rotate_cnt = 8192;
static unsigned int log_rotate_cnt = 512;
static unsigned int log_count      = 0;
static int          log_to_file    = 0;
static char         log_fn[PATH_MAX];
static char         log_backup_fn[PATH_MAX];

int rtv_route_logs(char *filename);


void hex_dump(char * tag, unsigned char * buf, size_t sz)
{
    unsigned int  rows, row, col, i, c;
    unsigned long addr;
    char          tmpstr[512];
    char         *strp = tmpstr;

    RTV_PRT("rtv:HEX DUMP: %s\n", tag);
    rows = (sz + 15)/16;
    for (row = 0; row < rows; row++) {
        addr = (unsigned long)(buf + (row*16));
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
        strp = tmpstr;
    }
}

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
      log_fd      = stderr;
      log_to_file = 0;         
      return;
   }

   fclose(log_fd);
   log_fd = new_fd;
   return;
}


void rtvVLog(const char *format, va_list ap)
{
   if (log_to_file) {
      char    tm_buf[255], newfmt[1024];
      time_t  tim = time(NULL);
      char   *ct  = ctime_r(&tim, tm_buf);

      snprintf(newfmt, sizeof(newfmt), "%15.15s %s", &ct[4], format);
      vfprintf(log_fd, newfmt, ap);

      // See if it is time to rotate the log file
      //
      fflush(log_fd);
      if ( ++log_count > log_rotate_cnt ) {
         rotate_logs();
      }
   } 
   else {
      vfprintf(log_fd, format, ap);
   }
   return;
}

int rtv_route_logs(char *filename)
{
   int   rc = 0;
   FILE *tmpfd;

   log_count = 0;
   if ( (log_fd != NULL) && (log_fd != stdout) && (log_fd != stderr) ) {
      fclose(log_fd);
   }
   if ( strcmp(filename, "stderr") == 0 ) {
      log_to_file = 0;
      log_fd      = stderr;
   }
   else if ( strcmp(filename, "stdout") == 0 ) {
      log_to_file = 0;
      log_fd      = stdout;
   } 
   else {
      tmpfd = fopen(filename, "w");
      if ( tmpfd == NULL ) {
         rc = errno;
         RTV_ERRLOG("Failed to open logfile, fopen: %s\n", filename);
         RTV_ERRLOG("       errno=%d=>%s\n", rc, strerror(rc));
         log_to_file = 0;
         log_fd      = stderr;
      }
      else {
         snprintf(log_fn, PATH_MAX-1, "%s", filename); 
         snprintf(log_backup_fn, PATH_MAX-1, "%s.old", filename);
         RTV_PRT("Sending logs to: %s\n", filename);
         log_to_file = 1;         
         log_fd      = tmpfd;
      }
   }
   return(rc);
}

