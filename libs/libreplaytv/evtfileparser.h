/*
 *  Copyright (C) 2005, John Honeycutt
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

#ifndef __EVTFILEPARSER_H__
#define __EVTFILEPARSER_H__


typedef enum { 
   EVT_AUD = 1, 
   EVT_VID = 2 
} evt_rec_type_t;


typedef struct _fade_pt
{
   struct _fade_pt *next;
   __u32            start;
   __u32            stop;
   __u32            video;
   __u32            audio;
   __u32            type;
} fade_pt_t;

#endif
