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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/errno.h>

#include "shell/cli.h"
#include "rtvlib.h"
#include "bigfile.h"

int ciCmdTestFxn(int argc, char **argv);
int ciSetDbgLevel(int argc, char **argv);

#define STRTOHEX(x) strtoul((x), NULL, 16)
#define STRTODEC(x) strtol((x), NULL, 10)

#define MAX_RTVS 10


// for http fs read file callback
typedef struct get_file_read_data_t
{
   BIGFILE *dstfile;
   __u64    bytes;
   __u64    fullsize;
} get_file_read_data_t;



// Top level replayTV structure
rtv_device_t rtv_top[MAX_RTVS];


static void print_status_bar(FILE * fp, __u64 done, __u64 total) 
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
   sprintf(p, "  %lld/%lld (%d%%)\r", done, total, percent);
   fprintf(fp, "%s", buf);
}

// http fs read file callback
static void hfs_rf_callback(unsigned char * buf, size_t len, void * vd)
{
   get_file_read_data_t *rd = vd;
   
   bfwrite(buf, len, 1, rd->dstfile);
   rd->bytes += len;
   print_status_bar(stderr, rd->bytes, rd->fullsize);
}

static rtv_device_t *get_rtv_device_struct(const char *ipaddr, int *new) {
   int x;

   *new = 0;
   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_top[x].device.ipaddr == NULL ) {
         *new = 1;
         return(&(rtv_top[x])); //New entry
      }
      if ( strcmp(rtv_top[x].device.ipaddr, ipaddr) == 0 ) {
         return(&(rtv_top[x])); //Existing entry
      }
   }
   return(NULL);
}


static void rtvShellInit(void) 
{
   static int shell_initialized = 0;
   if ( shell_initialized == 0 ) {
      memset(rtv_top, 0, sizeof(rtv_top));
      shell_initialized = 1;
   }
}


static int isaHexNum (const char *str)
{
   char *str2 = NULL;
   
   strtoul(str, &str2, 16);
   return *str2 ? 0 : -1;
}
#if 0
static int isaDecNum (const char *str)
{
   char *str2 = NULL;
   
   strtoul(str, &str2, 10);
   return *str2 ? 0 : -1;
}
#endif

int ciCmdTestFxn(int argc, char **argv)
{
   int x;

   USAGE("parm1, parm2, ....\n");

   //x = *testPtr;
   //printf("%d\n", x);
   if ( argc == 1 ) {
      printf("Maybe try entering some parameters...\n");
      return(0);
   }
   
   printf("%s: parameter dump:\n", __FUNCTION__);
   for (x=0;x<argc;x++) {
      printf("  %d:   %s\n", x, argv[x]);
   }
   return(0);
}


int ciSetDbgLevel(int argc, char **argv)
{
   USAGE("<hex debug mask>\n");

   if ( argc !=2 ) {
      printf("Parm Error: single 'debugLevel' parameter required\n");
      return(0); 
   }
   if ( !(isaHexNum(argv[1])) ) {
      printf("Parm Error: Invalid hex mask\n");
      return(-1);
   }
   rtv_set_dbgmask(STRTOHEX(argv[1]));
   printf("New debug mask is:0x%08lx\n", rtv_get_dbgmask());
   return(0);
}

static int ciGetDeviceInfo(int argc, char **argv)
{
   rtv_device_t      *rtv;  
   rtv_device_info_t *devinfo;
   int                rc, new_entry;

   USAGE("<ip address>\n");
   if ( argc !=2 ) {
      printf("Parm Error: single <ipaddress> parameter required\n");
      return(0); 
   }

   rtv = get_rtv_device_struct(argv[1], &new_entry);
   if ( new_entry ) {
      printf("Got New RTV Device Struct Entry\n");
   }
   else {
      printf("Found Existing RTV Device Struct Entry\n");
   }
   devinfo = &(rtv->device);

   if ( (rc = rtv_get_device_info(argv[1], devinfo)) != 0 ) {
      return(rc);
   } 
   rtv_print_device_info(devinfo);
   return(0);
}

static int ciGetGuide(int argc, char **argv)
{
   rtv_device_t       *rtv;  
   rtv_guide_export_t *guide;
   int                 rc, new_entry;

   USAGE("<ip address>\n");
   if ( argc !=2 ) {
      printf("Parm Error: single <ipaddress> parameter required\n");
      return(0); 
   }

   rtv = get_rtv_device_struct(argv[1], &new_entry);
   if ( new_entry ) {
      printf("\nNeed to get device info first.\n");
      if ( (rc = ciGetDeviceInfo(argc, argv)) != 0 ) {
         return(rc);
      }
      rtv = get_rtv_device_struct(argv[1], &new_entry);
      if ( new_entry ) {
         printf("\n\n\nWhat the f&^$. Something is broke bad.\n\n\n");
         return(0);
      }
      printf("\n\n\n");
   }
   
   guide = &(rtv->guide);
   if ( (rc = rtv_get_guide_snapshot( &(rtv->device), NULL, guide)) != 0 ) {
      return(rc);
   } 

   rtv_print_guide(guide);
   return(0);
}

static int ciFree(int argc, char **argv)
{
   int x;
   argc=argc; argv=argv;

   for ( x=0; x < MAX_RTVS; x++ ) {
      if ( rtv_top[x].device.ipaddr != NULL ) {
         rtv_free_device_info(&(rtv_top[x].device));
         rtv_free_guide(&(rtv_top[x].guide));
      }
   }
   return(0);
}

static int ciCryptTest(int argc, char **argv)
{
   argc=argc; argv=argv;
   rtv_crypt_test();
   return(0);
}

static int ciHttpFsVolinfo(int argc, char **argv)
{
   rtv_device_t       *rtv;
   rtv_fs_volume_t   *volinfo;
   int                 rc, new_entry;

   USAGE("<ip address> <volume>\n");

   if ( argc < 3 ) {
      printf("Parm Error: <ipaddress> <volume> parameters required\n");
      return(0); 
   }

   rtv = get_rtv_device_struct(argv[1], &new_entry);
   if ( new_entry ) {
      printf("\nYou need to get device info first.\n");
      return(0);
   }

   rc = rtv_get_volinfo( &(rtv->device), argv[2], &volinfo );
   if ( rc == 0 ) {
      rtv_print_volinfo(volinfo);
      rtv_free_volinfo(&volinfo);
   }
   return(rc);
}

static int ciHttpFsStatus(int argc, char **argv)
{
   rtv_device_t       *rtv;
   int                 rc, new_entry;
   rtv_fs_file_t       fileinfo;

   USAGE("<ip address> <filename>\n");

   if ( argc < 3 ) {
      printf("Parm Error: <ipaddress> <filename>\n");
      return(0); 
   }

   rtv = get_rtv_device_struct(argv[1], &new_entry);
   if ( new_entry ) {
      printf("\nYou need to get device info first.\n");
      return(0);
   }

   rc = rtv_get_file_info( &(rtv->device), argv[2], &fileinfo );
   if ( rc == 0 ) {
      rtv_print_file_info(&fileinfo);
      rtv_free_file_info(&fileinfo);
   }
   return(rc);
}

static int ciHttpFsListFiles(int argc, char **argv)
{
   int                 detailed_listing = 0;
   rtv_device_t       *rtv;
   rtv_fs_filelist_t  *filelist;
   int                 rc, new_entry, ch;
   const char         *ipaddr, *path;


   USAGE("<ip address> <path> [-l]\n");

   optind = 0;
   while ((ch = getopt(argc, argv, "l")) != EOF) {
      switch(ch) {
      case 'l':
         detailed_listing = 1;
         break;
      default:
         printf("Parm error: Invalid argument: -%c\n", ch);
         printf("   Do: \"%s -h\" for help\n", argv[0]);
         return(0);
         
      }
   }

   if ( argc - optind != 2 ) {
      printf("Parm Error: <ipaddress> <path> parameters required\n");
      return(0); 
   }
   ipaddr = argv[optind++];
   path   = argv[optind];

   rtv = get_rtv_device_struct(ipaddr, &new_entry);
   if ( new_entry ) {
      printf("\nYou need to get device info first.\n");
      return(0);
   }

   rc = rtv_get_filelist( &(rtv->device), path, detailed_listing, &filelist );
   if ( rc == 0 ) {
      rtv_print_file_list(filelist, detailed_listing);
      rtv_free_file_list(&filelist);
   }
   return(0);
}

static int ciHttpFsGetFile(int argc, char ** argv)
{
   __u32                 delay        = 75;
   unsigned int          pos_kb       = 0;
   unsigned int          getsize_kb   = 0;
   const char           *ipaddr;
   const char           *f_from;
   const char           *f_to;
   unsigned int          filesize_kb;
   __u64                 pos64;
   __u64                 getsize64;
   get_file_read_data_t  data;
   unsigned long         status;
   rtv_device_t         *rtv;
   rtv_fs_file_t         fileinfo;
   int                   new_entry, rc;
   int                   ch;
   

   USAGE("<ip address> <from_file> <to_file> [options]\n"
   "                      where options are:\n"
   "                         -p <start pos (kb)>\n"
   "                         -s <size (kb)>\n"
   "                         -d <delay (mS)>\n");

   memset(&data, 0, sizeof data);
   optind = 0;
   while ((ch = getopt(argc, argv, "d:p:s:")) != EOF) {
      switch(ch) {
      case 'p':
         pos_kb = atoi(optarg);
         break;
      case 's':
         getsize_kb = atoi(optarg);
         break;
      case 'd':
         delay = atoi(optarg);
         break;
      default:
         printf("Parm error: Invalid argument: -%c\n", ch);
         printf("   Do: \"%s -h\" for help\n", argv[0]);
         return(0);
         
      }
   }
   
   if (argc - optind != 3) {
      printf("Parm error: Need to specify <ip address> <from_file> <to_file>\n");
      printf("   Do: \"%s -h\" for help\n", argv[0]);
      return(0);
   }

   ipaddr = argv[optind++];
   f_from = argv[optind++];
   f_to   = argv[optind];
   
   rtv = get_rtv_device_struct(ipaddr, &new_entry);
   if ( new_entry ) {
      printf("\nYou need to get device info first.\n");
      return(0);
   }

   rc = rtv_get_file_info( &(rtv->device), f_from, &fileinfo );
   if ( rc != 0 ) {
      printf("Error getting info for file: %s:%s\n", ipaddr, f_from);
      return(0);
   }
   data.fullsize = fileinfo.size;
   filesize_kb   = fileinfo.size_k;   
   rtv_print_file_info(&fileinfo);
   rtv_free_file_info(&fileinfo);

   printf("\ngetfile: from=%s:%s to=%s   file_size(KB)=%u\n         options: pos=%u copy_size(KB)=%u delay=%lu(mS)\n\n", 
          ipaddr, f_from, f_to, filesize_kb, pos_kb, getsize_kb, delay);
   printf("getting file...\n\n");
          
   data.dstfile = bfopen(f_to, "w");
   if (!data.dstfile) {
      printf("Error: failed to open dstfile: %s: %d=>%s", f_to, errno, strerror(errno));
      return(0);;
   }
   
   pos64     = pos_kb * 1024;
   getsize64 = getsize_kb *1024;
   status = rtv_read_file(&(rtv->device), f_from, pos64, getsize64, 32, delay, hfs_rf_callback, &data);
   
   if (status != 0) {
      printf("\nError: rtv_read_file: rc=%ld\n\n", status);
   }
   else { 
      printf("\ndone.\n\n"); 
   }
   return(0);;
}

cmdb_t cmd_list[] = {
   {"sdm",        ciSetDbgLevel,     0, MAX_PARMS, "set the debug trace mask"         },
   {"di",         ciGetDeviceInfo,   0, MAX_PARMS, "get RTV device information"       },
   {"guide",      ciGetGuide,        0, MAX_PARMS, "get RTV guide"                    },
   {"free",       ciFree,            0, MAX_PARMS, "free rtv data struct"             },
   {"fsvi",       ciHttpFsVolinfo,   0, MAX_PARMS, "http filesystem: get volume info" },
   {"fsstat",     ciHttpFsStatus,    0, MAX_PARMS, "http filesystem: get file status" },
   {"fsls",       ciHttpFsListFiles, 0, MAX_PARMS, "http filesystem: list directory"  },
   {"fsget",      ciHttpFsGetFile,   0, MAX_PARMS, "http filesystem: get file"        },
   {"crypttest",  ciCryptTest,       0, MAX_PARMS, "Test encryption routines"         },
   {"clitestfxn", ciCmdTestFxn,      0, MAX_PARMS, "test parameter parsing"           },
   {"",           NULL,              0, 0,         ""                                 },
};




int main (int argc, char **argv)
{
   argc=argc; argv=argv;
   rtvShellInit();
   start_cli(cmd_list, "rtv_sh>", "WELCOME TO THE REPLAYTV SHELL");
   return(0);
}
