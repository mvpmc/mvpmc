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

/*******************************************************************************************************
 * NOTE: This code is based on the VideoLAN project's replaytv patch by Laurent Aimar <fenrir@via.ecp.fr>
 *       The VLC code was based on the RTV5K tools evtdump.c code byLee Thompson <thompsonl@logh.net>
 *******************************************************************************************************/


#include <stdlib.h>
#include <string.h>

#include "rtv.h"
#include "rtvlib.h"
#include "evtfileparser.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/


int RTV_DEBUG = 0;


//+*********************************************************************
//  Name: convert_evtbuf_recs
//+*********************************************************************
static void convert_evtbuf_recs(rtv_evt_record_t *recs, int num_recs)
{ 
   int x;
   for ( x=0; x < num_recs; x++ ) {
      rtv_convert_evt_rec(&(recs[x]));
   }
}

//+*********************************************************************
//  Name: normalize
//        find range to normalize the audio
//  Returns: audmin_p, audfactor_p
//+*********************************************************************
static void normalize(rtv_evt_record_t *recs, int num_recs, __u32 *audmin_p, __u32 *audfactor_p)
{
   __u32 audmax = 0;
   __u32 audmin = 0xFFFFFFFF;
   __u32 factor = 0;
   int   x;

   for ( x=0; x < num_recs; x++ ) {
      if (recs[x].data_type == EVT_AUD) {
         if (recs[x].audiopower > audmax) audmax = recs[x].audiopower;
         if (recs[x].audiopower < audmin) audmin = recs[x].audiopower;
      }
   }

   // audio multiplier
   //
   if (audmax) {
      factor = 10000 / audmax;
   }
   if (!factor) {
      factor = 1;
   }

   RTV_DBGLOG(RTVLOG_EVTFILE, "Scale factor = %lu (%lu, %lu)\n", factor, audmin, audmax);
   audmin = (audmin + ((audmax - audmin)/10)) * factor;
   RTV_DBGLOG(RTVLOG_EVTFILE, "Threshold = %lu\n", audmin);

   *audmin_p    = audmin;
   *audfactor_p = factor;
   return;
}


// From evtdump.c (rtvtools)
static char* format_ts(__s64 ts, char *time)
{
 __u32 ms = (__u32)(ts / 1000000);
 __u32 sec = ms / 1000;
 __u32 min = sec / 60;

   sec -= (min*60);
   ms %= 1000;
   sprintf (time, "%03d:%02d.%03d", (int)min, (int)sec, (int)ms);
   return time;
}
static char* format_ts2(__s64 ts, char *time)
{
 __u32 ms = (__u32)(ts / 1000000);
 __u32 sec = ms / 1000;
 __u32 min = sec / 60;

   sec -= (min*60);
   sprintf (time, "%02d:%02d", (int)min, (int)sec);
   return time;
}
static char* format_ts3(__u64 ts, char *time)
{
 __u32 ms = (__u32)(ts / 1000000);
 __u32 sec = ms / 1000;
 __u32 min = sec / 60;
 __u32 hour = min / 60;
 __u32 minlim = 60;

   if( min < minlim ) {
      char tt[16];
      return(format_ts2(ts,tt));
   }

   sec -= (min*60);
   min -= (hour*60);
   sprintf (time, "%02d:%02d:%02d", (int)hour, (int)min, (int)sec);
   return time;
}

static char* vid_level(__u32 level)
{
   static char vid[10];
   int i;
   
   level = (level + 7) / 8;
   for (i=0; i<9; i++) {
      if (level) {
         level--;
         vid[i] = '#';
      }
      else vid[i] = ' ';
   }
   vid[9] = 0;
   return vid;
}

static char* aud_level(__u32 level)
{
   static char aud[50];
   int i;
   memset(aud,0,50);
   level = (level + 199) / 200;
   for (i=0; i<49; i++) {
      if (level) {
         level--;
         aud[i] = '=';
      }
      else aud[i] = ' ';
   }
   aud[49] = 0;
   return aud;
}


//+***********************************************************************************************
//  Name: rtv_parse_evt_file
//  Processes evt file records and builds list of chapter segments.
//+***********************************************************************************************
int rtv_parse_evt_file( rtv_chapter_mark_parms_t evtfile_parms, rtv_chapters_t *chapter_struct)
{
   unsigned int  progseg_min = evtfile_parms.p_seg_min; //minimum program segment time. (sec)
   unsigned int  scene_min   = evtfile_parms.scene_min; //minimum scene time (sec)
   unsigned int  evtfile_sz  = evtfile_parms.buf_sz;    //evt file buffer size
   char         *evtfile_buf = evtfile_parms.buf;       //evt file buffer

   rtv_evt_record_t *evt_recs; //array of evt recs
   int               num_recs; //number of evt records
   __u32             audio_min;
   __u32             audio_factor;

   // Setup & file error checking.
   //
   chapter_struct->num_chapters = 0;
   chapter_struct->chapter      = NULL;

   if ( evtfile_sz < (RTV_EVT_HDR_SZ + sizeof(rtv_evt_record_t)) ) {
      RTV_WARNLOG("No records in EvtFile: sz=%d\n", evtfile_sz);
      return(-1);
   }
   evtfile_buf += RTV_EVT_HDR_SZ;
   evtfile_sz  -= RTV_EVT_HDR_SZ;
   
   if ( evtfile_sz % sizeof(rtv_evt_record_t) ) {
      RTV_WARNLOG("EvtFile size not a multiple of EvtRec size: sz=%d\n", evtfile_sz);
      return(-1);
   }

   evt_recs = (rtv_evt_record_t*)evtfile_buf;
   num_recs = evtfile_sz / sizeof(rtv_evt_record_t);
   RTV_DBGLOG(RTVLOG_EVTFILE, "number of evt recs: %d\n", num_recs);
   
   // Do endian conversion on records, & calculate the audio threshold.
   //
   convert_evtbuf_recs(evt_recs, num_recs);
   normalize(evt_recs, num_recs, &audio_min, &audio_factor);


   return(0);
}



#if 0
int rtv_parse_evt_filexxx( unsigned char *evtbuf, long int evtSize, struct evt5kStruct ***s, int *evtcount)
{
   EVT_EVENT   event;
   __u32      lastvid = 256;
   __u32      lastaud = 10000;
   __s64    lasttime, lastvidtime, lastaudtime;
   FADE*    current;
   FADE*    root;
   long int size=0;
   int commercial_start=0, i, j;
   __s64 last_added=0;
   char t1[16], t2[16];
   __s64 addpoint[20];
   int addcount = 0;
   
   // Normalize audio range
   normalize(evtbuf, evtSize);
   
   lasttime = lastvidtime = lastaudtime = 0;
   
   root = current = (FADE*)malloc(sizeof(FADE));
   memset(current, 0, sizeof(FADE));
   
   // First pass: Create a list of valid fade points within the event groups.
   // We don't care what these fade points are yet.
   evtbuf += 8;         // skip header
   while( size <= evtSize ) {
      memcpy(&event, evtbuf, 24);
      size += 24;
      evtbuf += 24;
      event.timestamp   = SWAP64(event.timestamp);
      event.type  = SWAP32(event.type);
      event.value1   = SWAP32(event.value1);
      event.value2   = SWAP32(event.value2);
      
      //event.timestamp   = rtv_to_u64(&evtbuf);
      //event.type        = rtv_to_u32(&evtbuf);
      //event.value1      = rtv_to_u32(&evtbuf);
      //event.value2      = rtv_to_u32(&evtbuf);
      //memcpy(&(event.color), evtbuf, 4); evtbuf += 4;
      //size += 24;
      
      if (!lasttime || (factor == 1 && (event.timestamp - lasttime) > (__s64)1000000000)) {
         // re-use node if not used
         if (current->start) {
            if (!current->stop) current->stop = lasttime;
            current->next = (FADE*)malloc(sizeof(FADE));
            current = current->next;
            memset(current, 0, sizeof(FADE));
         }
         current->stop = 0;
         current->video = 256;
         current->audio = 10000;
         lastvid = 256;
         lastaud = 10000;
      }
      if (event.type == EVT_AUD) {
         if ((event.timestamp - lastvidtime) > 70000000) lastvid = 256;
         lastaud = event.value1 * factor;
         lastaudtime = event.timestamp;
      }
      if (event.type == EVT_VID) {
         int i, divisor = 0;
         if ((event.timestamp - lastaudtime) > 50000000) lastaud = 10000;
         lastvid = 0;
         for (i=0; i<4; i++) {
            if (event.color[i]) {
               lastvid += event.color[i];
               divisor++;
            }
         }
         if (divisor) lastvid /= divisor;
         lastvidtime = event.timestamp;
      }
      if ((factor > 1 && ((!current->start && lastaud < audmin) || (current->start && lastaud >= audmin)))) {
         // re-use node if not used
         if (current->start) {
            if ((event.timestamp - current->start) > 1000000000) {
               if (!current->stop) current->stop = lasttime;
               current->next = (FADE*)malloc(sizeof(FADE));
               current = current->next;
               memset(current, 0, sizeof(FADE));
            }
         }
         current->stop = 0;
         current->video = 256;
         current->audio = 10000;
      }
      // sometimes we see errant past times at the end of the evt,
      // filter them out
      if (event.timestamp >= lasttime) {
         lasttime = event.timestamp;
         // A fade is only valid if BOTH vid and aud fall below
         // our thresholds.
         if (lastvid < 32) {
            if (lastaud < audmin) {
               if (!current->start) {
                  current->start = lasttime;
                  current->video = lastvid;
                  current->audio = lastaud;
               }
               else if (current->stop) {
                  current->start = lasttime;
                  current->video = lastvid;
                  current->audio = lastaud;
                  current->stop = 0;
               }
            }
            else if (current->start && !current->stop) {
               current->stop = lasttime;
            }
         }
         else if ((lastvid > 200) && current->start && !current->stop) {
            current->stop = lasttime;
         }
      }
   }
   
   // Second pass: Walk the list of fade points and see if anything falls out.
   // For now, just look for time between fades, if it's > 5 minutes it's likely
   // to be a program segment, else it's likely to be a commercial segment.  if
   // there is a fade in the first minute, the recording may have started on a
   // commercial.  nothing fancy.
   
   current = root;
   lasttime = 0;
   (*evtcount) = 0;
   
   while (current) {
      if (current->start) {
         __s64 spottime;
         // use the middle value of the fade range as the edit time
         current->stop = (current->stop + current->start)/2;
         spottime = current->stop - lasttime;
         if (!lasttime || spottime >= gMinTime) {
            // if it's less than gProgTime, we want to add the edit
            if (lasttime && spottime < gProgTime) {
               current->type = 1;
            }
            // if it's the first one in the first minute, we want to add
            // the edit
            if (!lasttime && spottime < (__s64)60000000000LL) {
               // 1 min
               current->type = 1;
            }
            else lasttime = current->stop;
            
            if(RTV_DEBUG) {
               fprintf(stderr,"%c ", current->type ? 'A' : 'D');
               fprintf(stderr,"%s\n", format_ts(current->stop,t1));
            }
            
            if( current->type ) {
               last_added = current->stop;
               if(last_added) {
                  if(addcount > 20) {
                     addpoint[0] = addpoint[19];
                     addpoint[1] = addpoint[20];
                     addcount = 2;
                  }
                  addpoint[addcount] = last_added;
                  addcount++;
               }
            } else {
               commercial_start = 1;
            }
            
            if( commercial_start ) {
               if( (*evtcount) == 0 ) {
                  (*s) = (struct evt5kStruct **)malloc(
                     sizeof(struct evt5kStruct *)
                     );
               } else {
                  (*s) = (struct evt5kStruct **)realloc(
                     (*s), sizeof(struct evt5kStruct *)*((*evtcount)+1)
                     );
               }
               (*s)[(*evtcount)] = (struct evt5kStruct *)malloc(
                  sizeof(struct evt5kStruct)
                  );
               (*s)[(*evtcount)]->start = current->stop;
               if( last_added && (*evtcount) > 0 ) {
                  (*s)[(*evtcount)-1]->stop = last_added;
               }
               (*evtcount)++;
               commercial_start = 0;
            }
         }
         else lasttime = current->stop;
      }
      current = current->next;
   }
   
   if( (*evtcount) > 0 ) {
      (*s)[(*evtcount)-1]->stop = lasttime;
      if( last_added > (*s)[(*evtcount)-1]->start )
         (*s)[(*evtcount)-1]->stop = last_added;
   }
   // Throw out last evt point if it's bogus (start time < previous stop time)
   if( (*evtcount) > 1 ) {
      if( (*s)[(*evtcount)-1]->start <= (*s)[(*evtcount)-2]->stop ) {
         (*evtcount)--;
      }
   }
   // last add points > 1 min apart most likely means that last
   // commercial block really ends at the previous add point
   if( addcount > 1 ) {
      addcount--;
      if( addpoint[addcount] - addpoint[addcount-1] > (__s64)60000000000LL ) {
         (*s)[(*evtcount)-1]->stop = addpoint[addcount-1];
      }
   }
   
   // Sanity check commercial blocks (>6 min diff => throw out)
   for(i=0; i<(*evtcount); ++i) {
      if( (*s)[i]->stop - (*s)[i]->start > (__s64)360000000000LL ) {
         fprintf(stderr,"**Throwing out commercial block: ");
         fprintf(stderr,"%s -> ", format_ts((*s)[i]->start,t1));
         fprintf(stderr,"%s\n", format_ts((*s)[i]->stop,t2));
         for(j=i+1; j<(*evtcount); ++j) {
            (*s)[j-1]->start = (*s)[j]->start;
            (*s)[j-1]->stop = (*s)[j]->stop;
         }
         (*evtcount)--;
      }
   }
   
   if(RTV_DEBUG) {
      fprintf(stderr,"**5K Commercial EVT points**\n");
      fprintf(stderr,"evtcount = %d\n",(*evtcount));
      for(i=0; i<(*evtcount); ++i) {
         fprintf(stderr,"%d ",i); 
         fprintf(stderr,"(%4.2f mins) ",
                 (float)((*s)[i]->stop-(*s)[i]->start)/60e9
            );
         fprintf(stderr,"%s -> ", format_ts((*s)[i]->start,t1));
         fprintf(stderr,"%s\n", format_ts((*s)[i]->stop,t2));
      }
   }
   
   // release the fade list
   current = root;
   while (current) {
      root = current;
      current = current->next;
      free(root);
   }
   return 1;
}

#endif
