#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/errno.h>
#include "cli.h"

int subCmdTestFxn(int argc, char **argv);
int setDbgLevel(int argc, char **argv);

#define STRTOHEX(x) strtoul((x), NULL, 16)
#define STRTODEC(x) strtol((x), NULL, 10)


static int isaHexNum (const char *str)
{
   char *str2 = NULL;
   
   strtoul(str, &str2, 16);
   return *str2 ? 0 : -1;
}

//static int isaDecNum (const char *str)
//{
//   char *str2 = NULL; 
//   strtoul(str, &str2, 10);
//   return *str2 ? 0 : -1;
//}

int subCmdTestFxn(int argc, char **argv)
{
   int x;

   USAGE("parm1, parm2, ....\n");

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


int setDbgLevel(int argc, char **argv)
{
   USAGE("<hex debug mask>\n");

   if ( argc !=2 ) {
      printf("Parm Error: single 'debugLevel' parameter required\n");
      return(0); 
   }
   if ( !(isaHexNum(argv[1])) ) {
      printf("Parm Error: Invalid debugLevel\n");
      return(-1);
   }  
   printf("New debug mask is:0x%lx\n", STRTOHEX(argv[1]));
   return(0);
}

cmdb_t cmd_list[] = {
   {"setdbgmask", setDbgLevel,   0, MAX_PARMS, "set the debug trace mask" },
   {"clitestfxn", subCmdTestFxn, 0, MAX_PARMS, "test parameter parsing"   },
   {"",           NULL,          0, 0,         ""                         },
};




int main (int argc, char **argv)
{
   argc=argc; argv=argv;
   start_cli(cmd_list, "testsh>", "*** WELCOME !!! ***");
   return(0);
}
