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

int ciCmdTestFxn(int argc, char **argv);
int ciSetDbgLevel(int argc, char **argv);

#define STRTOHEX(x) strtoul((x), NULL, 16)
#define STRTODEC(x) strtol((x), NULL, 10)

#define MAX_RTVS 10

// Top level replayTV structure
rtv_device_t rtv_top[MAX_RTVS];

static rtv_device_t *get_rtv_device_struct(char* ipaddr, int *new) {
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
static int isaDecNum (const char *str)
{
   char *str2 = NULL;
   
   strtoul(str, &str2, 10);
   return *str2 ? 0 : -1;
}


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

static int ciHttpFs(int argc, char **argv)
{
   rtv_device_t       *rtv;  
   int                 rc, new_entry;

   USAGE("<ip address> <command [args...]>\n"
         "   commands:\n"
         "     ls [path]\n"
         "     fstat <name>\n"
         "     volinfo <name>\n"
         "\n"
      );

   if ( argc < 2 ) {
      printf("Parm Error: <ipaddress> parameter required\n");
      return(0); 
   }

   rtv = get_rtv_device_struct(argv[1], &new_entry);
   if ( new_entry ) {
      printf("\nYou need to get device info first.\n");
      return(0);
   }

   rc = rtv_httpfs_cli_cmd( &(rtv->device), argc, argv);
   return(rc);
}

cmdb_t cmd_list[] = {
   {"setdbgmask", ciSetDbgLevel,   0, MAX_PARMS, "set the debug trace mask"   },
   {"devinfo",    ciGetDeviceInfo, 0, MAX_PARMS, "get RTV device information" },
   {"guide",      ciGetGuide,      0, MAX_PARMS, "get RTV guide"              },
   {"free",       ciFree,          0, MAX_PARMS, "free rtv data struct"       },
   {"fs",         ciHttpFs,        0, MAX_PARMS, "http filesystem cmds"       },
   {"crypttest",  ciCryptTest,     0, MAX_PARMS, "Test encryption routines"   },
   {"clitestfxn", ciCmdTestFxn,    0, MAX_PARMS, "test parameter parsing"     },
   {"",           NULL,            0, 0,         ""                           },
};




int main (int argc, char **argv)
{
   argc=argc; argv=argv;
   rtvShellInit();
   start_cli(cmd_list, "rtv_sh>", "WELCOME TO THE REPLAYTV SHELL");
   return(0);
}
