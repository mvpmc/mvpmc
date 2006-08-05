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

#ifndef __mvpstb_mod_h_
#define __mvpstb_mod_h_


enum stb_ts_type {
   TS_VIDEO_STC = 1,
   TS_VIDEO_PTS,
   TS_AUDIO_STC,
   TS_AUDIO_PTS
};

enum mvpmod_av {
   MVPMOD_VIDEO = 1,
   MVPMOD_AUDIO
};

/*******************
 * Ioctl definitions
 *******************/

/* Use 'e' as magic number */
#define MVPMOD_IOC_MAGIC  'e'

struct mvpmod_iocrw {
   void *memaddr;
   union {
      unsigned char  dchar;
      unsigned short dshort;
      unsigned int   dint;
      unsigned long  dlong;
      struct buffer_ {
         unsigned int  bcount;
         void         *addr;
      } buff;
   } res;
   unsigned long info[4];
};

#define MVPMOD_READL           _IOWR(MVPMOD_IOC_MAGIC,   1, struct mvpmod_iocrw)
#define MVPMOD_WRITEL          _IOWR(MVPMOD_IOC_MAGIC,   2, struct mvpmod_iocrw)
#define MVPMOD_SETDBGLVL       _IOWR(MVPMOD_IOC_MAGIC,   3, struct mvpmod_iocrw)

#define MVPMOD_READ_DCR        _IOWR(MVPMOD_IOC_MAGIC,   4, struct mvpmod_iocrw)
#define MVPMOD_WRITE_DCR       _IOWR(MVPMOD_IOC_MAGIC,   5, struct mvpmod_iocrw)

#define MVPMOD_GET_TS          _IOWR(MVPMOD_IOC_MAGIC,   6, struct mvpmod_iocrw)
#define MVPMOD_SET_SYNC        _IOWR(MVPMOD_IOC_MAGIC,   7, struct mvpmod_iocrw)

#define MVPMOD_START_AUDIT     _IOWR(MVPMOD_IOC_MAGIC,   8, struct mvpmod_iocrw)
#define MVPMOD_STOP_AUDIT      _IO(MVPMOD_IOC_MAGIC,   9)

#define MVPMOD_FINAL_NUM 9
#define MVPMOD_IOC_MAXNR 9



// STB DCR registers
//
#define VIDEO_DCR_BASE  0x140

#define VIDEO_CHIP_CTRL      VIDEO_DCR_BASE + 0x00
#define VIDEO_SYNC_STC0      VIDEO_DCR_BASE + 0x02
#define VIDEO_SYNC_STC1      VIDEO_DCR_BASE + 0x03
#define VIDEO_SYNC_PTS0      VIDEO_DCR_BASE + 0x04
#define VIDEO_SYNC_PTS1      VIDEO_DCR_BASE + 0x05
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
#define VIDEO_UNKNOWN_3A     VIDEO_DCR_BASE + 0x3a
#define VIDEO_LETTERBOX_OFFSET     VIDEO_DCR_BASE + 0x3b
#define VIDEO_UNKNOWN_3C     VIDEO_DCR_BASE + 0x3c
#define VIDEO_UNKNOWN_3D     VIDEO_DCR_BASE + 0x3d
#define VIDEO_UNKNOWN_3E     VIDEO_DCR_BASE + 0x3e
#define VIDEO_RB_SIZE        VIDEO_DCR_BASE + 0x3f


#define AUDIO_CTRL0               0x1A0
#define AUDIO_CTRL1               0x1A1
#define AUDIO_CTRL2               0x1A2
#define AUDIO_CMD                 0x1A3
#define AUDIO_ISR                 0x1A4
#define AUDIO_IMR                 0x1A5
#define AUDIO_DSR                 0x1A6
#define AUDIO_STC                 0x1A7
#define AUDIO_CSR                 0x1A8
#define AUDIO_QAR2                0x1A9
#define AUDIO_PTS                 0x1AA
#define AUDIO_TONE_GEN_CTRL       0x1AB
#define AUDIO_QLR2                0x1AC
#define AUDIO_ACL_DATA            0x1AD
#define AUDIO_STREAM_ID           0x1AE
#define AUDIO_QAR                 0x1AF

#define AUDIO_DSP_STATUS          0x1B0
#define AUDIO_QLR                 0x1B1
#define AUDIO_DSP_CTRL            0x1B2
#define AUDIO_WLR2                0x1B3
#define AUDIO_IMFD                0x1B4
#define AUDIO_WAR                 0x1B5
#define AUDIO_SEG1                0x1B6
#define AUDIO_SEG2                0x1B7
#define AUDIO_RB_RBF              0x1B8
#define AUDIO_ATN_VAL_FRONT       0x1B9
#define AUDIO_ATN_VAL_REAR        0x1BA
#define AUDIO_ATN_VAL_CENTER      0x1BB
#define AUDIO_SEG3                0x1BC
#define AUDIO_OFFSETS             0x1BD
#define AUDIO_WLR                 0x1BE
#define AUDIO_WAR2                0x1BF


#define VIDEO_CHIP_CTRL_DIS_SYNC     0x00000010

#define AUDIO_CTRL0_ENABLE_SYNC     0x00000200


#endif
