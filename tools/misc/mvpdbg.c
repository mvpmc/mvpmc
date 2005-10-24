/*
 *  Copyright (C) 2005, John Honeycutt
 *  http://mvpmc.sourceforge.net/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*******************************************************************************
                               INCLUDE FILES 
 *******************************************************************************/
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <asm/types.h>
#include "mvp_av.h"

/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/


/*******************************************************************************
 *                         LOCAL CONSTANTS/LITERALS/TYPES 
 ******************************************************************************/
#define MAX_PARMS	20  /* Max number of user parameters */
#define MAX_USRCMDS	40  /* Max number of user commands */
#define MAX_CMDLINE_SZ 200  /* Max number oc chars in cmd line */
#define MAX_HELP_STR 100

#define PRT(fmt, args...) fprintf(stderr, fmt, ## args)
#define STRTOHEX(x) strtoul((x), NULL, 16)
#define STRTODEC(x) strtol((x), NULL, 10)


static struct cmdb 
{
   char *name;
   int (*proc)(int, char **);
   int minargc;
   int maxargc;
   char help[MAX_HELP_STR];
} cmd_list[MAX_USRCMDS];        /* Command list registry */
static int cmdmax = 0;          /* Max commands in command list */

/*******************************************************************************
 *                                LOCAL VARIABLES 
 ******************************************************************************/
#define BUFF_SZ (8096)

/*******************************************************************************
 *                           LOCAL FUNCTIONS/PROTOTYPES 
 ******************************************************************************/

/*******************************************************************************
                                    CODE
 *******************************************************************************/


/*******************************************************************************
 * Function: isaHexNum 
 * 
 * Description: Test if ascii string specifies a hex number.
 *              
 ******************************************************************************/
#if 0
static int isaHexNum (const char *str)
{
   char *str2 = NULL;
   
   strtoul(str, &str2, 16);
   return *str2 ? 0 : -1;
}
#endif
/*******************************************************************************
 * Function: isaDecNum 
 * 
 * Description: Test if ascii string specifies a hex number.
 *              
 ******************************************************************************/
#if 0
static int isaDecNum (const char *str)
{
   char *str2 = NULL;
   
   strtoul(str, &str2, 10);
   return *str2 ? 0 : -1;
}
#endif

/*******************************************************************************
 * Function: tolowerStr 
 * 
 * Description:  
 *              
 ******************************************************************************/
static void tolowerStr(char *str)
{
   while(*str) {
      *str=tolower(*str);
      str++;
   }
}


/*******************************************************************************
 * Function:  my_atocs
 * 
 * Description:  
 *    - Convert "nchars" ascii string to binary data array.
 *    - Returns the number of data bytes converted.
 *    - Forces program exit on non-hex-ascii chars.
 ******************************************************************************/ 
static unsigned long my_atocs (unsigned char *dp, char *p, int nchars, int abort)
{
   unsigned char val;
   char c;
   unsigned char *dp2 = dp;
   
   for (val = 0; nchars > 0; nchars-=2) {
      c = *p++;
      if (!isxdigit(c)) {
         PRT("--HEX ERROR:%02x\n",c);
         if (abort) {
            exit(1);
         } else {
            return (0);
         }
      }
      if (isdigit(c)) {
         val = (val << 4) | (c - '0');
      } else if (isupper(c)){
         val = (val << 4) | (c - 'A' + 10);
      } else {
         val = (val << 4) | (c - 'a' + 10);
      }
      c = *p++;
      if (!isxdigit(c)) {
         PRT("--HEX ERROR:%02x\n",c);
         if (abort) {
            exit(1);
         } else {
            return (0);
         }
      }
      if (isdigit(c)) {
         val = (val << 4) | (c - '0');
      } else if (isupper(c)){
         val = (val << 4) | (c - 'A' + 10);
      } else {
         val = (val << 4) | (c - 'a' + 10);
      }
      *dp2++ = val;
   }
   return dp2 - dp;
}     

/*******************************************************************************
 * Function: print_databuffer 
 * 
 * Description:  
 *              
 ******************************************************************************/
void print_databuffer (char *marker, void *addr, unsigned char *dp, int bcnt)
{
   unsigned long laddr = (unsigned long)addr;
   unsigned long *dlp = (unsigned long *)dp;
   int i, cnt;
               
   cnt = bcnt - (bcnt & 0x03);
   for (i = 0; i < cnt; i += 4, dlp++) {
      if ((i % 16) == 0) {
         PRT("%s%08lx  %08lx", marker, laddr + i, *dlp);
      } else if ((i % 16) == 12) {
         PRT(" %08lx\n", *dlp);
      } else {
         PRT(" %08lx", *dlp);
      }
   }
   dp = (unsigned char *)dlp;
   if (bcnt - cnt) {
      if ((i % 16) == 0) {
         PRT("%s%08x ", marker, i);
      }
      switch (bcnt - cnt) {
      case 1:
         PRT(" %02x\n", *dp);
         break;
      case 2:
         PRT(" %04x\n", *(short *)dp & 0x0000ffff);
         break;
      case 3:
         PRT(" %04x%02x\n", *(short *)dp & 0x0000ffff, *(dp + 2));
         break;
      }
   } else if (i % 16) {
      putchar('\n');
   }
}


// STB DCR registers
//
#define VIDEO_DCR_BASE  0x140


#define VIDEO_CHIP_CTRL      VIDEO_DCR_BASE + 0x00
#define VIDEO_SYNC_STC0      VIDEO_DCR_BASE + 0x02
#define VIDEO_SYNC_STC1      VIDEO_DCR_BASE + 0x03
#define VIDEO_FIFO           VIDEO_DCR_BASE + 0x06
#define VIDEO_FIFO_STAT      VIDEO_DCR_BASE + 0x07
#define VIDEO_CMD_DATA       VIDEO_DCR_BASE + 0x09
#define VIDEO_PROC_IADDR     VIDEO_DCR_BASE + 0x0c
#define VIDEO_PROC_IDATA     VIDEO_DCR_BASE + 0x0d

#define VIDEO_OSD_MODE       VIDEO_DCR_BASE + 0x11
#define VIDEO_HOST_INT       VIDEO_DCR_BASE + 0x12
#define VIDEO_MASK           VIDEO_DCR_BASE + 0x13
#define VIDEO_DISP_MODE      VIDEO_DCR_BASE + 0x14
#define VIDEO_DISP_DLY       VIDEO_DCR_BASE + 0x15
#define VIDEO_OSDI_LINK_ADR  VIDEO_DCR_BASE + 0x1a
#define VIDEO_RB_THRE        VIDEO_DCR_BASE + 0x1b
#define VIDEO_PTS_DELTA      VIDEO_DCR_BASE + 0x1e
#define VIDEO_PTS_CTRL       VIDEO_DCR_BASE + 0x1f

#define VIDEO_UNKNOWN_21     VIDEO_DCR_BASE + 0x21
#define VIDEO_VCLIP_ADR      VIDEO_DCR_BASE + 0x27
#define VIDEO_VCLIP_LEN      VIDEO_DCR_BASE + 0x28
#define VIDEO_BLOCK_SIZE     VIDEO_DCR_BASE + 0x29
#define VIDEO_SRC_ADR        VIDEO_DCR_BASE + 0x2a
#define VIDEO_USERDATA_BASE  VIDEO_DCR_BASE + 0x2b
#define VIDEO_VBI_BASE       VIDEO_DCR_BASE + 0x2c
#define VIDEO_UNKNOWN_2D     VIDEO_DCR_BASE + 0x2d
#define VIDEO_UNKNOWN_2E     VIDEO_DCR_BASE + 0x2e
#define VIDEO_RB_BASE        VIDEO_DCR_BASE + 0x2f

#define VIDEO_DRAM_ADR       VIDEO_DCR_BASE + 0x30
#define VIDEO_CLIP_WAR       VIDEO_DCR_BASE + 0x33
#define VIDEO_CLIP_WLR       VIDEO_DCR_BASE + 0x34
#define VIDEO_SEG0           VIDEO_DCR_BASE + 0x35
#define VIDEO_SEG1           VIDEO_DCR_BASE + 0x36
#define VIDEO_SEG2           VIDEO_DCR_BASE + 0x37
#define VIDEO_SEG3           VIDEO_DCR_BASE + 0x38
#define VIDEO_FRAME_BUF      VIDEO_DCR_BASE + 0x39
#define VIDEO_RB_SIZE        VIDEO_DCR_BASE + 0x3f




static unsigned int dcr_get(unsigned long reg)
{
   int rc;
   unsigned int data;

   rc = dcr_read(reg, &data);
   if ( rc ) {
      printf("ERROR: %s: rc=%d: %s\n", __FUNCTION__, errno, strerror(errno));
      return(0);
   }
   return(data);
}

#define DCR_READ_PRT(x) printf("%-20s (%04x) = 0x%08x\n", ""#x"", x, dcr_get(x))

/*******************************************************************************
 * Function: video_reg_dmp
 * 
 * Description:  
 *              
 ******************************************************************************/
static int video_reg_dmp(int argc, char **argv)
{
   
   DCR_READ_PRT(VIDEO_CHIP_CTRL);
   DCR_READ_PRT(VIDEO_SYNC_STC0);
   DCR_READ_PRT(VIDEO_SYNC_STC1);
   DCR_READ_PRT(VIDEO_FIFO);
   DCR_READ_PRT(VIDEO_FIFO_STAT);
   DCR_READ_PRT(VIDEO_CMD_DATA);
   DCR_READ_PRT(VIDEO_PROC_IADDR);
   DCR_READ_PRT(VIDEO_PROC_IDATA);

   DCR_READ_PRT(VIDEO_OSD_MODE);
   DCR_READ_PRT(VIDEO_HOST_INT);
   DCR_READ_PRT(VIDEO_MASK);
   DCR_READ_PRT(VIDEO_DISP_MODE);
   DCR_READ_PRT(VIDEO_DISP_DLY);
   DCR_READ_PRT(VIDEO_OSDI_LINK_ADR);
   DCR_READ_PRT(VIDEO_RB_THRE);
   DCR_READ_PRT(VIDEO_PTS_DELTA);
   DCR_READ_PRT(VIDEO_PTS_CTRL);
   
   DCR_READ_PRT(VIDEO_UNKNOWN_21);
   DCR_READ_PRT(VIDEO_VCLIP_ADR);
   DCR_READ_PRT(VIDEO_VCLIP_LEN);
   DCR_READ_PRT(VIDEO_BLOCK_SIZE);
   DCR_READ_PRT(VIDEO_SRC_ADR);
   DCR_READ_PRT(VIDEO_VBI_BASE);
   DCR_READ_PRT(VIDEO_UNKNOWN_2D);
   DCR_READ_PRT(VIDEO_UNKNOWN_2E);
   DCR_READ_PRT(VIDEO_RB_BASE);
   
   DCR_READ_PRT(VIDEO_DRAM_ADR);
   DCR_READ_PRT(VIDEO_CLIP_WAR);
   DCR_READ_PRT(VIDEO_CLIP_WLR);
   DCR_READ_PRT(VIDEO_SEG0);
   DCR_READ_PRT(VIDEO_SEG1);
   DCR_READ_PRT(VIDEO_SEG2);
   DCR_READ_PRT(VIDEO_SEG3);
   DCR_READ_PRT(VIDEO_USERDATA_BASE);
   DCR_READ_PRT(VIDEO_RB_SIZE);
   DCR_READ_PRT(VIDEO_FRAME_BUF);

   return(0);
}

/*******************************************************************************
 * Function: mem_ops
 * 
 * Description: 
 *
 * Read Ops: <cmd> <addr>        :Read 32-bit word
 *           <cmd> l <addr>      :Read 32-bit word
 *           <cmd> w <addr>      :Read 16-bit word
 *           <cmd> b <addr>      :Read  8-bit word
 *           <cmd> -<num> <addr> :Read  <num>/4 32-bit words
 *
 * Write Ops: <cmd> <addr>=<val>   :Set 32-bit word at <addr>=<val>
 *            <cmd> l <addr>=<val> :Set 32-bit word at <addr>=<val>
 *            <cmd> w <addr>=<val> :Set 16-bit word at <addr>=<val>
 *            <cmd> b <addr>=<val> :Set  8-bit word at <addr>=<val>
 *
 *            <cmd> -<num> <addr>=<val>,<val>,<val>,... :Set <num>/4 32-bit words
 *                                                       starting at <addr>=val's
 *            <cmd> -<num> <addr>=<hex data string>     :Set <num>/4 32-bit words
 *                                                       starting at <addr>=<hex data string>
 *
 ******************************************************************************/
#define ISGT4HEXBYTE(firstp, nextp)     \
                                (((nextp - firstp) > 10) || (((*firstp != '0') \
                                || (*(firstp + 1) != 'x')) && \
                                ((nextp - firstp) > 8)))        

static int mem_ops(int argc, char **argv)
{
   int argi, ibyte, bytecnt = 0;
   char *start, *end;
   unsigned char *pbuff;
   void *memaddr;
   //unsigned char valc;
   //unsigned short vals;
   unsigned long val;
   int valsize = 'l';
   int rc = 0;

   if ( argc == 1 ) {
      PRT( " Read Ops:\n");
      PRT( "   %s <addr>         :Read 32-bit word\n",argv[0]);
      PRT( "   %s l <addr>       :Read 32-bit word \n",argv[0]);
      PRT( "   %s -<num> <addr>  :Read  <num>/4 32-bit words\n",argv[0]);
      PRT( " Write Ops:\n");
      PRT( "   %s <addr>=<val>   :Set 32-bit word at <addr>=<val>\n",argv[0]);
      PRT( "   %s l <addr>=<val> :Set 32-bit word at <addr>=<val>\n",argv[0]);
      PRT( "   %s -<num> <addr>=<val>,<val>,<val>,... :Set <num>/4 32-bit words\n",argv[0]);
      PRT( "                                           starting at <addr>=val's\n");
      PRT( "   %s -<num> <addr>=<hex data string>     :Set <num>/4 32-bit words\n",argv[0]);
      PRT( "                                           starting at <addr>=<hex data string>\n");
      return(0);
   }

   /*
    * First check everything before executing.
    */
   for (argi = 1; argi < argc; argi++) {
      //PRT("dbg: %d-%s\n", argi, argv[argi]); 
      switch (*(argv[argi])) {
      case '-':
         bytecnt = strtoul(argv[argi] + 1, &end, 10);
         //PRT("bytecnt=%d\n", bytecnt);
         if ((end == (argv[argi] + 1)) || (*end != 0)) {
            PRT(" ERROR ON ARG : %s\n", argv[argi]);
            return(-EINVAL);
         }
         valsize = *(argv[argi]);
         break;
         
      case 'l':
      /*case 'w':*/
      /*case 'b':*/
         if (*(argv[argi] + 1) == 0) {
            valsize = *(argv[argi]);
            bytecnt = 0;
            continue;
         }
         
      default:
         start = argv[argi];
         strtoul(start, &end, 16);
         if ((end == start) || ((*end != '=') && (*end != 0)) ||
             ISGT4HEXBYTE(start, end)) {
            PRT(" ERROR ON ARG : %s\n", argv[argi]);
            return(-EINVAL);
         }
         if (*end++ == '=') {
            start = end;
            strtoul(start, &end, 16);
            if (valsize == '-') {
               if ((end == start) || ((*end != ',') && (*end != 0))) {
                  PRT(" ERROR ON ARG - bad rvalue : %s\n",
                          argv[argi]);
                  return(-EINVAL);
               }
               if (*end == ',') {
                  if (bytecnt % 4) {
                     PRT(" ERROR ON ARG - bad rcnt %d (!x4): %s\n",
                             bytecnt, argv[argi]);
                     return(-EINVAL);
                  }
                  ibyte = 0;
                  while (1) {
                     ibyte += 4;
                     if ((ibyte > bytecnt) || ((*end == 0) &&
                                               (ibyte < bytecnt))) {
                        PRT(" ERROR ON ARG - bad rcnt %d (!=%d): "
                                "%s\n", ibyte / 4, bytecnt / 4,
                                argv[argi]);
                        return(-EINVAL);
                     }
                     if (*end == 0) {
                        break;
                     }
                     start = end + 1;
                     strtoul(start, &end, 16);
                     if ((end == start) || ((*end != ',') && (*end != 0)) ||
                         ISGT4HEXBYTE(start, end)) {
                        PRT(" ERROR ON ARG - bad rvalue (arg %d):"
                                " %s\n", (ibyte / 4) + 1, argv[argi]);
                        return(-EINVAL);
                     }
                  }
               } else if ((bytecnt * 2) != (end - start)) {     
                  PRT(" ERROR ON ARG - bad rlen %d (!=%d): %s\n",
                          end - start, bytecnt * 2, argv[argi]);
               }
            } else {
               if ((start == end) || (*end != 0) || ISGT4HEXBYTE(start, end)) {
                  PRT(" ERROR ON ARG - bad rvalue : %s\n",
                          argv[argi]);
                  return(-EINVAL);
               }
            }
         }
      }
   }
   
   for (argi = 1; argi < argc; argi++) {
      switch (*(argv[argi])) {
      case '-':
         bytecnt = strtoul(argv[argi] + 1, NULL, 10);
         valsize = '-';
         break;
         
      case 'l':
      //case 'w':
      //case 'b':
         if (*(argv[argi] + 1) == 0) {
            valsize = *(argv[argi]);
            bytecnt = 0;
            break;
         }
         
      default:
         memaddr = (void *)strtoul(argv[argi], &end, 16);
         if (*end++ == '=') {
            /* Sets */
            start = end;
            val = strtoul(start, &end, 16);
            switch (valsize) {
            case '-':
               pbuff = (unsigned char*)malloc(bytecnt);
               if (*end == ',') {
                  memcpy(pbuff, &val, 4);
                  start = end + 1;
                  for (ibyte = 4; ibyte < bytecnt; ibyte += 4) {
                     val = strtoul(start, &end, 16);
                     memcpy(pbuff + ibyte, &val, 4);
                     start = end + 1;
                  }
               } else {
                  my_atocs(pbuff, start, bytecnt * 2, 1);
               }
               for (ibyte = 0; ibyte < bytecnt; ibyte += 4) {
                  kern_write((unsigned long)((unsigned char*)memaddr + ibyte),(pbuff + ibyte),sizeof(unsigned int));
               }
               free(pbuff);
               break;
               
            //case 'b':
            //   valc = val;
            //   *(unsigned char *)(memaddr)   = (val);
            //   break;
               
            //case 'w':
            //   vals = val;
            //   *(unsigned short *)(memaddr)   = (val);
            //   break;
               
            case 'l':
               kern_write((unsigned long)memaddr, &val, sizeof(unsigned int));
               break;
            }
            
         } else {
            /* Gets */
            switch (valsize) {
            case '-':
               pbuff = (unsigned char*)malloc(bytecnt);
               memset(pbuff, 0, bytecnt);
               for (ibyte = 0; ibyte < bytecnt; ibyte += 4) {
                  rc = kern_read((unsigned long)((unsigned char*)memaddr + ibyte), (pbuff + ibyte), sizeof(unsigned int));
                  if ( rc != 0 ) {
                     PRT("ERROR: kern_read: errno=%d: %s\n", errno, strerror(errno));
                     break;
                  }
               }
               if ( rc == 0 ) {
                  PRT("Done...bytecnt=%d,memaddr=%p\n",bytecnt,memaddr);
                  print_databuffer("",memaddr, pbuff, bytecnt);
               }
               free(pbuff);
               break;
               
           //case 'b':
           //    valc = (*(unsigned char *)(memaddr));
           //    PRT("%10p  %02x\n", memaddr, valc);
           //   break;
               
           // case 'w':
           //    vals = (*(unsigned short *)(memaddr));
           //    PRT("%10p  %04x\n", memaddr, vals);
           //    break;
               
            case 'l':
               PRT("mem=%p\n",memaddr);
               rc = kern_read((unsigned long)memaddr,&val,sizeof(long));
               if ( rc != 0 ) {
                  PRT("ERROR: kern_read: errno=%d: %s\n", errno, strerror(errno));
               }
               else {
                  PRT("%10p  %08lx\n", memaddr, val);
               }
               break;
            }
         }
      }
   }
   return(0);
}                    


/**************************************************
 ********  CLIENT DEBUG TASK FXNS  **************** 
 **************************************************/

/*******************************************************************************
 * Function: my_help
 * 
 * Description: Debug command help 
 *
 ******************************************************************************/
static int myHelp (int argc, char *argv[])
{
    int icmd;

    for (icmd = 0; icmd < cmdmax; icmd++) {
       PRT(" %-10s: %s\n", cmd_list[icmd].name, cmd_list[icmd].help);
    }
    return 0;
}


/*******************************************************************************
 * Function: processLibRc;
 * 
 * Description: Format/print the returncode 
 *
 ******************************************************************************/
static void processLibRc (int rc)
{
   if (!rc) {
      PRT("rc=0 AOK...\n");
   }
   else {
      PRT("***RtnCode: %d=>%s\n",rc, strerror(abs(rc)));
   } 
   return;
}


/*******************************************************************************
 * Function: interpExport   
 * 
 * Description: Register command line application.
 *
 ******************************************************************************/
static void interpExport (char *cmdName, 
                          int (*proc)(int, char**),
                          int   minargc, 
                          int   maxargc,
                          char *help)
{
   cmd_list[cmdmax].name            = cmdName;
   cmd_list[cmdmax].minargc         = minargc;
   cmd_list[cmdmax].maxargc         = maxargc;
   strncpy(cmd_list[cmdmax].help, help, MAX_HELP_STR);
   cmd_list[cmdmax++].proc = (int (*)(int, char **))proc;
}

/*******************************************************************************
 * Function: initDebug
 * 
 * Description: Register new commands. 
 *
 ******************************************************************************/
static void initDebug (void)
{
   interpExport("mm",          mem_ops,           0, MAX_PARMS, "read/write kernel space");
   interpExport("vdmp",        video_reg_dmp,     0, MAX_PARMS, "dump video registers");
   interpExport("help",        myHelp,            0, 0,         "Display cmds help");
   return;
}



/*******************************************************************************
 * Function:    main
 * 
 * Description: Decode command line and invoke the registered application.
 *
 ******************************************************************************/
int main (int argc, char **argv)
{
   int icmd, rc;

   if (argc == 1) {
      PRT("%s -help :cmdline mode help\n",argv[0]);
      exit(-1);
   }
   
   initDebug();
   tolowerStr(argv[1]);
   if ( (strcmp( argv[1], "-help") == 0) || (strcmp( argv[1], "-h") == 0) ) {
      myHelp(0,NULL);
      return(0);
   }

   for (icmd = 0; icmd < cmdmax; icmd++) {
      if (strcmp(cmd_list[icmd].name, argv[1]) == 0) {
         argc--;
         argv++;
         if (cmd_list[icmd].maxargc) {
            if ((argc < cmd_list[icmd].minargc) ||
                (argc > cmd_list[icmd].maxargc)) {
               PRT("\n Number of Args Mismatch: %d not %d-%d.\n", argc,
                      cmd_list[icmd].minargc, cmd_list[icmd].maxargc);
               return 0;
            }
         }
         if ( (rc = cmd_list[icmd].proc(argc, argv)) ) {
            processLibRc(rc);
         }
         break;
      }
   }
   if (icmd == cmdmax) {
      PRT("\n Command: \"%s\" - not found.\n", argv[1]);
   }
   return 0;
}
