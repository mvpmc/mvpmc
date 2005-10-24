/*
 *  Copyright (C) 2004, 2005, John Honeycutt
 *  http://mvpmc.sourceforge.net/
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

#ident ""

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

