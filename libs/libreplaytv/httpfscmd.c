/*
 *  Copyright (C) 2004, John Honeycutt
 *  Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
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

#ifdef __unix__
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if 0
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <errno.h>

#include "rtv.h"
#include "rtvlib.h"
#include "httpfsclient.h"
#include "bigfile.h"

#define MIN(x,y) ((x)<(y) ? (x) : (y))

static void usage(const char * err)
{
    if (err)
        RTV_ERRLOG("%s\n\n", err);

    RTV_PRT(
"Usage: fs <address> [command <args...>]\n"
"Commands:\n"
"  ls [path]\n"
"  fstat <name>\n"
"  volinfo <name>\n"
"\n"
       );
    return;
}

static int rtv_split_lines(char * src, char *** pdst)
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

static void print_status_bar(FILE * fp, u64 done, u64 total) 
{
   int   percent;
   char  buf[100];
   int   i;
   char *p;
   
   percent = (done*100)/total;
   p = buf;
   *p++ = '[';
   for (i = 0; i < 50; i++) {
      if (i*2 < percent)
         *p++ = '#';
      else
         *p++ = ' ';
   }
   *p++ = ']';
   sprintf(p, "  %"U64F"d/%"U64F"d (%d%%)\r", done, total, percent);
   fprintf(fp, "%s", buf);
}

static int hfs_fstat(const rtv_device_info_t *device, int argc, char **argv)
{
   char * data = NULL;
   unsigned long status;
   
   if (argc != 2) {
      usage("fstat needs one argument");
      return(0);
   }
   status = hfs_do_simple(&data, device,
                          "fstat",
                          "name", argv[1],
                          NULL);
   if (status != 0)
      RTV_ERRLOG("%ld\n", status);
   RTV_PRT("%s", data);
   free(data);
   return status;
}

static int vstrcmp(const void * v1, const void * v2)
{
   return (strcmp(*(char **)v1, *(char **)v2));
}

static const char * format_type(char type) 
{
   switch(type) {
   case 'f': return "-";
   case 'd': return "d";
   default:  return "?";
   }
}

static char * format_perm(char * perm) 
{
   static char rtn[4];
   char * p;
   
   strcpy(rtn, "-- ");
   
   if (strlen(perm) > 3) {
      RTV_ERRLOG("format_perm: unexpectedly long permission string :%s:\n", perm);
      strcpy(rtn, "ERR");
      return rtn;
   }
   
   for (p = perm; *p; p++) {
      switch(*p) {
      case 'r':
         rtn[0] = 'r';
         break;
      case 'w':
         rtn[1] = 'w';
         break;
      default:
         rtn[2] = *p;
         break;
      }
   }
   return rtn;
}

static char * format_time(u64 ttk) 
{
   static char results[256];
   
   time_t tt;
   int msec;
   struct tm * tm;
   
   tt = ttk / 1000;
   msec = ttk % 1000;
   
   tm = localtime(&tt);
   
   strftime(results, sizeof results, "%Y-%m-%d %H:%M:%S", tm);
   
   sprintf(results + 19, ".%03d", msec);
   
   return results;
}

static int ls_l(const rtv_device_info_t *device, const char * dir, const char * filename)
{
   char          pathname[256];
   unsigned long status;
   char *        data;
   char **       lines;
   int           num_lines;
   int           i;
   char          type = '?';
   char *        perm = (char *)"";
   u64           size = 0;
   u64           filetime = 0;
   
   if (strlen(dir) + strlen(filename) + 2 > sizeof pathname) {
      RTV_ERRLOG("ls_l: dir + filename too long: %s/%s\n",
                 dir, filename);
      return -1;
   }
   
   sprintf(pathname, "%s/%s", dir, filename);
   status = hfs_do_simple(&data, device,
                          "fstat",
                          "name", pathname,
                          NULL);
   if (status != 0) {
      RTV_ERRLOG("ls_l %s: Error %ld\n", filename, status);
      return status;
   }
   
   /* should have a higher-level fstat func that does the request
      and parses it.  or at least a common fstat parser */
   num_lines = rtv_split_lines(data, &lines);
   for (i = 0; i < num_lines; i++) {
      if (strncmp(lines[i], "type=", 5) == 0) {
         type = lines[i][5];
      } else if (strncmp(lines[i], "size=", 5) == 0) {
         sscanf(lines[i]+5, "%"U64F"d", &size);
      } else if (strncmp(lines[i], "ctime=", 6) == 0) {
         sscanf(lines[i]+6, "%"U64F"d", &filetime);
      } else if (strncmp(lines[i], "perm=", 5) == 0) {
         perm = lines[i] + 5;
      }
   }
   
   RTV_PRT("%s%s\t%12"U64F"d %s %s\n",
           format_type(type),
           format_perm(perm),
           size,
           format_time(filetime),
           filename);
   
   free(lines);
   free(data);
   
   return 0;
}

static int hfs_ls(const rtv_device_info_t *device, int argc, char ** argv)
{
   char *        dir;
   char *        data;
   char **       files;
   int           i;
   int           num_files;
   unsigned long status;
   
   if ( argc == 2 ) {
      dir = argv[1];
   }
   else {
      dir = (char *)"/";
   }
   
   status = hfs_do_simple(&data, device,
                          "ls",
                          "name", dir,
                          NULL);
   if (status != 0) {
      RTV_ERRLOG("Error %ld\n", status);
      return status;
   }
   
   num_files = rtv_split_lines(data, &files);
   qsort(files, num_files, sizeof(char *), vstrcmp);
   for (i = 0; i < num_files; i++) {
      RTV_PRT("%s\n", files[i]);
   }

   free(files);
   free(data);
   return 0;
}

static u64 get_filesize(const rtv_device_info_t *device, const char * name)
{
   char * data = NULL;
   char ** lines;
   int num_lines;
   int i;
   u64 size;
   unsigned long status;
   
   status = hfs_do_simple(&data, device,
                          "fstat",
                          "name", name,
                          NULL);
   if (status != 0) {
      RTV_ERRLOG("fstat %ld\n", status);
      free(data);
      return -1;
   }
   
   num_lines = rtv_split_lines(data, &lines);
   for (i = 0; i < num_lines; i++) {
      if (strncmp(lines[i], "size=", 5) == 0) {
         sscanf(lines[i]+5, "%"U64F"d", &size);
         free(lines);
         free(data);
         return size;
      }
   }
   free(lines);
   free(data);
   return -1;
}

struct read_data
{
   BIGFILE * dstfile;
   u64 bytes;
   u64 fullsize;
   int verbose;
};

static void hfs_rf_callback(unsigned char * buf, size_t len, void * vd)
{
   struct read_data * rd = vd;
   
   bfwrite(buf, len, 1, rd->dstfile);
   rd->bytes += len;
   switch(rd->verbose) {
   case 2:
      RTV_PRT("Total: %"U64F"d\n", rd->fullsize);
      rd->verbose--;
      /* fallthrough */
   case 1:
      RTV_PRT("Read: %"U64F"d\n", rd->bytes);
      break;
   case 3:
      print_status_bar(stderr, rd->bytes, rd->fullsize);
      break;
   }
}

static int hfs_readfile(const rtv_device_info_t *device, int argc, char ** argv)
{
   struct read_data data;
   unsigned long status;
   const char * pos  = NULL;
   const char * size = NULL;
   const char * file = NULL;
   const char * name;
   int ch;
   u32 delay = 75;
   
   memset(&data, 0, sizeof data);
   data.verbose = 3;
   
   while ((ch = getopt(argc, argv, "d:f:p:s:v:")) != EOF) {
      switch(ch) {
      case 'f':
         file = optarg;
         break;
      case 'p':
         pos = optarg;
         break;
      case 's':
         size = optarg;
         break;
      case 'd':
         delay = atoi(optarg);
         break;
      case 'v':
         data.verbose = atoi(optarg);
         break;
      default:
         usage("Invalid argument to readfile");
         return(0);
         
      }
   }
   
   if (argc - optind != 1) {
      usage("Need to specify exactly one filename for readfile");
      return(0);
   }
   name = argv[optind];
   
   if (!pos)
      pos = "0";
   
   if (file) {
      data.dstfile = bfopen(file, "w");
      if (!data.dstfile) {
         RTV_ERRLOG("bfopen dstfile: %d=>%s", errno, strerror(errno));
         return -1;
      }
   } else {
      data.dstfile = bfreopen(stdout);
   }
   
   if (data.verbose > 1) {
      data.fullsize = get_filesize(device, name);
   }
   
   status = hfs_do_chunked(hfs_rf_callback, &data, device, delay,
                           "readfile",
                           "pos",  pos,
                           "size", size,
                           "name", name,
                           NULL);
   if (status != 0)
      RTV_ERRLOG("Error: %ld\n", status);
   
   return status;
}

static int hfs_volinfo(const rtv_device_info_t *device, int argc, char ** argv)
{
   char * data = NULL;
   unsigned long status;
   
   if ( argc != 2 ) {
      usage("volinfo needs one argument.");
      return(0);
   }
   status = hfs_do_simple(&data, device,
                          "volinfo",
                          "name", argv[1],
                          NULL);
   if (status != 0)
      RTV_ERRLOG("Error %ld\n", status);
   RTV_PRT("%s", data);
   free(data);
   return status;
}

static struct {
   const char * command;
   int (*fn)(const rtv_device_info_t *device, int, char **);
} commands[] = {
   { "fstat",          hfs_fstat       },
   { "ls",             hfs_ls          },
   { "readfile",       hfs_readfile    },
   { "volinfo",        hfs_volinfo     },
};

int rtv_httpfs_cli_cmd(const rtv_device_info_t *devinfo, int argc, char ** argv)
{
   char  *address;
   int    num_commands = sizeof(commands)/sizeof(commands[0]);
   int    r            = -1;
   int    i;
   
   address = devinfo->ipaddr;
   
   if (argc < 3) {   
      usage("subcommand parm required");
      return(0);
   }

   for (i = 0; i < num_commands; i++) {
      if (strcmp(argv[2], commands[i].command) == 0) {
         r = commands[i].fn(devinfo, argc - 2, argv + 2);
         break;
      }
   }
   if (i == num_commands) {
      usage("Invalid command");
      return(0);
   }
   
   return(0);
}
