/*
 * Copyright (C) 2005 John Honeycutt <honeycut@sourceforge.net>
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
 *
 *  NOTE: Parts of this code are based on DVBSNOOP
 *        a dvb sniffer  and mpeg2 stream analyzer tool
 *        http://dvbsnoop.sourceforge.net/
 *        (c) 2001-2004   Rainer.Scherg@gmx.de  (rasc)
 */

#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include "rtvlib.h"
#include "bigfile.h"

#define PRT(fmt, args...)  fprintf(outfd, fmt, ## args)

#define MPEG_VID_STREAM_HDR (0x000001e0)
#define MPEG_AUD_STREAM_HDR (0x000001c0)
#define MPEG_PACK_HDR       (0x000001ba)
#define MPEG_SYSTEM_HDR     (0x000001bb)
#define MPEG_PROGRAM_END    (0x000001b9)

typedef enum {
   NDX_VER_22 = 2,
   NDX_VER_30 = 3
} ndx_version_t;

// Globals
//
int           do_ndx_only = 0;
int           do_evt_file = 0;
int           ndx_exists  = 0;
int           mpg_exists  = 0;
int           verb        = 1;
int           hex_verb    = 0;
const char   *outfile     = NULL;
FILE         *outfd;
ndx_version_t ndx_ver;

int mpg_fpos = 0;
int ndx_fpos = 0;

int           dump_level = 1
;

// rtv_hex_dump()
//
static void hex_dump(char * tag, unsigned long address, unsigned char * buf, size_t sz)
{
    unsigned int  rows, row, col, i;
    unsigned long addr;
    char          tmpstr[512];
    char         *strp = tmpstr;

    PRT("DUMP: %s\n", tag);
    rows = (sz + 15)/16;
    for (row = 0; row < rows; row++) {
        addr = address + (row*16);
        strp += sprintf(strp, "0x%08lx | ", addr);
        for (col = 0; col < 16; col++) {
            i = row * 16 + col;
            if (i < sz) {
               strp += sprintf(strp, "%02x", buf[i]);
            }
            else {
                strp += sprintf(strp, "  ");
            }
            if ((i & 3) == 3) {
               strp += sprintf(strp, " ");
            }
        }
        PRT("%s\n", tmpstr);
        strp = tmpstr;
    }
}

static void unexpected(int recno, const char *reason, __u64 value)
{
   fprintf(stderr, "UNEXPECTED %d: %s; value == %llx\n", recno, reason, value);
   if ( outfd != stdout ) {
      PRT("#UNEXPECTED %d %s; value == %llx\n", recno, reason, value);
   }
}

static int process_ndx_header(FILE *fp)
{
   size_t               cnt;
   char                 buf[32];
   rtv_ndx_30_header_t *hdr30; //32-bytes
   
   if ( (cnt = fread(buf, 1, sizeof(rtv_ndx_30_header_t), fp)) != sizeof(rtv_ndx_30_header_t) ) {
      fprintf(stderr, "ERROR: unable to read header version info\n");
      return(cnt);
   }
   
   hdr30 = (rtv_ndx_30_header_t*)buf;
   
   if ( (hdr30->major_version == 2) && (hdr30->minor_version == 2) ) {
      ndx_ver = NDX_VER_22;
      hex_dump("NDX_HDR_VER:2.2", 0, buf, sizeof(rtv_ndx_22_header_t));      
      ndx_fpos += sizeof(rtv_ndx_22_header_t);
   }
   else if ( (hdr30->major_version == 3) && (hdr30->minor_version == 0) ) {
      ndx_ver = NDX_VER_30;
      hex_dump("NDX_HDR_VER:3.0", 0, buf, sizeof(rtv_ndx_30_header_t));      
      ndx_fpos += sizeof(rtv_ndx_30_header_t);
   }
   else {
      fprintf(stderr, "ERROR: unknown ndx header version\n");
      hex_dump("UNKNOWN HEADER", 0, buf, sizeof(rtv_ndx_30_header_t));
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


static int process_30(FILE *ndx, BIGFILE *mpg)
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


   last = this;
   memset(&this, 0, sizeof this);
   
   if ( do_ndx_only ) {
      printf("\nrec         timestamp    delta_t        iframe_filepos                        iframe_size\n");
      printf("-----------------------------------------------------------------------------------------------\n");

      while ( (cnt = fread(&this.r, 1, sizeof(rtv_ndx_30_record_t), ndx)) > 0) {
         
         rtv_convert_30_ndx_rec(&this.r);
         if (recno == 0) {
            basetime      = this.r.timestamp;
            this.delta_tm = 0;
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

         if ( recno > 0 ) {
            this.delta_tm = (__s64)(this.r.timestamp - last.r.timestamp) / 1000000; // convert to mS
            if ( (this.delta_tm < 480) || (this.delta_tm > 520) ) {
               unexpected(recno, "*****************Timestamp skew********************",  this.delta_tm);
            } 
         }
         
         PRT("%06d    %s   %8llu    0x%016llx = %-16llu  0x%08lx = %-8lu\n", 
                recno, format_seconds(this.seconds), this.delta_tm,
                this.r.filepos_iframe, this.r.filepos_iframe, this.r.iframe_size, this.r.iframe_size);
         

         memcpy(&last, &this, sizeof this);
         recno++;
         ndx_fpos += cnt;
      } //while

      printf("\nDone. Processed %d ndx records\n", recno);
      return(0);
   } //do_ndx_only

   
   if ( dump_level == 1 ) {
      printf("\nrec       timestamp     delta_t   iframe_filepos   iframe_size\n");
      printf("----------------------------------------------------------------\n");
   }
   else if ( dump_level == 2 ) {
      printf("\nrec       timestamp     iframe_filepos   iframe_size    empty\n");
      printf("--------------------------------------------------------------\n");
   }

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
   printf("\nDone. Processed %d ndx records\n", recno);
   
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

static void usage(char *name) 
{
   fprintf(stderr, "\n\n%s: A tool for parsing ReplayTV 5K mpg files, ndx files, and evt files.\n", name);
   fprintf(stderr, "Usage: %s [options] <file base name>\n", name);
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "   -f <output file name> : write results to output file\n");
   fprintf(stderr, "   -n                    : only process ndx file (skip mpg parsing)\n");
   fprintf(stderr, "   -e                    : process evt file\n");
   fprintf(stderr, "   -v <level>            : results verbosity level (default = 1)\n");
   fprintf(stderr, "       level=0           : Quiet. Only report errors\n");
   fprintf(stderr, "       level=1           : Report ndx & mpg file timestamps\n");
   fprintf(stderr, "       level=2           : Report ndx & mpg file timestamps\n");
   fprintf(stderr, "       level=3           : Report ndx & mpg file timestamps, display mpeg headers\n");
   fprintf(stderr, "   -h <level>            : hex dump level (default = 0)\n");
   fprintf(stderr, "       level=0           : Quiet.\n");
   fprintf(stderr, "       level=1           : Do mpeg header hex dumps.\n");
   fprintf(stderr, "       level=2           : Dump first 100 bytes or streams.\n");
   fprintf(stderr, "       level=3           : Dump all data..\n");

}
int main(int argc, char ** argv)
{
   FILE    *ndx = NULL;
   FILE    *evt = NULL;
   BIGFILE *mpg = NULL;
   char     filename[1024];
   char    *basefile;
   char     ch;


   if ( argc < 2 ) {
      usage(argv[0]);
      exit(0);
   }

   optind = 0;
   while ((ch = getopt(argc, argv, "f:v:h:ne")) != EOF) {
      switch(ch) {
      case 'v':
         verb = atoi(optarg);
         break;
      case 'h':
         hex_verb = atoi(optarg);
         break;
      case 'f':
         outfile = optarg;
         break;
      case 'n':
         do_ndx_only = 1;
         break;
      case 'e':
         do_evt_file = 1;
         break;
      default:
         printf("Parm error: Invalid argument: -%c\n", ch);
         printf("   Do: \"%s<ret> for help\n", argv[0]);
         exit(0);
      }
   }

   if (argc - optind != 1) {
      printf("Parm error: Need to specify <base file name> to process\n");
      printf("   Do: \"%s <ret> for help\n", argv[0]);
      return(0);
   }
   basefile = argv[optind];

   if ( outfile != NULL ) {
      fprintf(stderr,"Sending output to: %s\n", outfile);
      outfd = stdout;
   }
   else {
      outfd = stdout;
   }

   PRT("------------------------------------------------\n\n");
   PRT("Configuration:\n");
   PRT("Basefile: %s\n", basefile);
   PRT("options: verbosity=%d hex_dump=%d\n", verb, hex_verb);
   PRT("------------------------------------------------\n\n");

   if ( do_evt_file ) {
      sprintf(filename, "%s.evt", basefile);
      evt = fopen(filename, "r");
      if (!evt) {
         fprintf(stderr, "Error: Couldn't open %s, \n", filename);
         exit(1);
      }
      PRT("Found ent file: %s\n", filename);

      //process evt file
   }

   sprintf(filename, "%s.ndx", basefile);
   ndx = fopen(filename, "r");
   if (!ndx) {
      if ( do_ndx_only ) {
         fprintf(stderr, "Error: Couldn't open %s\n", filename);
         exit(1);
      }
   }
   else {
      ndx_exists = 1;
      PRT("Found ndx file: %s\n", filename);
   }

   sprintf(filename, "%s.mpg", basefile);
   mpg = bfopen(filename, "r");
   if (!mpg) {
      PRT("Couldn't open mpg file: %s, reducing checking\n", filename);
   } 
   else {
      PRT("Found mpg file: %s\n", filename);
      mpg_exists = 1;
   }

   if ( !(ndx_exists) && !(mpg_exists) ) {
      fprintf(stderr, "Error: neither ndx or mpg file found\n");      
      exit(0);
   }

   PRT("------------------------------------------------\n\n");
   
   rtv_init_lib();

   if ( ndx_exists ) {
      if ( process_ndx_header(ndx) < 0 ) {
         exit(1);
      }
      if ( ndx_ver == NDX_VER_22 ) {
         fprintf(stderr, "Error: RTV 4K header format not supported\n");
         exit(1);
      }
      else if ( ndx_ver == NDX_VER_30 ) {
         process_30(ndx, mpg);
      }
   }

   return(0);
}
