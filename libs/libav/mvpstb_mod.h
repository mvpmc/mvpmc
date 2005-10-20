/*
 *  Copyright (C) 2005, John Honeycutt
 *  http://mvpmc.sourceforge.net/
 *
 *  Code based on ReplayPC:
 *    Copyright (C) 2002 Matthew T. Linehan and John Todd Larason
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

#define MVPMOD_FINAL_NUM 3

#define MVPMOD_IOC_MAXNR 3

#endif
