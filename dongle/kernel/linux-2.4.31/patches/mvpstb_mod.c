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

#ifndef __KERNEL__
#  define __KERNEL__
#endif

#ifdef __KERNEL__
#include <linux/modversions.h>
#endif
#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/skbuff.h>
#include <linux/poll.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/pgtable.h>

#include "mvpstb_mod.h"

MODULE_LICENSE("GPL");


/*******************************************************************************
 *                         LOCAL CONSTANTS/LITERALS/TYPES 
 ******************************************************************************/
#define MVPMOD_MAJOR 0
MODULE_PARM(mvpmod_major, "i");
MODULE_PARM(mvpmod_debug, "i");

/*
 * Types
 */
#define HISTORY_SZ (32)
#define HIST_PREVIDX(idx) ((idx-1) & 0x01Fu)
#define HIST_NEXTIDX(idx) ((idx+1) & 0x01Fu)

struct mvpdev_info {
   int  used;
};

typedef struct mvpmod_stb_stats_t {
    unsigned int jiffies;   //timestamp
    unsigned int vb_adr;    //VIDEO_VCLIP_ADR
    unsigned int vb_len;    //VIDEO_VCLIP_LEN
    unsigned int v_read;    //VIDEO_CLIP_WAR
    unsigned int vb_left;   //VIDEO_CLIP_WLR
    unsigned int visr;      //VIDEO_HOST_INT
    unsigned int vptsdelta; //VIDEO_PTS_DELTA
    unsigned int ab_adr;    //AUDIO_QAR
    unsigned int ab_len;    //AUDIO_QLR
    unsigned int a_read;    //AUDIO_WAR
    unsigned int ab_left;   //AUDIO_WLR
    unsigned int aisr;      //AUDIO_ISR    
} mvpmod_stb_stats_t;

typedef struct mvpmod_stb_state_t {
    int hist_idx;
} mvpmod_stb_state_t;

/*
 * Module parms
 */
static int mvpmod_major = MVPMOD_MAJOR;
int        mvpmod_debug = 0;

/*
 * Globals
 */
int        mvpmod_audit_running = 0;
int        mvpmod_audit_rate    = 0;

mvpmod_stb_stats_t mvpmod_hist_buf[HISTORY_SZ];
mvpmod_stb_state_t mvpmod_state;

/*
 * Locals
 */
static struct mvpdev_info *mvp_device = NULL;
static struct timer_list   audit_timer;



/*********** Debug Utils *********/

#define CONSOLE_ALL_LOGS  (0x80000000)

#define MVPLOG_INFO          (mvpmod_debug & 0x00000001)
#define MVPLOG_RW            (mvpmod_debug & 0x00000002)
#define MVPLOG_AUDIT         (mvpmod_debug & 0x00000004)
#define MVPLOG_IOCTL         (mvpmod_debug & 0x00000008)

#define MVP_PRT(cons, fmt, args...) \
        {\
           if (cons || (mvpmod_debug & CONSOLE_ALL_LOGS)) { \
              printk(fmt, ## args); \
           } \
           else { \
              printk(KERN_INFO fmt, ## args); \
           } \
        }

#define MVP_ERRLOG(fmt, args...) MVP_PRT(1, "mvpmod: ERROR:" fmt, ## args)
#define MVP_WARNLOG(fmt, args...) MVP_PRT(0, "mvpmod: WARNING:" fmt, ## args)

#define MVP_DBGLOG(flag, fmt, args...) \
          if (flag) { MVP_PRT(0, "mvpmod:" fmt, ## args); }


#define __MTDCR(regaddr, v)  \
   case regaddr: \
   asm volatile("mtdcr " stringify(regaddr) ",%0" : : "r" (v)); \
   break

#define __mtdcr(regaddr, v)  \
   asm volatile("mtdcr " stringify(regaddr) ",%0" : : "r" (v))

#define __MFDCR(regaddr) \
   case regaddr: \
   asm volatile("mfdcr %0," stringify(regaddr) : "=r" (rval));	\
   break

#define __mfdcr(regaddr) \
  ({unsigned int rval;                                           \
    asm volatile("mfdcr %0," stringify(regaddr) : "=r" (rval));  \
    rval;})

static int mvp_ioctl (struct inode *inode, struct file *filp,
                      unsigned int cmd, unsigned long arg);

static inline int mvp_copyfromdram(void *memaddr, void *buffaddr, unsigned int size);
static inline int mvp_copytodram(void *memaddr, void *buffaddr, unsigned int size);
static int mvp_read_dcr(unsigned long addr, unsigned int *datap);
static int mvp_write_dcr(unsigned long addr, unsigned int data);
static int get_timestamp(int dev, void *datap);
static int sync_cntl(int av, int on);

static void mvpmod_audit_fxn(unsigned long arg);



/*
 * File op structs
 */
static struct file_operations mvp_fops = {
   ioctl:       mvp_ioctl,
};


/*******************************************************************************
   Name:        mvp_ioctl 
   Description:
   Return:      
*******************************************************************************/  
static int mvp_ioctl (struct inode *inode, struct file *filp,
                         unsigned int cmd, unsigned long arg) 
{
   unsigned int         size   = _IOC_SIZE(cmd); // the size bitfield in cmd
   int                  retVal = 0;
   int                  err    = 0;
   struct mvpmod_iocrw  pdb;
   int                  x;
   char                *logP;

   //
   // extract the type and number bitfields, and don't decode
   // wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
   //
   if (_IOC_TYPE(cmd) != MVPMOD_IOC_MAGIC) {
      MVP_ERRLOG("mvp_ioctl - BAD CMD %08x %08lx\n", cmd, arg);
      return(-ENOTTY);
   }
   if (_IOC_NR(cmd) > MVPMOD_IOC_MAXNR) {
      MVP_ERRLOG("mvp_ioctl - BAD CMD %08x %08lx\n", cmd, arg);
      return(-ENOTTY);
   }

   //
   // the direction is a bitmask, and VERIFY_WRITE catches R/W
   // transfers. `Type' is user-oriented, while
   // access_ok is kernel-oriented, so the concept of "read" and
   // "write" is reversed
   //
   if (_IOC_DIR(cmd) & _IOC_READ) {
      err = !access_ok(VERIFY_WRITE, (void *)arg, size);
   }
   else if (_IOC_DIR(cmd) & _IOC_WRITE) {
      err =  !access_ok(VERIFY_READ, (void *)arg, size);
   }
   if (err) {
      MVP_ERRLOG("mvp_ioctl - BAD ADDRESS %08x %08lx\n", cmd, arg);
      return -EFAULT;
   }

   if (_IOC_DIR(cmd) & _IOC_WRITE) {
      // We need to get the parms from user space.
      
      if ( __copy_from_user(&pdb, (void*)arg, size) ) {
         MVP_ERRLOG("mvp_ioctl - parms __copy_from_user failed\n");
         return -EFAULT;
      }
      
      if (MVPLOG_IOCTL) {
         char scratchBuf[1024];

         logP=scratchBuf;
         MVP_PRT(0,"mvp_ioctl:parms byte_dump sz=%d\n", size);
         for(x=0;x<size;x++) {
            logP += sprintf(logP, " %02X", *(((unsigned char*)&(pdb))+x)); 
         }
         MVP_PRT(0,"%s\n", scratchBuf);
      }  
   }

   switch (cmd) {
      case MVPMOD_READL: 
      {
         retVal = mvp_copyfromdram(pdb.memaddr, pdb.res.buff.addr, pdb.res.buff.bcount);
         break;
      }

      case MVPMOD_WRITEL:
      {
         retVal = mvp_copytodram(pdb.memaddr, pdb.res.buff.addr, pdb.res.buff.bcount);
         break;
      }     

      case MVPMOD_READ_DCR: 
      {
         retVal = mvp_read_dcr((unsigned long)pdb.memaddr, pdb.res.buff.addr);
         break;
      }

      case MVPMOD_WRITE_DCR:
      {
         retVal = mvp_write_dcr((unsigned long)pdb.memaddr, pdb.res.dint);
         break;
      }     

      case MVPMOD_SETDBGLVL:
      {
         mvpmod_debug = pdb.info[0];
         break;
      }

      case MVPMOD_GET_TS:
      {
         retVal = get_timestamp(pdb.info[0], pdb.res.buff.addr);
         break;
      }

      case MVPMOD_SET_SYNC:
      {
         retVal = sync_cntl(pdb.info[0], pdb.info[1]);
         break;
      }

      case MVPMOD_START_AUDIT:
      {  
          if( mvpmod_audit_running == 0 ) {
              mvpmod_audit_running = 1;   
              mvpmod_audit_rate    = (pdb.info[0] * HZ) / 1000;
              memset(mvpmod_hist_buf, 0, sizeof(mvpmod_hist_buf));  //zero the history buffer
              mvpmod_state.hist_idx = 0;
              mvpmod_audit_fxn(0);
          }
          else {
              MVP_ERRLOG("%s: MVPMOD_START_AUDIT: audit already started\n", __FUNCTION__);
              retVal = -EPERM;
          }
	 break;
      }

      case MVPMOD_STOP_AUDIT:
      {
         if( mvpmod_audit_running ) {
            mvpmod_audit_running = 0; 
            del_timer(&audit_timer);
         }
         else {
             MVP_ERRLOG("%s: MVPMOD_STOP_AUDIT: audit not running\n", __FUNCTION__);
             retVal = -EPERM;
         }
	 break;
      }

      default:
      {
         break;
         retVal = -ENOSYS;
      }
   }

   return(retVal);
}


/*******************************************************************************
   Name:        mvp_copyfromdram() 
   Description: 
   Return:      
*******************************************************************************/  
static inline int mvp_copyfromdram(void *memaddr, void *buffaddr, unsigned int size)
{
   void          *memend;
   unsigned long  dlong;
   int            offset, ret;
   
   offset = (unsigned long)memaddr % sizeof(long);
   if (offset) {
      return -EFAULT;
   }
   
   memend = memaddr + size;
   if (((unsigned long)buffaddr % sizeof(long)) == 0) {
      for (; memaddr < memend; memaddr += sizeof(long), buffaddr   += sizeof(long)) {
         dlong = in_be32(memaddr);
         ret = __put_user(dlong, (unsigned long *)buffaddr);
         if (ret) {
            return ret;
         }
      }
   } else {
      for (; memaddr < memend; memaddr += sizeof(long), buffaddr += sizeof(long)) {
         dlong = in_be32(memaddr);
         copy_to_user(buffaddr, &dlong, sizeof(long));
      }
   }
   return 0;
}


/*******************************************************************************
   Name:        mvp_copytodram() 
   Description: 
   Return:      
*******************************************************************************/  
static inline int mvp_copytodram(void *memaddr, void *buffaddr, unsigned int size)
{
   void          *memend;
   unsigned long  dlong;
   int            offset, ret;
   
   offset = (unsigned long)memaddr % sizeof(long);
   if (offset) {
      return -EFAULT;
   }

   memend = (void *)memaddr + size;
   if (((unsigned long)buffaddr % sizeof(long)) == 0) {
      for (; memaddr < memend; memaddr += sizeof(long), buffaddr   += sizeof(long)) {
         ret = __get_user(dlong, (unsigned long *)buffaddr);
         if (ret) {
            return ret;
         }
         out_be32(memaddr, dlong);
      }
   } else {
      for (; memaddr < memend; memaddr += sizeof(long), buffaddr += sizeof(long)) {
         copy_from_user(&dlong, buffaddr, sizeof(long));
         out_be32(memaddr, dlong);
      }
   }
   return 0;
}


/*******************************************************************************
   Name:     mvp_read_dcr   
   Description: 
   Return:      
*******************************************************************************/  
static int mvp_read_dcr(unsigned long addr, unsigned int *datap)
{
   unsigned int rval;

   switch (addr) {
      __MFDCR(VIDEO_CHIP_CTRL);
      __MFDCR(VIDEO_SYNC_STC0);
      __MFDCR(VIDEO_SYNC_STC1);
      __MFDCR(VIDEO_FIFO);
      __MFDCR(VIDEO_FIFO_STAT);
      __MFDCR(VIDEO_CMD_DATA);
      __MFDCR(VIDEO_PROC_IADDR);
      __MFDCR(VIDEO_PROC_IDATA);

      __MFDCR(VIDEO_OSD_MODE);
      __MFDCR(VIDEO_HOST_INT);
      __MFDCR(VIDEO_MASK);
      __MFDCR(VIDEO_DISP_MODE);
      __MFDCR(VIDEO_DISP_DLY);
      __MFDCR(VIDEO_OSDI_LINK_ADR);
      __MFDCR(VIDEO_RB_THRE);
      __MFDCR(VIDEO_PTS_DELTA);
      __MFDCR(VIDEO_PTS_CTRL);

      __MFDCR(VIDEO_UNKNOWN_21);
      __MFDCR(VIDEO_VCLIP_ADR);
      __MFDCR(VIDEO_VCLIP_LEN);
      __MFDCR(VIDEO_BLOCK_SIZE);
      __MFDCR(VIDEO_SRC_ADR);
      __MFDCR(VIDEO_VBI_BASE);
      __MFDCR(VIDEO_UNKNOWN_2D);
      __MFDCR(VIDEO_UNKNOWN_2E);
      __MFDCR(VIDEO_RB_BASE);

      __MFDCR(VIDEO_DRAM_ADR);
      __MFDCR(VIDEO_CLIP_WAR);
      __MFDCR(VIDEO_CLIP_WLR);
      __MFDCR(VIDEO_SEG0);
      __MFDCR(VIDEO_SEG1);
      __MFDCR(VIDEO_SEG2);
      __MFDCR(VIDEO_SEG3);
      __MFDCR(VIDEO_USERDATA_BASE);
      __MFDCR(VIDEO_UNKNOWN_3A);
      __MFDCR(VIDEO_LETTERBOX_OFFSET);
      __MFDCR(VIDEO_UNKNOWN_3C);
      __MFDCR(VIDEO_UNKNOWN_3D);
      __MFDCR(VIDEO_UNKNOWN_3E);
      __MFDCR(VIDEO_RB_SIZE);
      __MFDCR(VIDEO_FRAME_BUF);

      __MFDCR(AUDIO_CTRL0);
      __MFDCR(AUDIO_CTRL1);
      __MFDCR(AUDIO_CTRL2);
      __MFDCR(AUDIO_CMD);
      __MFDCR(AUDIO_ISR);
      __MFDCR(AUDIO_IMR);
      __MFDCR(AUDIO_DSR);
      __MFDCR(AUDIO_STC);
      __MFDCR(AUDIO_CSR);
      __MFDCR(AUDIO_QAR2);
      __MFDCR(AUDIO_PTS);
      __MFDCR(AUDIO_TONE_GEN_CTRL);
      __MFDCR(AUDIO_QLR2);
      __MFDCR(AUDIO_ACL_DATA);
      __MFDCR(AUDIO_STREAM_ID);
      __MFDCR(AUDIO_QAR);
      __MFDCR(AUDIO_DSP_STATUS);
      __MFDCR(AUDIO_QLR);
      __MFDCR(AUDIO_DSP_CTRL);
      __MFDCR(AUDIO_WLR2);
      __MFDCR(AUDIO_IMFD);
      __MFDCR(AUDIO_WAR);
      __MFDCR(AUDIO_SEG1);
      __MFDCR(AUDIO_SEG2);
      __MFDCR(AUDIO_RB_RBF);
      __MFDCR(AUDIO_ATN_VAL_FRONT);
      __MFDCR(AUDIO_ATN_VAL_REAR);
      __MFDCR(AUDIO_ATN_VAL_CENTER);
      __MFDCR(AUDIO_SEG3);
      __MFDCR(AUDIO_OFFSETS);
      __MFDCR(AUDIO_WLR);
      __MFDCR(AUDIO_WAR2);

   default:
      MVP_ERRLOG("%s - BAD ADDRESS %08lx\n", __FUNCTION__, addr);
      return -EINVAL;
   }

   copy_to_user(datap, &rval, sizeof(unsigned int));
   return 0;
}


/*******************************************************************************
   Name:     mvp_write_dcr   
   Description: 
   Return:      
*******************************************************************************/  
static int mvp_write_dcr(unsigned long addr, unsigned int data)
{

   switch (addr) {
      __MTDCR(VIDEO_CHIP_CTRL,data);
      __MTDCR(VIDEO_SYNC_STC0,data);
      __MTDCR(VIDEO_SYNC_STC1,data);
      __MTDCR(VIDEO_FIFO,data);
      __MTDCR(VIDEO_FIFO_STAT,data);
      __MTDCR(VIDEO_CMD_DATA,data);
      __MTDCR(VIDEO_PROC_IADDR,data);
      __MTDCR(VIDEO_PROC_IDATA,data);

      __MTDCR(VIDEO_OSD_MODE,data);
      __MTDCR(VIDEO_HOST_INT,data);
      __MTDCR(VIDEO_MASK,data);
      __MTDCR(VIDEO_DISP_MODE,data);
      __MTDCR(VIDEO_DISP_DLY,data);
      __MTDCR(VIDEO_OSDI_LINK_ADR,data);
      __MTDCR(VIDEO_RB_THRE,data);
      __MTDCR(VIDEO_PTS_DELTA,data);
      __MTDCR(VIDEO_PTS_CTRL,data);

      __MTDCR(VIDEO_UNKNOWN_21,data);
      __MTDCR(VIDEO_VCLIP_ADR,data);
      __MTDCR(VIDEO_VCLIP_LEN,data);
      __MTDCR(VIDEO_BLOCK_SIZE,data);
      __MTDCR(VIDEO_SRC_ADR,data);
      __MTDCR(VIDEO_VBI_BASE,data);
      __MTDCR(VIDEO_UNKNOWN_2D,data);
      __MTDCR(VIDEO_UNKNOWN_2E,data);
      __MTDCR(VIDEO_RB_BASE,data);

      __MTDCR(VIDEO_DRAM_ADR,data);
      __MTDCR(VIDEO_CLIP_WAR,data);
      __MTDCR(VIDEO_CLIP_WLR,data);
      __MTDCR(VIDEO_SEG0,data);
      __MTDCR(VIDEO_SEG1,data);
      __MTDCR(VIDEO_SEG2,data);
      __MTDCR(VIDEO_SEG3,data);
      __MTDCR(VIDEO_USERDATA_BASE,data);
      __MTDCR(VIDEO_UNKNOWN_3A,data);
      __MTDCR(VIDEO_LETTERBOX_OFFSET,data);
      __MTDCR(VIDEO_UNKNOWN_3C,data);
      __MTDCR(VIDEO_UNKNOWN_3D,data);
      __MTDCR(VIDEO_UNKNOWN_3E,data);
      __MTDCR(VIDEO_RB_SIZE,data);
      __MTDCR(VIDEO_FRAME_BUF,data);

      __MTDCR(AUDIO_CTRL0,data);
      __MTDCR(AUDIO_CTRL1,data);
      __MTDCR(AUDIO_CTRL2,data);
      __MTDCR(AUDIO_CMD,data);
      __MTDCR(AUDIO_ISR,data);
      __MTDCR(AUDIO_IMR,data);
      __MTDCR(AUDIO_DSR,data);
      __MTDCR(AUDIO_STC,data);
      __MTDCR(AUDIO_CSR,data);
      __MTDCR(AUDIO_QAR2,data);
      __MTDCR(AUDIO_PTS,data);
      __MTDCR(AUDIO_TONE_GEN_CTRL,data);
      __MTDCR(AUDIO_QLR2,data);
      __MTDCR(AUDIO_ACL_DATA,data);
      __MTDCR(AUDIO_STREAM_ID,data);
      __MTDCR(AUDIO_QAR,data);
      __MTDCR(AUDIO_DSP_STATUS,data);
      __MTDCR(AUDIO_QLR,data);
      __MTDCR(AUDIO_DSP_CTRL,data);
      __MTDCR(AUDIO_WLR2,data);
      __MTDCR(AUDIO_IMFD,data);
      __MTDCR(AUDIO_WAR,data);
      __MTDCR(AUDIO_SEG1,data);
      __MTDCR(AUDIO_SEG2,data);
      __MTDCR(AUDIO_RB_RBF,data);
      __MTDCR(AUDIO_ATN_VAL_FRONT,data);
      __MTDCR(AUDIO_ATN_VAL_REAR,data);
      __MTDCR(AUDIO_ATN_VAL_CENTER,data);
      __MTDCR(AUDIO_SEG3,data);
      __MTDCR(AUDIO_OFFSETS,data);
      __MTDCR(AUDIO_WLR,data);
      __MTDCR(AUDIO_WAR2,data);

   default:
      MVP_ERRLOG("%s - BAD ADDRESS %08lx\n", __FUNCTION__, addr);
      return -EINVAL;
   }

   return 0;
}


/*******************************************************************************
   Name:     get_timestamp
   Description: 
   Return:      
*******************************************************************************/  
static int get_timestamp(int dev, void *datap)
{
   unsigned long long data64, tmp64;
   unsigned int data1, data2, data3, data4;
 
   switch ( dev ) {
   case TS_VIDEO_STC:
      data64  = __mfdcr(VIDEO_SYNC_STC0) << 1;
      data64 |= (__mfdcr(VIDEO_SYNC_STC1) >> 9) & 0x01;
      break;
   case TS_VIDEO_PTS:
      data1 = __mfdcr(AUDIO_STC); //dummy read
      data64  = __mfdcr(VIDEO_SYNC_PTS0) << 1;
      data64 |= (__mfdcr(VIDEO_SYNC_PTS1) >> 9) & 0x01;
      break;
   case TS_AUDIO_STC:
      data1 = __mfdcr(AUDIO_STC); //dummy read

      data2 = __mfdcr(AUDIO_STC);
      data3 = __mfdcr(AUDIO_STC);
      data4 = __mfdcr(AUDIO_STC);
      data64  = data2 & 0x0000FFFF;
      data64 |= (data3 & 0x0000FFFF) << 16;
      tmp64 = data4 & 0x00000001;
      data64 |= tmp64 << 32;

      if ( MVPLOG_RW ) {
         MVP_PRT(0,"ASTC: %08X %08X %08X %08X\n", data1, data2, data3, data4);
      }

//      data64  = __mfdcr(AUDIO_STC) & 0x0000FFFF;
//      data64 |= (__mfdcr(AUDIO_STC) & 0x0000FFFF) << 16;
//      data64 |= (__mfdcr(AUDIO_STC) & 0x00000001) << 32;
      break;
   case TS_AUDIO_PTS:
      data1 = __mfdcr(AUDIO_PTS); //dummy read

      data2 = __mfdcr(AUDIO_PTS);
      data3 = __mfdcr(AUDIO_PTS);
      data4 = __mfdcr(AUDIO_PTS);
      data64  = data2 & 0x0000FFFF;
      data64 |= (data3 & 0x0000FFFF) << 16;
      tmp64 = data4 & 0x00000001;
      data64 |= tmp64 << 32;

      if ( MVPLOG_RW ) {
         MVP_PRT(0,"APTS: %08X %08X %08X %08X\n", data1, data2, data3, data4);
      }

//      data64  = __mfdcr(AUDIO_PTS) & 0x0000FFFF;
//      data64 |= (__mfdcr(AUDIO_PTS) & 0x0000FFFF) << 16;
//      data64 |= (__mfdcr(AUDIO_PTS) & 0x00000001) << 32;
      break;
   default:
      MVP_ERRLOG("%s: unknown dev: %d\n", __FUNCTION__, dev);
      return -EINVAL;
   }

   copy_to_user(datap, &data64, sizeof(long long));
   return 0;
}


/*******************************************************************************
   Name:     sync_cntl
   Description: 
   Return:      
*******************************************************************************/  
static int sync_cntl(int av, int on)
{
   unsigned long reg;
   
   MVP_DBGLOG(MVPLOG_INFO, "%s: av=%d on=%d\n", __FUNCTION__, av, on);
   if ( av == MVPMOD_VIDEO ) {
      if ( on ) {
         reg = __mfdcr(VIDEO_CHIP_CTRL);        
         __mtdcr(VIDEO_CHIP_CTRL, reg & (~VIDEO_CHIP_CTRL_DIS_SYNC));
      }
      else {
         reg = __mfdcr(VIDEO_CHIP_CTRL);
         __mtdcr(VIDEO_CHIP_CTRL, reg | VIDEO_CHIP_CTRL_DIS_SYNC);
      }
   }
   else if ( av == MVPMOD_AUDIO ) {
      if ( on ) {
         reg = __mfdcr(AUDIO_CTRL0);
         __mtdcr(AUDIO_CTRL0, reg | AUDIO_CTRL0_ENABLE_SYNC);
      }
      else {         
         reg = __mfdcr(AUDIO_CTRL0);
         __mtdcr(AUDIO_CTRL0, reg & (~AUDIO_CTRL0_ENABLE_SYNC));
      }
   }
   else {
      MVP_ERRLOG("%s: unknown dev: %d\n", __FUNCTION__, av);
      return -EINVAL;
   }
   return 0;
}


/*******************************************************************************
   Name:   mvpmod_audit_fxn
   Description:
   Return:      
*******************************************************************************/  
static void mvpmod_audit_fxn(unsigned long arg)
{
    int idx;
   
    idx = mvpmod_state.hist_idx;

    mvpmod_hist_buf[idx].jiffies = jiffies;

    mvpmod_hist_buf[idx].vb_adr    = __mfdcr(VIDEO_VCLIP_ADR);
    mvpmod_hist_buf[idx].vb_len    = __mfdcr(VIDEO_VCLIP_LEN);
    mvpmod_hist_buf[idx].v_read    = __mfdcr(VIDEO_CLIP_WAR);
    mvpmod_hist_buf[idx].vb_left   = __mfdcr(VIDEO_CLIP_WLR);
    mvpmod_hist_buf[idx].visr      = __mfdcr(VIDEO_HOST_INT);
    mvpmod_hist_buf[idx].vptsdelta = __mfdcr(VIDEO_PTS_DELTA);
    
    mvpmod_hist_buf[idx].ab_adr  = __mfdcr(AUDIO_QAR);
    mvpmod_hist_buf[idx].ab_len  = __mfdcr(AUDIO_QLR);
    mvpmod_hist_buf[idx].a_read  = __mfdcr(AUDIO_WAR);
    mvpmod_hist_buf[idx].ab_left = __mfdcr(AUDIO_WLR);
    mvpmod_hist_buf[idx].aisr    = __mfdcr(AUDIO_ISR);
    
    MVP_DBGLOG(MVPLOG_AUDIT, 
               "(%u) S/A/L/P/R  V: %08x  %08x  %08x  %08x  %08x  [%08x]  A: %08x  %08x  %08x  %08x  %08x\n",
               mvpmod_hist_buf[idx].jiffies, 
               mvpmod_hist_buf[idx].visr, 
               mvpmod_hist_buf[idx].vb_adr, 
               mvpmod_hist_buf[idx].vb_len, 
               mvpmod_hist_buf[idx].v_read, 
               mvpmod_hist_buf[idx].vb_left, 
               mvpmod_hist_buf[idx].vptsdelta,
               mvpmod_hist_buf[idx].aisr, 
               mvpmod_hist_buf[idx].ab_adr, 
               mvpmod_hist_buf[idx].ab_len, 
               mvpmod_hist_buf[idx].a_read, 
               mvpmod_hist_buf[idx].ab_left);
    
    mvpmod_state.hist_idx = HIST_NEXTIDX(mvpmod_state.hist_idx);
    
    init_timer(&audit_timer);
    audit_timer.function = mvpmod_audit_fxn;
    audit_timer.data     = 0;
    audit_timer.expires  = jiffies + mvpmod_audit_rate;
    add_timer(&audit_timer);
}

/*******************************************************************************
   Name:        __initfunc
   Description: module registration.
   Return:      
*******************************************************************************/  
int  __init mvp_init(void)
{
   int result;

   result = register_chrdev(mvpmod_major, "mvpstb", &mvp_fops);
   if ( result < 0 ) {
      MVP_ERRLOG("unable to get major %d for mvp devices\n",
               mvpmod_major);
      return result;
   }
   if (mvpmod_major == 0) {
      mvpmod_major = result; // dynamic
   }
   MVP_DBGLOG(MVPLOG_INFO, "The major number is %d\n",mvpmod_major);

   return 0;
}

#ifdef MODULE
/*******************************************************************************
   Name:        init_module 
   Description: Init MVP module
   Return:      
*******************************************************************************/  
int init_module (void)
{
   MVP_DBGLOG(MVPLOG_INFO,"Installing MVP STB Module \n");
   return( mvp_init() );
}

/*******************************************************************************
   Name:        cleanup_module 
   Description: MVP module removal
   Return:      
*******************************************************************************/  
void cleanup_module (void)
{
   MVP_DBGLOG(MVPLOG_INFO,"Removing MVP STB Module\n");
   
   if ( mvpmod_audit_running ) {
       del_timer(&audit_timer);
   }

   if ( mvp_device != NULL ) {
      kfree(mvp_device);
   }
   unregister_chrdev(mvpmod_major, "mvpstb");
}
#endif
