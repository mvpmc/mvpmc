/*
 * Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
 * Copyright (C) 2004 John Honeycutt <honeycut@sourceforge.net>
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include "rtvlib.h"
#include "bigfile.h"


#define MPEG_VID_STREAM_HDR (0x000001e0)
#define MPEG_AUD_STREAM_HDR (0x000001c0)
#define MPEG_PACK_HDR       (0x000001ba)

typedef enum {
   NDX_VER_22 = 2,
   NDX_VER_30 = 3
} ndx_version_t;

ndx_version_t ndx_ver;
int           dump_level = 0;

static void unexpected(int recno, const char *reason, __u64 value)
{
   fprintf(stderr, "UNEXPECTED %d: %s; value == %llx\n", recno, reason, value);
   printf("#UNEXPECTED %d %s; value == %llx\n", recno, reason, value);
}

static int process_header(FILE *fp)
{
   size_t               cnt;
   char                 buf[32];
   rtv_ndx_30_header_t *hdr30; //32-bytes
   rtv_ndx_22_header_t *hdr22; //32-bytes
   
   if ( (cnt = fread(buf, 1, sizeof(rtv_ndx_30_header_t), fp)) != sizeof(rtv_ndx_30_header_t) ) {
      fprintf(stderr, "ERROR: unable to read header version info\n");
      printf("#ERROR: unable to read header version info\n");
      return(cnt);
   }
   
   hdr30 = (rtv_ndx_30_header_t*)buf;
   
   if ( (hdr30->major_version == 2) && (hdr30->minor_version == 2) ) {
      ndx_ver = NDX_VER_22;
      hdr22 = (rtv_ndx_22_header_t*)buf;
      rtv_hex_dump("NDX_HDR_VER:2.2", buf, sizeof(rtv_ndx_22_header_t));      
   }
   else if ( (hdr30->major_version == 3) && (hdr30->minor_version == 0) ) {
      ndx_ver = NDX_VER_30;
      rtv_hex_dump("NDX_HDR_VER:3.0", buf, sizeof(rtv_ndx_30_header_t));      
   }
   else {
      fprintf(stderr, "ERROR: unknown header version\n");
      printf("#ERROR: unknown header version\n");
      rtv_hex_dump("UNKNOWN HEADER", buf, sizeof(rtv_ndx_30_header_t));
      return(-1);
   }
   
   return(ndx_ver);
}



static char *format_seconds(float seconds)
{
   static char buffer[12];
   int minutes;
   
   minutes = (int)seconds / 60;
   seconds -= minutes * 60;
   
   if (minutes)
      sprintf(buffer, "%3d:", minutes);
   else
      strcpy(buffer, "    ");
   sprintf(buffer + 4, "%07.4f", seconds);
   
   return buffer;
}

static int explore_22(FILE *ndx, BIGFILE *mpg)
{
   unsigned char buf[1024], *p;
   
   struct 
   {
      rtv_ndx_22_record_t r;
      float               seconds;
      int                 bad_video;
      int                 bad_audio;
   } this, last;
   
   int recno      = 0;
   __u64 basetime = 0;
   double commercial_start_seconds = 0;
   int in_commercial = 0;
   
   memset(&this, 0, sizeof this);
   
   printf("87654321 SC Recno VidO  AudO File Pos   Time Offs VideoPos AudioPos VAM\n");
   last = this;
   memset(&this, 0, sizeof this);
   
   while ( fread(&this.r, 1, sizeof(rtv_ndx_22_record_t), ndx) > 0) {
      /* 0x8000 -- unknown
         0x4000 -- unknown, new with jan 30 sw
         0x2000 -- PPV/protected (?? not any more?)
         0x0800 -- unknown, new with jan 30 sw
         0x0400 -- unknown, new with jan 30 sw
         0x0200 -- unknown, new with jan 30 sw
         0x0100 -- unknown, new with jan 30 sw
         0x0002 -- black screen
         0x0001 -- commercial
      */
      
      rtv_convert_22_ndx_rec(&this.r);
      
      if (this.r.commercial_flag & ~0x03) {
         unexpected(recno, "commercial flag has unexpected bit set", this.r.commercial_flag);
      }
      if (this.r.unk_fe != 0xfe && this.r.macrovision == 0) {
         unexpected(recno, "unk_fe != 0xfe without macrovision set", this.r.unk_fe);
      }
      if (this.r.macrovision != 0 && this.r.macrovision != 3) {
         unexpected(recno, "macrovision != 0 or 3", this.r.macrovision);
      }
      if (this.r.macrovision == 0 && this.r.macrovision_count != 0) {
         unexpected(recno, "macrovision clear but count != 0", this.r.macrovision_count);
      }
      if (this.r.macrovision && this.r.macrovision_count == 0) {
         unexpected(recno, "macrovision count clear but macrovision set", this.r.macrovision);
      }
      if (this.r.unused1 != 0) {
         unexpected(recno, "unused1 != 0", this.r.unused1);
      }
      if (this.r.stream_position % 0x8000 != 0) {
         unexpected(recno, "stream position not 32k boundary", this.r.stream_position);
      }
      if (this.r.video_offset % 4 != 0) {
         unexpected(recno, "video_offset % 4 != 0", this.r.video_offset);
      }
      if (this.r.video_offset >= 0x8000) {
         unexpected(recno, "video_offset larger than cluster size", this.r.video_offset);
      }
      if (this.r.audio_offset % 4 != 0) {
         unexpected(recno, "audio_offset % 4 != 0", this.r.audio_offset);
      }
      
      if (recno == 0) {
         basetime   = this.r.timestamp;
      }
      this.seconds = ((__s64)(this.r.timestamp - basetime)) / 1000000000.0;
      
      if (mpg) {
         bfseek(mpg, this.r.stream_position + this.r.video_offset, SEEK_SET);
         bfread(buf, 4, 1, mpg);
         p = buf;
         if (ntohl(*(__u32*)p) != 0x000001e0) {
            this.bad_video = 1;
         } 
         else {
            bfseek(mpg, this.r.stream_position + this.r.video_offset + 56, SEEK_SET);
            bfread(buf, 4, 1, mpg);
            p = buf;
            if (ntohl(*(__u32*)p) != 0x000001b8) {
               unexpected(recno, "Video offset correct, but GOP not found", this.r.video_offset);
            }
         }
         if (last.bad_video) {
            if ((bfseek(mpg, this.r.stream_position + last.r.video_offset, SEEK_SET)) < 0) {
               perror("bfseek");
            }
            
            bfread(buf, 4, 1, mpg);
            p = buf;
            if (ntohl(*(__u32*)p) != 0x000001e0) {
               p = buf;
               unexpected(recno, "Video not found after applying race condition correction", ntohl(*(__u32*)p));
            }
            if (this.r.stream_position - last.r.stream_position > 0x10000)
               unexpected(recno, "large stream block following bad video offset", this.r.stream_position - last.r.stream_position);
         }
         
         bfseek(mpg, this.r.stream_position + this.r.video_offset + this.r.audio_offset, SEEK_SET);
         bfread(buf, 4, 1, mpg);
         p = buf;
         if (ntohl(*(__u32*)p) != 0x000001c0) {
            this.bad_audio = 1;
            if (this.bad_audio && !this.bad_video) {
               unexpected(recno, "not audio without being not video", 0);
            }
         }
         if (last.bad_audio) {
            bfseek(mpg, this.r.stream_position + last.r.video_offset + this.r.audio_offset, SEEK_SET);
            bfread(buf, 4, 1, mpg);
            p = buf;
            if (ntohl(*(__u32*)p) != 0x000001c0) {
               bfseek(mpg, this.r.stream_position + last.r.video_offset + last.r.audio_offset, SEEK_SET);
               bfread(buf, 4, 1, mpg);
               p = buf;
               if (ntohl(*(__u32*)p) != 0x000001c0) {
                  p = buf;
                  unexpected(recno, "Audio not found after applying race condition correction", ntohl(*(__u32*)p));
               }
            }
         }
      } //mpg
      
      p = buf;
      if (this.r.commercial_flag & 0x1) {
         if (!in_commercial) {
            commercial_start_seconds = this.seconds;
            if (!(this.r.commercial_flag & 0x02)) {
               unexpected(recno, "Start of commercial without 0x02 flag", this.r.commercial_flag);
            }
            in_commercial = 1;
         }
      } 
      else if (in_commercial) {
         printf("# Commercial %s",format_seconds(commercial_start_seconds));
         printf(" - %s", format_seconds(this.seconds));
         printf(" (%s)\n", format_seconds(this.seconds - commercial_start_seconds));
         in_commercial = 0;
      }
      
      p += sprintf(p, "%c%c%c%c%c%c%c%c %c%c %5d %4x %5lx %08Lx %s %08Lx %08Lx %c%c%c",
                   this.r.flag_1 & 0x80 ? 'X' : '.',
                   this.r.flag_1 & 0x40 ? 'X' : '.',
                   this.r.flag_1 & 0x20 ? 'X' : '.',
                   this.r.flag_1 & 0x10 ? 'X' : '.',
                   this.r.flag_1 & 0x08 ? 'X' : '.',
                   this.r.flag_1 & 0x04 ? 'X' : '.',
                   this.r.flag_1 & 0x02 ? 'X' : '.',
                   this.r.flag_1 & 0x01 ? 'X' : '.',
                   this.r.commercial_flag & 0x02 ? 'X' : '.',
                   this.r.commercial_flag & 0x01 ? 'X' : '.',
                   recno,
                   this.r.video_offset,
                   this.r.audio_offset,
                   this.r.stream_position,
                   format_seconds(this.seconds),
                   this.r.stream_position + this.r.video_offset,
                   this.r.stream_position + this.r.video_offset + this.r.audio_offset,
                   this.bad_video ? last.bad_video ? 'O' : 'o' : '.',
                   this.bad_audio ? last.bad_audio ? 'O' : 'o' : '.',
                   this.r.macrovision ?              'O'       : '.'
         );
      puts(buf);
      recno++;
      last = this;
      memset(&this, 0, sizeof this);
   }
   return 0;
}

static int explore_30(FILE *ndx, BIGFILE *mpg)
{
   unsigned char buf[1024], *p;
   
   struct 
   {
      rtv_ndx_30_record_t r;
      float               seconds;
      __u64               delta_tm;
      int                 bad_video;
      int                 bad_audio;
   } this, last;
   
   int recno      = 0;
   __u64 basetime = 0;
   int cnt;
   
   memset(&this, 0, sizeof this);

   if ( dump_level == 1 ) {
      printf("\nrec       timestamp     delta_t   iframe_filepos   iframe_size\n");
      printf("----------------------------------------------------------------\n");
   }
   else if ( dump_level == 2 ) {
      printf("\nrec       timestamp     iframe_filepos   iframe_size    empty\n");
      printf("--------------------------------------------------------------\n");
   }

   last = this;
   memset(&this, 0, sizeof this);
   
   while ( (cnt = fread(&this.r, 1, sizeof(rtv_ndx_30_record_t), ndx)) > 0) {
      
      rtv_convert_30_ndx_rec(&this.r);
      if (recno == 0) {
         basetime      = this.r.timestamp;
         this.delta_tm = 0;
      }
      
      if ( dump_level == 2 ) {
         printf("%06d   0x%016llx=%016llu   0x%016llx=%016llu   0x%08lx=%8lu     0x%08lx\n", 
                recno, this.r.timestamp, this.r.timestamp, 
                this.r.filepos_iframe, this.r.filepos_iframe, this.r.iframe_size, this.r.iframe_size,
                this.r.empty);
      }

      if (this.r.empty != 0) {
         unexpected(recno, "empty != 0", this.r.empty);
      }
      if ((this.r.filepos_iframe  % 4) != 0) {
         unexpected(recno, "filepos_iframe not on 4 byte boundry", this.r.empty);
      }
      if ((this.r.iframe_size  % 4) != 0) {
         unexpected(recno, "iframe_size not on 4 byte boundry", this.r.iframe_size);
      }
      
      this.seconds = ((__s64)(this.r.timestamp - basetime)) / 1000000000.0;
      
      
      if (mpg) {
         bfseek(mpg, this.r.filepos_iframe, SEEK_SET);
         bfread(buf, 1, 256, mpg);
         if ( ntohl(*(__u32*)buf) != MPEG_VID_STREAM_HDR ) {
            unexpected(recno, "mpeg Video Iframe start expected",  ntohl(*(__u32*)buf));
         }
         if ( dump_level == 2 ) {
            rtv_hex_dump("VID", buf, 256);      
         }
         bfseek(mpg, this.r.filepos_iframe + this.r.iframe_size, SEEK_SET);
         bfread(buf, 1, 256, mpg);
         if ( (ntohl(*(__u32*)buf) != MPEG_AUD_STREAM_HDR) &&
              (ntohl(*(__u32*)buf) != MPEG_PACK_HDR)) {
            unexpected(recno, "mpeg audio frame or pack hdr expected",  ntohl(*(__u32*)buf));
         }
         if ( dump_level == 2 ) {
            rtv_hex_dump("AUD/PACK", buf, 256);
         }

         if ( recno > 0 ) {
            this.delta_tm = (__s64)(this.r.timestamp - last.r.timestamp) / 1000000; // convert to mS
            if ( (this.delta_tm < 480) || (this.delta_tm > 520) ) {
               unexpected(recno, "Timestamp skew",  this.delta_tm);
            } 
         }
      
         if ( dump_level == 1 ) {
            printf("%06d      %s   %10llu    0x%016llx=%016llu    0x%08lx=%8lu\n", 
                   recno, format_seconds(this.seconds), this.delta_tm,
                   this.r.filepos_iframe, this.r.filepos_iframe, this.r.iframe_size, this.r.iframe_size);
         }
         
      } //mpg
      
      memcpy(&last, &this, sizeof this);
      recno++;
   } //while
   printf("\nDone. Processed %d records\n", recno);
   
   return(0);
}

static int explore_evt(FILE *evt)
{
   unsigned char buf[1024], *p;
   
   rtv_evt_record_t rec;
   
   int recno      = 0;
   __u64 basetime = 0;
   double commercial_start_seconds = 0;
   int in_commercial = 0;
   int cnt;
   
   fread(buf, 1, 8, evt); //header
   printf("\n\n");
   rtv_hex_dump("EVT FILE HDR", buf, 8);      
   
   printf("\nrec       timestamp     aud/vid   audiopower    blacklevel   unknown\n");
   printf("-----------------------------------------------------------------------\n");
   
   while ( (cnt = fread(&rec, 1, sizeof(rtv_evt_record_t), evt)) > 0) {
      
      rtv_convert_evt_rec(&rec);
      if (recno == 0) {
         basetime = rec.timestamp;
      }
      printf("%06d:%d   0x%016llx=%016llu   0x%08lx=%010lu     0x%08lx=%010lu     0x%08lx=%010lu     0x%08lx\n", 
             recno, cnt, rec.timestamp, rec.timestamp, rec.data_type, rec.data_type, 
             rec.audiopower, rec.audiopower, rec.blacklevel,  rec.blacklevel, rec.unknown1);
      
      recno++;
   } //while
   
   return(0);
}

int main(int argc, char ** argv)
{
   char     filename[1024];
   char    *basefile;
   FILE    *ndx;
   FILE    *evt = NULL;;
   BIGFILE *mpg;
   

   if ( argc > 1 ) {
      if ( strlen(argv[1]) >= 3 ) {
         if ( strncmp("-v", argv[1], 2) == 0 ) {
            dump_level = atoi(&(argv[1][2]));
            printf("dump level = %d\n", dump_level);
            argv++;argc--;
         }
      }
   }

   if (argc > 1) {
      basefile = argv[1];
      sprintf(filename, "%s.ndx", basefile);
      ndx = fopen(filename, "r");
      if (!ndx) {
         fprintf(stderr, "Couldn't open %s, aborting\n", filename);
         exit(1);
      }
      sprintf(filename, "%s.mpg", basefile);
      mpg = bfopen(filename, "r");
      if (!mpg) {
         fprintf(stderr, "Couldn't open %s, reducing checking\n", filename);
      }
      sprintf(filename, "%s.evt", basefile);
      evt = fopen(filename, "r");
      if (!evt) {
         fprintf(stderr, "Couldn't open %s, Skipping evt file checking\n", filename);
      }
   } else {
      fprintf(stderr, "No filename given, just doing ndx checking\n");
      ndx = stdin;
      mpg = NULL;
   }
   
   rtv_init_lib();
   process_header(ndx);
   if ( ndx_ver == NDX_VER_22 ) {
      explore_22(ndx, mpg);
   }
   else if ( ndx_ver == NDX_VER_30 ) {
      explore_30(ndx, mpg);
   }
   return(0);
}
