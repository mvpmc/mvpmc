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
#include "../../mvplib/libav/mvpstb_mod.h"

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
 * Function: audio_reg_dmp
 * 
 * Description:  
 *              
 ******************************************************************************/
static int audio_reg_dmp(int argc, char **argv)
{
   
   DCR_READ_PRT(AUDIO_CTRL0);
   DCR_READ_PRT(AUDIO_CTRL1);
   DCR_READ_PRT(AUDIO_CTRL2);
   DCR_READ_PRT(AUDIO_CMD);
   DCR_READ_PRT(AUDIO_ISR);
   DCR_READ_PRT(AUDIO_IMR);
   DCR_READ_PRT(AUDIO_DSR);
   DCR_READ_PRT(AUDIO_STC);
   DCR_READ_PRT(AUDIO_CSR);
   DCR_READ_PRT(AUDIO_QAR2);
   DCR_READ_PRT(AUDIO_PTS);
   DCR_READ_PRT(AUDIO_TONE_GEN_CTRL);
   DCR_READ_PRT(AUDIO_QLR2);
   DCR_READ_PRT(AUDIO_ACL_DATA);
   DCR_READ_PRT(AUDIO_STREAM_ID);
   DCR_READ_PRT(AUDIO_QAR);
   DCR_READ_PRT(AUDIO_DSP_STATUS);
   DCR_READ_PRT(AUDIO_QLR);
   DCR_READ_PRT(AUDIO_DSP_CTRL);
   DCR_READ_PRT(AUDIO_WLR2);
   DCR_READ_PRT(AUDIO_IMFD);
   DCR_READ_PRT(AUDIO_WAR);
   DCR_READ_PRT(AUDIO_SEG1);
   DCR_READ_PRT(AUDIO_SEG2);
   DCR_READ_PRT(AUDIO_RB_RBF);
   DCR_READ_PRT(AUDIO_ATN_VAL_FRONT);
   DCR_READ_PRT(AUDIO_ATN_VAL_REAR);
   DCR_READ_PRT(AUDIO_ATN_VAL_CENTER);
   DCR_READ_PRT(AUDIO_SEG3);
   DCR_READ_PRT(AUDIO_OFFSETS);
   DCR_READ_PRT(AUDIO_WLR);
   DCR_READ_PRT(AUDIO_WAR2);
   
   return(0);
}


/*******************************************************************************
 * Function: dmp_avpos
 * 
 * Description:  
 *              
 ******************************************************************************/
static int  dmp_avpos(int argc, char **argv)
{
   unsigned int vb_adr, vb_len, v_read, vb_left, visr;
   unsigned int ab_adr, ab_len, a_read, ab_left, aisr;
   
   vb_adr  = dcr_get(VIDEO_VCLIP_ADR);
   vb_len  = dcr_get(VIDEO_VCLIP_LEN);
   v_read  = dcr_get(VIDEO_CLIP_WAR);
   vb_left = dcr_get(VIDEO_CLIP_WLR);
   visr    = dcr_get(VIDEO_HOST_INT);

   ab_adr  = dcr_get(AUDIO_QAR);
   ab_len  = dcr_get(AUDIO_QLR);
   a_read  = dcr_get(AUDIO_WAR);
   ab_left = dcr_get(AUDIO_WLR);
   aisr     = dcr_get(AUDIO_ISR);
   
//   printf("BuffAddr(V/A):     %08x    %08x\n", vb_adr, ab_adr);
//   printf("BuffLen(V/A):      %08x    %08x\n", vb_len, ab_len);
//   printf("Read(V/A):         %08x    %08x\n", v_read, a_read);
//   printf("BuffLeft(V/A):     %08x    %08x\n", vb_left, ab_left);
   printf("S/A/L/P/R:       %08x  %08x  %08x  %08x  %08x    %08x  %08x  %08x  %08x  %08x\n",
          visr, vb_adr, vb_len, v_read, vb_left, aisr, ab_adr, ab_len, a_read, ab_left);
   return(0);
}



/*******************************************************************************
 * Function: dmp_videopos
 * 
 * Description:  
 *              
 ******************************************************************************/
static int  dmp_videopos(int argc, char **argv)
{
   unsigned int vb_adr, vb_len, v_read, vb_left, isr;
   
   isr    = dcr_get(VIDEO_HOST_INT);
   vb_adr  = dcr_get(VIDEO_VCLIP_ADR);
   vb_len  = dcr_get(VIDEO_VCLIP_LEN);
   v_read  = dcr_get(VIDEO_CLIP_WAR);
   vb_left = dcr_get(VIDEO_CLIP_WLR);
   
   printf("S/A/L/P/R:       %08x    %08x  %08x  %08x  %08x\n",
          isr, vb_adr, vb_len, v_read, vb_left);
   return(0);
}

/*******************************************************************************
 * Function: dmp_audiopos
 * 
 * Description:  
 *              
 ******************************************************************************/
static int  dmp_audiopos(int argc, char **argv)
{
   unsigned int ab_adr, ab_len, a_read, ab_left, isr;
   
   isr     = dcr_get(AUDIO_ISR);
   ab_adr  = dcr_get(AUDIO_QAR);
   ab_len  = dcr_get(AUDIO_QLR);
   a_read  = dcr_get(AUDIO_WAR);
   ab_left = dcr_get(AUDIO_WLR);
   
   printf("S/A/L/P/R:       %08x    %08x  %08x  %08x  %08x\n",
          isr, ab_adr, ab_len, a_read, ab_left);
   return(0);
}

/*******************************************************************************
 * Function: get_video_ts
 * 
 * Description:  
 *              
 ******************************************************************************/
static int  get_ts(int argc, char **argv)
{
   unsigned int mask, hour, minute, second, ms;
   unsigned long long ts;

   if ( argc == 1 ) {
      PRT( "   %s  <vid_stc=1 | vid_pts=2 | aud_stc=4 | aud_pts=8>\n",argv[0]);
      return(0);
   }

   mask = STRTOHEX(argv[1]);
   
   if ( mask & 0x01 ) {
      mvpstb_get_vid_stc(&ts);
      printf("vid_stc=%09llX ", ts);
		second = ts / PTS_HZ;
		hour = second / 3600;
		minute = second / 60 - hour * 60;
		second = second % 60;
      ms = (ts / ( PTS_HZ / 1000 )) % 1000; 
      printf("(%02d:%02d:%02d.%03d)\n", hour, minute, second, ms);
   }

   if ( mask & 0x02 ) {
      mvpstb_get_vid_pts(&ts);
      printf("vid_pts=%09llX ", ts);
		second = ts / PTS_HZ;
		hour = second / 3600;
		minute = second / 60 - hour * 60;
		second = second % 60;
      ms = (ts / ( PTS_HZ / 1000 )) % 1000; 
      printf("(%02d:%02d:%02d.%03d)\n", hour, minute, second, ms);
   }

   if ( mask & 0x04 ) {
      mvpstb_get_aud_stc(&ts);
      printf("aud_stc=%09llX ", ts);
		second = ts / PTS_HZ;
		hour = second / 3600;
		minute = second / 60 - hour * 60;
		second = second % 60;
      ms = (ts / ( PTS_HZ / 1000 )) % 1000; 
      printf("(%02d:%02d:%02d.%03d)\n", hour, minute, second, ms);
   }
      
   if ( mask & 0x08 ) {
      mvpstb_get_aud_pts(&ts);
      printf("aud_pts=%09llX ", ts);
		second = ts / PTS_HZ;
		hour = second / 3600;
		minute = second / 60 - hour * 60;
		second = second % 60;
      ms = (ts / ( PTS_HZ / 1000 )) % 1000; 
      printf("(%02d:%02d:%02d.%03d)\n", hour, minute, second, ms);
   }
   return(0);
}


/*******************************************************************************
 * Function: set_sync
 * 
 * Description:  
 *              
 ******************************************************************************/
static int  set_sync(int argc, char **argv)
{
   int on;

   if ( argc < 3 ) {
      PRT( "   %s  <a|v> <1=on|0=off>\n",argv[0]);
      return(0);
   }

   on = STRTODEC(argv[2]);
   if ( tolower(argv[1][0]) == 'a' ) {
      mvpstb_set_audio_sync(on);
   }
   else if ( tolower(argv[1][0]) == 'v' ) {
      mvpstb_set_video_sync(on);
   }
   else {
      PRT( "   %s  <a|v> <1=on|0=off>\n",argv[0]);
   }
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
   interpExport("admp",        audio_reg_dmp,     0, MAX_PARMS, "dump audio registers");
   interpExport("avp",         dmp_avpos,         0, MAX_PARMS, "Audio/Video position registers");
   interpExport("ap",          dmp_audiopos,      0, MAX_PARMS, "Audio position registers");
   interpExport("vp",          dmp_videopos,      0, MAX_PARMS, "Video position registers");
   interpExport("gts",         get_ts,            0, MAX_PARMS, "Get timestamps: stc/pts");
   interpExport("setsync",     set_sync,          0, MAX_PARMS, "Enable/disable audio/video sync");
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
