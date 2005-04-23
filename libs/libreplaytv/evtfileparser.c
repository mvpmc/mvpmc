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


static char* format_ts(__u32 ts, char *time)
{
   __u32 ms = ts;
   __u32 sec = ms / 1000;
   __u32 min = sec / 60;
   
   sec -= (min*60);
   ms %= 1000;
   sprintf (time, "%03d:%02d.%03d", (int)min, (int)sec, (int)ms);
   return time;
}


static void print_fadepoints(const fade_pt_t *fpts) 
{
   int cnt = 0;
   char start_str[20];
   char stop_str[20];

   RTV_PRT("start       stop          vid      aud      type\n");
   RTV_PRT("---------------------------------------------------\n");

   while ( fpts != NULL ) {
      cnt++;
      format_ts(fpts->start, start_str);
      format_ts(fpts->stop, stop_str);
      RTV_PRT("%s  %s   %08lx %08lx %08lx\n", start_str, stop_str, fpts->video, fpts->audio, fpts->type);
      fpts = fpts->next;
   }
   RTV_PRT("FadepointCount=%d\n", cnt);
}


static void print_chapters(const rtv_prog_seg_t *chapters, int num_chapters) 
{
   int  x;
   char start_str[20];
   char stop_str[20];

   RTV_PRT(" num   start       stop  \n");
   RTV_PRT("-------------------------\n");

   for ( x=0; x < num_chapters; x++ ) {
      format_ts(chapters[x].start, start_str);
      format_ts(chapters[x].stop, stop_str);
      RTV_PRT("%3d: %s  %s\n", x, start_str, stop_str);
   }
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
   unsigned int  progseg_min = evtfile_parms.p_seg_min * 1000; //minimum program segment time. (mS)
   unsigned int  scene_min   = evtfile_parms.scene_min * 1000; //minimum scene time (mS)
   unsigned int  evtfile_sz  = evtfile_parms.buf_sz;           //evt file buffer size
   char         *evtfile_buf = evtfile_parms.buf;              //evt file buffer
   __u32         lastvid     = 256;
   __u32         lastaud     = 10000;

   rtv_evt_record_t *evt_recs; //array of evt recs
   int               num_recs; //number of evt records
   rtv_prog_seg_t   *chapter_recs;
   int               num_chapters;
   int               addcount;
   int               commercial_start;
   int               x, i, j;
   __u32             audio_min;
   __u32             audio_factor;
   __u32             lasttime, lastvidtime, lastaudtime;
   __u32             addpoint[20];
   __u32             last_added;
   fade_pt_t        *current;
   fade_pt_t        *root;

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


   // First pass: Create a list of valid fade points within the event groups.
   // We don't care what these fade points are yet.
   //
   lasttime = lastvidtime = lastaudtime = 0;
   
   root = current = (fade_pt_t*)malloc(sizeof(fade_pt_t));
   memset(current, 0, sizeof(fade_pt_t));
   
   for ( x=0; x < num_recs; x++ ) {
      __u32 rec_timestamp = evt_recs[x].timestamp / 1000000; //nS to mS

      if ( !(lasttime) || ((audio_factor == 1) && (rec_timestamp - lasttime) > 1000) ) { //1 sec
         if ( current->start ) {
            if ( !(current->stop) ) {
               current->stop = lasttime;
            }
            current->next = (fade_pt_t*)malloc(sizeof(fade_pt_t));
            current = current->next;
            memset(current, 0, sizeof(fade_pt_t));
         }
         current->stop  = 0;
         current->video = 256;
         current->audio = 10000;
         lastvid        = 256;
         lastaud        = 10000;
      }
      
      if ( evt_recs[x].data_type == EVT_AUD ) {
         if ( (rec_timestamp - lastvidtime) > 70 ) { //70mS
            lastvid = 256;
         }
         lastaud     = evt_recs[x].audiopower * audio_factor;
         lastaudtime = rec_timestamp;
      }

      if (evt_recs[x].data_type == EVT_VID) {
         __u8 *color   = (__u8*)&(evt_recs[x].blacklevel);
         int   divisor = 0;

         if ( (rec_timestamp - lastaudtime) > 50 ) { //50mS
            lastaud = 10000;
         }
         lastvid = 0;
         for ( i=0; i < 4; i++ ) {
            if ( color[i] ) {
               lastvid += color[i];
               divisor++;
            }
         }
         if ( divisor ) {
            lastvid /= divisor;
         }
         lastvidtime = rec_timestamp;
      }

      if ( ((audio_factor > 1) && ((!(current->start) && lastaud < audio_min) || (current->start && lastaud >= audio_min))) ) {
         if ( current->start ) {
            if ( (rec_timestamp - current->start) > 1000 ) { //1sec
               if ( !(current->stop) ) {
                  current->stop = lasttime;
               }
               current->next = (fade_pt_t*)malloc(sizeof(fade_pt_t));
               current = current->next;
               memset(current, 0, sizeof(fade_pt_t));
            }
         }
         current->stop = 0;
         current->video = 256;
         current->audio = 10000;
      }

      // sometimes we see errant past times at the end of the evt,
      // filter them out
      //
      if ( rec_timestamp >= lasttime ) {
         lasttime = rec_timestamp;
         
         // A fade is only valid if BOTH vid and aud fall below
         // our thresholds.
         //
         if ( lastvid < 32 ) {
            if ( lastaud < audio_min ) {
               if ( !(current->start) ) {
                  current->start = lasttime;
                  current->video = lastvid;
                  current->audio = lastaud;
               }
               else if ( current->stop ) {
                  current->start = lasttime;
                  current->video = lastvid;
                  current->audio = lastaud;
                  current->stop = 0;
               }
            }
            else if ( current->start && !(current->stop) ) {
               current->stop = lasttime;
            }
         }
         else if ( (lastvid > 200) && current->start && !(current->stop) ) {
            current->stop = lasttime;
         }
      }
   } //for

   if ( RTVLOG_EVTFILE ) {
      RTV_PRT("\n>>>Fadepoints<<<\n");
      print_fadepoints(root);
   }


   // Second pass: Walk the list of fade points and see if anything falls out.
   // For now, just look for time between fades, if it's > 5 minutes it's likely
   // to be a program segment, else it's likely to be a commercial segment.  if
   // there is a fade in the first minute, the recording may have started on a
   // commercial.  nothing fancy.
   //
   current          = root;
   lasttime         = 0;
   addcount         = 0;
   last_added       = 0;
   chapter_recs     = NULL;
   num_chapters     = 0;
   commercial_start = 0;
      
   while (current) {
      if (current->start) {
         __u32 spottime;

         // use the middle value of the fade range as the edit time
         //
         current->stop = (current->stop + current->start)/2;
         spottime = current->stop - lasttime;
         if ( !(lasttime) || (spottime >= scene_min) ) {

            // if it's less than progseg_min, we want to add the edit
            //
            if ( lasttime && (spottime < progseg_min) ) {
               current->type = 1;
            }

            // if it's the first one in the first minute, we want to add
            // the edit
            //
            if ( !(lasttime) && spottime < 60000 ) { //60sec
               current->type = 1;
            }
            else {
               lasttime = current->stop;
            }
            
            if( RTVLOG_EVTFILE ) {
               char t1[16];
               RTV_PRT("%c ", current->type ? 'A' : 'D');
               RTV_PRT("%s\n", format_ts(current->stop,t1));
            }
            
            if( current->type ) {
               last_added = current->stop;
               if( last_added ) {
                  if( addcount > 20 ) {
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
               if( num_chapters == 0 ) {
                  chapter_recs = (rtv_prog_seg_t*)malloc(sizeof(rtv_prog_seg_t));
               } else {
                  chapter_recs = (rtv_prog_seg_t*)realloc(chapter_recs, sizeof(rtv_prog_seg_t) * (num_chapters+1));
               }

               chapter_recs[num_chapters].start = current->stop;
               if( last_added && (num_chapters > 0) ) {
                  chapter_recs[num_chapters-1].stop = last_added;
               }
               num_chapters++;
               commercial_start = 0;
            }
         }
         else lasttime = current->stop;
      }
      current = current->next;
   } //while


   if ( RTVLOG_EVTFILE ) {
      RTV_PRT("\n>>>Chapters 1st Pass<<<\n");
      print_chapters(chapter_recs, num_chapters);
   }

   if( num_chapters > 0 ) {
      chapter_recs[num_chapters-1].stop = lasttime;
      if( last_added > chapter_recs[num_chapters-1].start )
         chapter_recs[num_chapters-1].stop = last_added;
   }

   // Throw out last evt point if it's bogus
   //
   if( num_chapters > 1 ) {
      if( chapter_recs[num_chapters-1].start <= chapter_recs[num_chapters-2].stop ) {
         num_chapters--;
      }
   }

   // last add points > 1 min apart most likely means that last
   // commercial block really ends at the previous add point
   //
   if( addcount > 1 ) {
      addcount--;
      if( addpoint[addcount] - addpoint[addcount-1] > 60000 ) { //60sec
         chapter_recs[num_chapters-1].stop = addpoint[addcount-1];
      }
   }

   // Sanity check commercial blocks (>6 min diff => throw out)
   //
   for( i=0; i < num_chapters; ++i ) {
      if( chapter_recs[i].stop - chapter_recs[i].start > 360000 ) { //360sec
         if ( RTVLOG_EVTFILE ) {
            char t1[20], t2[20];
            RTV_PRT("**Throwing out commercial block: ");
            RTV_PRT("%s -> ", format_ts(chapter_recs[i].start, t1));
            RTV_PRT("%s\n", format_ts(chapter_recs[i].stop, t2));
         }
         for( j=i+1; j < num_chapters; ++j ) {
            chapter_recs[j-1].start = chapter_recs[j].start;
            chapter_recs[j-1].stop  = chapter_recs[j].stop;
         }
         num_chapters--;
      }
   } //for

   if ( RTVLOG_EVTFILE ) {
      RTV_PRT("\n>>>Chapters 2nd Pass<<<\n");
      print_chapters(chapter_recs, num_chapters);
   }

   // release the fade list
   //
   current = root;
   while (current) {
      root = current;
      current = current->next;
      free(root);
   }

   chapter_struct->num_chapters = num_chapters;
   chapter_struct->chapter      = chapter_recs;
   return(0);
}
