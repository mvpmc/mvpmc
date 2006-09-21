/*
 *  Copyright (C) 2004, 2005, 2006, John Honeycutt
 *  http://www.mvpmc.org/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>

#include "mvpstb_mod.h"

static int mvpstb_fd = -1;

/*******************************************************************************
   Name:  mvpstb_open
   Description: 
   Return:      
*******************************************************************************/  
static inline int mvpstb_open(void)
{
   if ( mvpstb_fd == -1 ) {
      if ( (mvpstb_fd = open("/dev/mvpstb_dev", O_RDWR)) < 0 ) {
         return -1;
      }
   }
   return 0;
}

/*******************************************************************************
   Name:  kern_read
   Description: 
   Return:      
*******************************************************************************/  
int kern_read(unsigned long memaddr, void *buffaddr, unsigned int size)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.memaddr = (void*)memaddr;
   pdb.res.buff.addr = buffaddr;
   pdb.res.buff.bcount = size;
   
   return(ioctl(mvpstb_fd, MVPMOD_READL, &pdb));
}

/*******************************************************************************
   Name:  kern_write
   Description: 
   Return:      
*******************************************************************************/  
int kern_write(unsigned long memaddr, void *buffaddr, unsigned int size)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.memaddr = (void*)memaddr;
   pdb.res.buff.addr = buffaddr;
   pdb.res.buff.bcount = size;
   
   return(ioctl(mvpstb_fd, MVPMOD_WRITEL, &pdb));
}

/*******************************************************************************
   Name:  dcr_read
   Description: 
   Return:      
*******************************************************************************/  
int dcr_read(unsigned long regaddr, unsigned int *data)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.memaddr = (void*)regaddr;
   pdb.res.buff.addr = data;
   
   return(ioctl(mvpstb_fd, MVPMOD_READ_DCR, &pdb));
}

/*******************************************************************************
   Name:  dcr_write
   Description: 
   Return:      
*******************************************************************************/  
int dcr_write(unsigned long regaddr, unsigned int data)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.memaddr = (void*)regaddr;
   pdb.res.dint = data;
   
   return(ioctl(mvpstb_fd, MVPMOD_WRITE_DCR, &pdb));
}

/*******************************************************************************
   Name:  mvpstb_set_lbox_offset
   Description: Sets the offset for letterboxed output, seems to be in
   		lines per field
   Return:      
*******************************************************************************/  
int mvpstb_set_lbox_offset(unsigned int offset)
{
    return(dcr_write(VIDEO_LETTERBOX_OFFSET,offset));
}

int mvpstb_get_lbox_offset(unsigned int *offset)
{
    return(dcr_read(VIDEO_LETTERBOX_OFFSET,offset));
}

/*******************************************************************************
   Name:  mvpmod_start_audit
   Description: 
   Return:      
*******************************************************************************/  
int mvpmod_start_audit(unsigned long interval_ms)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.info[0] = interval_ms;
   
   return(ioctl(mvpstb_fd, MVPMOD_START_AUDIT, &pdb));
}

/*******************************************************************************
   Name:  mvpmod_stop_audit
   Description: 
   Return:      
*******************************************************************************/  
int mvpmod_stop_audit(void)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }
   
   return(ioctl(mvpstb_fd, MVPMOD_STOP_AUDIT, &pdb));
}

/*******************************************************************************
   Name:  mvpstb_get_vid_stc
   Description: 
   Return:      
*******************************************************************************/  
int mvpstb_get_vid_stc(unsigned long long *vstc)
{
   struct mvpmod_iocrw pdb;
   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.info[0] = TS_VIDEO_STC;
   pdb.res.buff.addr = vstc;
   return(ioctl(mvpstb_fd, MVPMOD_GET_TS, &pdb));
}

/*******************************************************************************
   Name:  mvpstb_get_vid_pts
   Description: 
   Return:      
*******************************************************************************/  
int mvpstb_get_vid_pts(unsigned long long *vpts)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.info[0] = TS_VIDEO_PTS;
   pdb.res.buff.addr = vpts;
   return(ioctl(mvpstb_fd, MVPMOD_GET_TS, &pdb));
}

/*******************************************************************************
   Name:  mvpstb_get_aud_stc
   Description: 
   Return:      
*******************************************************************************/  
int mvpstb_get_aud_stc(unsigned long long *astc)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.info[0] = TS_AUDIO_STC;
   pdb.res.buff.addr = astc;
   return(ioctl(mvpstb_fd, MVPMOD_GET_TS, &pdb));
}

/*******************************************************************************
   Name:  mvpstb_get_aud_pts
   Description: 
   Return:      
*******************************************************************************/  
int mvpstb_get_aud_pts(unsigned long long *apts)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.info[0] = TS_AUDIO_PTS;
   pdb.res.buff.addr = apts;
   return(ioctl(mvpstb_fd, MVPMOD_GET_TS, &pdb));
}

/*******************************************************************************
   Name:  mvpstb_set_video_sync
   Description: 
   Return:      
*******************************************************************************/  
int mvpstb_set_video_sync(int on)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.info[0] = MVPMOD_VIDEO;
   pdb.info[1] = on;
   return(ioctl(mvpstb_fd, MVPMOD_SET_SYNC, &pdb));
}

/*******************************************************************************
   Name:  mvpstb_set_audio_sync
   Description: 
   Return:      
*******************************************************************************/  
int mvpstb_set_audio_sync(int on)
{
   struct mvpmod_iocrw pdb;

   if ( mvpstb_open() ) {
      return -1;
   }

   pdb.info[0] = MVPMOD_AUDIO;
   pdb.info[1] = on;
   return(ioctl(mvpstb_fd, MVPMOD_SET_SYNC, &pdb));
}

int mvpstb_audio_end(void)
{
	int wlr;

	if (dcr_read(AUDIO_DSP_STATUS, &wlr) < 0)
		return -1;

	if (wlr == 0)
		return 1;
	else
		return 0;
}

int mvpstb_video_end(void)
{
	int wlr;

	if (dcr_read(VIDEO_CLIP_WLR, &wlr) < 0)
		return -1;

	if (wlr == 0)
		return 1;
	else
		return 0;
}
