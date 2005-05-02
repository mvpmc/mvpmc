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

//#define DBGPRT(fmt, args...) fprintf(stdout, fmt, ## args) 
#define DBGPRT(fmt, args...)

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


//+*********************************************************************
//  Name: print_evt_recs
//+*********************************************************************
static void print_evt_recs(char *hdr,  rtv_evt_record_t *recs, int num_recs) 
{
   int x;
   
   RTV_PRT("\n\n");
   rtv_hex_dump("EVT FILE HDR", 0, hdr,  RTV_EVT_HDR_SZ, 1);      
   
   RTV_PRT("\nrec          timestamp                           aud/vid          audiopower              blacklevel             unknown\n");
   RTV_PRT("--------------------------------------------------------------------------------------------------------------------------\n");
   for ( x = 0; x < num_recs; x++ ) {
      RTV_PRT("%05d   0x%016llx=%016llu   0x%08lx     0x%08lx=%010lu     0x%08lx=%010lu     0x%08lx\n", 
              x, recs[x].timestamp, recs[x].timestamp, recs[x].data_type, 
              recs[x].audiopower, recs[x].audiopower, recs[x].blacklevel,  recs[x].blacklevel, recs[x].unknown1);
   }
}


//+*********************************************************************
//  Name: print_fadepoints
//+*********************************************************************
static void print_fadepoints(const fade_pt_t *fpts) 
{
   int cnt = 0;
   char start_str[20];
   char stop_str[20];

   RTV_PRT("start       stop          vid      aud      type\n");
   RTV_PRT("---------------------------------------------------\n");

   while ( fpts != NULL ) {
      rtv_format_ts_ms32_min_sec_ms(fpts->start, start_str);
      rtv_format_ts_ms32_min_sec_ms(fpts->stop, stop_str);
      RTV_PRT("%3d %s  %s   %08lx %08lx %08lx\n", cnt, start_str, stop_str, fpts->video, fpts->audio, fpts->type);
      fpts = fpts->next;
      cnt++;
   }
   RTV_PRT("\n");
}


//+*********************************************************************
//  Name: vid_level
//+*********************************************************************
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


//+*********************************************************************
//  Name: aud_level
//+*********************************************************************
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

//+*************************************************************************************************************
//           PUBLIC
//+*************************************************************************************************************

//+*********************************************************************
//  Name: rtv_print_comm_blks
//+*********************************************************************
void rtv_print_comm_blks(const rtv_prog_seg_t *block, int num_blocks) 
{
   int  x;
   char start_str[20];
   char stop_str[20];

   RTV_PRT(" num   start       stop  \n");
   RTV_PRT("-------------------------\n");

   for ( x=0; x < num_blocks; x++ ) {
      rtv_format_ts_ms32_min_sec_ms(block[x].start, start_str);
      rtv_format_ts_ms32_min_sec_ms(block[x].stop, stop_str);
      RTV_PRT("%3d: %s  %s\n", x, start_str, stop_str);
   }
}


//+***********************************************************************************************
//  Name: rtv_parse_evt_file
//  Processes evt file records and builds list of chapter segments.
//+***********************************************************************************************
int rtv_parse_evt_file( rtv_chapter_mark_parms_t evtfile_parms, rtv_comm_blks_t *commercials)
{
   unsigned int  progseg_min = evtfile_parms.p_seg_min * 1000; //minimum program segment time. (mS)
   unsigned int  scene_min   = evtfile_parms.scene_min * 1000; //minimum scene time (mS)
   unsigned int  evtfile_sz  = evtfile_parms.buf_sz;           //evt file buffer size
   char         *evtfile_buf = evtfile_parms.buf;              //evt file buffer
   __u32         lastvid     = 256;
   __u32         lastaud     = 10000;

   rtv_evt_record_t *evt_recs; //array of evt recs
   int               num_recs; //number of evt records
   rtv_prog_seg_t   *comm_blk_recs;
   int               num_blocks;
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
   commercials->num_blocks = 0;
   commercials->blocks     = NULL;

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

   if ( RTVLOG_EVTFILE_V3 ) {
      print_evt_recs(evtfile_buf - RTV_EVT_HDR_SZ,  evt_recs, num_recs);
   }

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

      if ( RTVLOG_EVTFILE_V3 ) {
         char rec_ts_str[20];
         if ( rec_timestamp > (lasttime + 1000) ) {
            RTV_PRT("----------:\n");
         }
         RTV_PRT("%4d: %s: %c %s | %s\n", 
                 x, rtv_format_ts_ms32_min_sec_ms(rec_timestamp, rec_ts_str), 
                 (evt_recs[x].data_type == EVT_AUD) ? 'A' : 'V',
                 vid_level(lastvid), aud_level(lastaud));
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

   if ( RTVLOG_EVTFILE_V2 ) {
      RTV_PRT("\n>>>Fadepoints<<<\n");
      print_fadepoints(root);
   }


   // Second pass: Walk the list of fade points and see if anything falls out.
   // For now, just look for time between fades, if it's > 5 minutes it's likely
   // to be a program segment, else it's likely to be a commercial segment.  if
   // there is a fade in the first minute, the recording may have started on a
   // commercial.
   //
   current          = root;
   lasttime         = 0;
   addcount         = 0;
   last_added       = 0;
   comm_blk_recs     = NULL;
   num_blocks     = 0;
   commercial_start = 0;
   x                = 0;   //Track fadepoint numbers.
      
   while (current) {
      if (current->start) {
         __u32 spottime;

         // use the middle value of the fade range as the edit time
         //
         DBGPRT("\n");
         current->stop = (current->stop + current->start)/2;
         spottime = current->stop - lasttime;
         if ( !(lasttime) || (spottime >= scene_min) ) {
            DBGPRT("-->%3d: (!(lasttime) || spottime > scene_min)  lasttime=%lu spottime=%lu scene_min=%d\n", x, lasttime, spottime, scene_min);
            
            // if it's less than progseg_min, we want to add the edit
            //
            if ( lasttime && (spottime < progseg_min) ) {
               DBGPRT("------>%3d: (lasttime && (spottime < progseg_min))  lasttime=%lu spottime=%lu progseg_min=%d\n", x, lasttime, spottime, progseg_min);
               current->type = 1;
            }

            // if it's the first one in the first minute, we want to add
            // the edit
            //
            if ( !(lasttime) && spottime < 60000 ) { //60sec
               DBGPRT("------>%3d: (!(lasttime) && spottime < 60000)  lasttime=%lu spottime=%lu\n", x, lasttime, spottime);
               current->type = 1;
            }
            else {
               DBGPRT("------>%3d: (lasttime = current->stop) current->stop=%lu\n", x, current->stop);
               lasttime = current->stop;
            }
            
            if( RTVLOG_EVTFILE_V2 ) {
               char t1[16];
               RTV_PRT("%c ", current->type ? 'A' : 'D');
               RTV_PRT("%s\n", rtv_format_ts_ms32_min_sec_ms(current->stop,t1));
            }
            
            if( current->type ) {
               last_added = current->stop;
               DBGPRT("------>%3d: (current->type) last_added=%lu\n", x, current->stop);
               if( last_added ) {
                  DBGPRT("---------->%3d: (last_added)\n", x);
                  if( addcount > 20 ) {
                     addpoint[0] = addpoint[19];
                     addpoint[1] = addpoint[20];
                     addcount = 2;
                     DBGPRT("-------------->%3d: (addcount > 20) addcnt=2 addpoint[0]=%lu addpoint[0]=%lu\n", x, addpoint[0], addpoint[2]);
                  }
                  addpoint[addcount] = last_added;
                  DBGPRT("---------->%3d: (end: last_added) addpoint[%d]=%lu \n", x, addcount, addpoint[addcount]);
                  addcount++;
               }
            } else {
               DBGPRT("------>%3d: (commercial_start = 1)\n", x);
               commercial_start = 1;
            }
            
            if( commercial_start ) {
               DBGPRT("------>%3d: (commercial_start)\n", x);
               if( num_blocks == 0 ) {
                  comm_blk_recs = (rtv_prog_seg_t*)malloc(sizeof(rtv_prog_seg_t));
               } else {
                  comm_blk_recs = (rtv_prog_seg_t*)realloc(comm_blk_recs, sizeof(rtv_prog_seg_t) * (num_blocks+1));
               }

               comm_blk_recs[num_blocks].start = current->stop;
               DBGPRT("------>%3d: num_blocks=%d comm_blk_recs[num_blocks].start=%lu\n", x, num_blocks, comm_blk_recs[num_blocks].start);

               if( last_added && (num_blocks > 0) ) {
                  comm_blk_recs[num_blocks-1].stop = last_added;
                  DBGPRT("---------->%3d: (last_added && (num_blocks > 0)) comm_blk_recs[num_blocks-1].stop=%lu\n", 
                         x, comm_blk_recs[num_blocks-1].stop);
               }
               else {
                  DBGPRT("---------->%3d: (ELSE:NOT last_added && (num_blocks > 0))\n", x);
               }
               num_blocks++;
               commercial_start = 0;
            }
         }
         else {
            // This is hit if (lasttime != 0) && (spottime(time beteween fades) < scene_min)
            //
            lasttime = current->stop;
            DBGPRT("------>%3d: (!(commercial_start)) lasttime=%lu\n", x, lasttime);
         }
      }
      current = current->next;
      x++;
   } //while

   if ( RTVLOG_EVTFILE_V2 ) {
      RTV_PRT("\n>>>Commercial Blocks 1st Pass<<<\n");
      rtv_print_comm_blks(comm_blk_recs, num_blocks);
   }

   if( num_blocks > 0 ) {
      comm_blk_recs[num_blocks-1].stop = lasttime;
      if( last_added > comm_blk_recs[num_blocks-1].start )
         comm_blk_recs[num_blocks-1].stop = last_added;
   }

   // Throw out last evt point if it's bogus
   //
   if( num_blocks > 1 ) {
      if( comm_blk_recs[num_blocks-1].start <= comm_blk_recs[num_blocks-2].stop ) {
         num_blocks--;
      }
   }

   // last add points > 1 min apart most likely means that last
   // commercial block really ends at the previous add point
   //
   if( addcount > 1 ) {
      addcount--;
      if( (addpoint[addcount] - addpoint[addcount-1]) > 60000 ) { //60sec
         comm_blk_recs[num_blocks-1].stop = addpoint[addcount-1];
      }
   }

   // Sanity check commercial blocks (>6 min diff => throw out)
   //
   for( i=0; i < num_blocks; ++i ) {
      if( comm_blk_recs[i].stop - comm_blk_recs[i].start > 360000 ) { //360sec
         if ( RTVLOG_EVTFILE_V2 ) {
            char t1[20], t2[20];
            RTV_PRT("**Throwing out commercial block: ");
            RTV_PRT("%s -> ", rtv_format_ts_ms32_min_sec_ms(comm_blk_recs[i].start, t1));
            RTV_PRT("%s\n", rtv_format_ts_ms32_min_sec_ms(comm_blk_recs[i].stop, t2));
         }
         for( j=i+1; j < num_blocks; ++j ) {
            comm_blk_recs[j-1].start = comm_blk_recs[j].start;
            comm_blk_recs[j-1].stop  = comm_blk_recs[j].stop;
         }
         num_blocks--;
      }
   } //for

#if 0
   if ( RTVLOG_EVTFILE ) {
      RTV_PRT("\n>>>Commercial Block List<<<\n");
      rtv_print_comm_blks(comm_blk_recs, num_blocks);
   }
#endif

   // release the fade list
   //
   current = root;
   while (current) {
      root = current;
      current = current->next;
      free(root);
   }

   commercials->num_blocks = num_blocks;
   commercials->blocks     = comm_blk_recs;
   return(0);
}
