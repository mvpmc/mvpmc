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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include "rtvlib.h"

#define PRT(fmt, args...)  fprintf(outfd, fmt, ## args)

#define UNEXPECTED(fmt, args...) \
{ \
  /* fprintf(stderr, "#UNEXPECTED: " fmt, ## args);*/  \
  PRT("\n#UNEXPECTED: " fmt, ## args); \
}


#define READ_BUF_SIZE (128 * 1024)

#define MPEG_VID_STREAM_HDR (0x000001e0)
#define MPEG_AUD_STREAM_HDR (0x000001c0)
#define MPEG_PACK_HDR       (0x000001ba)
#define MPEG_SYSTEM_HDR     (0x000001bb)
#define MPEG_PROGRAM_END    (0x000001b9)

#define MAX_EVT_FILE_SZ (200 *1024)

typedef enum 
{
   NDX_VER_22 = 2,
   NDX_VER_30 = 3
} ndx_version_t;

typedef enum
{
   SRCH_VIDEO      = 1,
   SRCH_AUDIO_PACK = 2,
   SRCH_NDX_DONE   = 3,
   SRCH_MPG_DONE   = 4
} search_state_t;

typedef struct ndx_rec_info_t
{
   rtv_ndx_30_record_t r;
   float               seconds;
   __u64               delta_tm;
   int                 bad_video;
   int                 bad_audio;
} ndx_rec_info_t;


typedef struct pack_hdr_t
{
   unsigned long long scr_all;
   unsigned int       scr_ext;
   unsigned int       prog_mux_rate;
} pack_hdr_t;

typedef struct audvid_hdr_t
{
   unsigned int       ptsdts_flags;
   unsigned long long pts_all;
} audvid_hdr_t;

typedef struct string_xref_t
{
   __u32 key;
   char  val[32];
} string_xref_t;

string_xref_t stream_id_xref[] =
{
   { MPEG_PROGRAM_END,    "PRG_END" },
   { MPEG_PACK_HDR,       "PACK   " },
   { MPEG_SYSTEM_HDR,     "SYSTEM " },
   { MPEG_AUD_STREAM_HDR, "AUDIO  " },
   { MPEG_VID_STREAM_HDR, "VIDEO  " },
   
};
// Globals
//
int           do_mpg_only = 0;
int           do_ndx_only = 0;
int           do_evt_file = 0;
int           ndx_exists  = 0;
int           mpg_exists  = 0;
int           verb        = 1;
int           hex_verb    = 0;
const char   *outfile     = NULL;
FILE         *outfd;
ndx_version_t ndx_ver;

unsigned int mpg_fpos    = 0;
unsigned int ndx_fpos    = 0;

u_char mpg_buf[READ_BUF_SIZE];
u_char skip_buf[READ_BUF_SIZE];

static unsigned long getBits (u_char *buf, int byte_offset, int startbit, int bitlen);


//+****************************************************
//
//+****************************************************
static char* str_xref(string_xref_t *st, __u32 key, int num_elem)
{
   int x;
   for ( x=0; x < num_elem; x++ )
   {
      if ( st[x].key == key ) {
         return(st[x].val);
      }
   }
   return(NULL);
}


//+****************************************************
//
//+****************************************************
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

//+****************************************************
//
//+****************************************************
static int process_ndx_header(int fd)
{
   size_t               cnt;
   char                 buf[32];
   rtv_ndx_30_header_t *hdr30; //32-bytes
   
   if ( (cnt = read(fd, buf, sizeof(rtv_ndx_30_header_t))) != sizeof(rtv_ndx_30_header_t) ) {
      UNEXPECTED("ERROR: unable to read header version info\n");
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
      UNEXPECTED("ERROR: unknown ndx header version\n");
      hex_dump("UNKNOWN HEADER", 0, buf, sizeof(rtv_ndx_30_header_t));
      return(-1);
   }
   
   return(ndx_ver);
}

//+****************************************************
//
//+****************************************************
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

//+****************************************************
//
//+****************************************************
static void prt_ndx_banner(void)
{
   PRT("\nrec         timestamp    delta_t        iframe_filepos                        iframe_size\n");
   PRT("-----------------------------------------------------------------------------------------------\n");
}

static void prt_pes_banner(void)
{
   PRT("\ntype                    address       size            pts\n");
   PRT("--------------------------------------------------------------\n");
}

static void prt_ndx_rec(int recno, ndx_rec_info_t *rp)
{
   PRT("%06d    %s   %8llu    0x%016llx = %-16llu  0x%08lx = %-8lu\n", 
       recno, format_seconds(rp->seconds), rp->delta_tm,
       rp->r.filepos_iframe, rp->r.filepos_iframe, rp->r.iframe_size, rp->r.iframe_size);
}

//+****************************************************
//
//+****************************************************
static void crunch_ndx_30_rec(int recno, ndx_rec_info_t *this, ndx_rec_info_t *last)
{
   static __u64 basetime = 0;

   if (recno == 0) {
      basetime      = this->r.timestamp;
      this->delta_tm = 0;
   }
   
   if (this->r.empty != 0) {
      UNEXPECTED("ndx recno %d: empty != 0\n", recno);
   }
   if ((this->r.filepos_iframe  % 4) != 0) {
      UNEXPECTED("ndx recno %d: filepos_iframe not on 4 byte boundry\n", recno);
   }
   if ((this->r.iframe_size  % 4) != 0) {
      UNEXPECTED("ndx recno %d: iframe_size not on 4 byte boundry\n", recno);
   }
   
   this->seconds = ((__s64)(this->r.timestamp - basetime)) / 1000000000.0;
   
   if ( recno > 0 ) {
      this->delta_tm = (__s64)(this->r.timestamp - last->r.timestamp) / 1000000; // convert to mS
      if ( (this->delta_tm < 480) || (this->delta_tm > 520) ) {
         UNEXPECTED("ndx recno %d: *****************Timestamp skew********************\n", recno);
      } 
   }
}

//+****************************************************
//
//+****************************************************
static int process_ndx_30(int ndx_fd)
{
   int recno      = 0;
   int cnt;
   ndx_rec_info_t this, last; 
      
   last = this;
   memset(&this, 0, sizeof this);
   if ( verb > 0 ) {
      prt_ndx_banner();
   }
   while ( (cnt = read(ndx_fd, &this.r, sizeof(rtv_ndx_30_record_t))) > 0 ) {      
      rtv_convert_30_ndx_rec(&this.r);
      crunch_ndx_30_rec(recno, &this, &last);
      if ( verb > 0 ) {
         prt_ndx_rec(recno, &this);      
      }
      memcpy(&last, &this, sizeof this);
      recno++;
      ndx_fpos += cnt;
   } //while
   
   PRT("\nDone. Processed %d ndx records\n", recno);
   return(0);
}

//+****************************************************
//
//+****************************************************
static void dump_vid_stream(u_char *buf, __u64 addr, int sz)
{
   int            pos = 0;
   unsigned long syncp;

   syncp = 0xFFFFFFFF;

   while (pos < sz-4) {
      u_char c;
      
      c = buf[pos+3];
      syncp = (syncp << 8) | c;
      
      if ( (syncp & 0xFFFFFF00) == 0x00000100 ) {
         if ( c <= 0xB8 ) {
            if ( c == 00 ) { //PICTURE
               unsigned int seq = getBits(&(buf[pos]), 4, 0, 10);
               unsigned int ipb = getBits(&(buf[pos]), 5, 2, 3);
               char ipb_ch;

               ipb_ch = (ipb==1) ? 'I' : ((ipb==2) ? 'P' : ((ipb==3) ? 'B' : 'X'));
               PRT("    ES:PICT %08lx:   %016llx                       seq=%u (%c)\n", syncp, addr+pos, seq, ipb_ch);
             }
            else if ( c >= 0xB0 ) {
               if ( c == 0xB2) {
                  PRT("    ES:USER %08lx:   %016llx\n", syncp, addr+pos);
               }
               else if ( c == 0xB3 ) {
                  PRT("    ES:SEQ  %08lx:   %016llx\n", syncp, addr+pos);
               }
               else if ( c == 0xB5 ) {
                  PRT("    ES:EXT  %08lx:   %016llx\n", syncp, addr+pos);
               }
               else if ( c == 0xB8 ) {
                  unsigned int hr    = getBits(&(buf[pos]), 4, 1, 5);
                  unsigned int min   = getBits(&(buf[pos]), 4, 6, 6);
                  unsigned int sec   = getBits(&(buf[pos]), 5, 5, 6);
                  unsigned int frame = getBits(&(buf[pos]), 6, 3, 6);
                  unsigned int cl    = getBits(&(buf[pos]), 7, 1, 1);
                  unsigned int br    = getBits(&(buf[pos]), 7, 2, 1);
                  unsigned int dr    = getBits(&(buf[pos]), 4, 0, 1);

                  PRT("    ES:GOP  %08lx:   %016llx        %02u:%02u:%02u.000   frame=%-3u dr=%u cl=%u br=%u\n", 
                      syncp, addr+pos, hr, min, sec, frame, dr, cl, br);
               }
               else  {
                  UNEXPECTED("    ES:????  %08lx:   %016llx\n", syncp, addr+pos);
               }
           }
            else {
               //slice
            }
         }
         else {
            UNEXPECTED("    ES:???? %08lx   %016llx\n", syncp, addr+pos);
         }
         syncp = 0xFFFFFFFF;
      }
      pos++;
   } //while
}

//+****************************************************
//  -- get bits out of buffer  (max 48 bit)
//  -- extended bitrange, so it's slower
//  -- return: value
//  based off DVBSNOOP
//+****************************************************
static long long getBits48 (u_char *buf, int byte_offset, int startbit, int bitlen)
{
   u_char *b;
   unsigned long long v;
   unsigned long long mask;
   unsigned long long tmp;
   
   if (bitlen > 48) {
      PRT("ERROR: getBits48() request out of bound!!!! (report!!) \n");
      return 0xFEFEFEFEFEFEFEFELL;
   }
   
   b = &buf[byte_offset + (startbit / 8)];
   startbit %= 8;
   
   // -- safe is 48 bitlen
   tmp = (unsigned long long)(
      ((unsigned long long)*(b  )<<48) + ((unsigned long long)*(b+1)<<40) +
      ((unsigned long long)*(b+2)<<32) + ((unsigned long long)*(b+3)<<24) +
      (*(b+4)<<16) + (*(b+5)<< 8) + *(b+6) );
   
   startbit = 56 - startbit - bitlen;
   tmp      = tmp >> startbit;
   mask     = (1ULL << bitlen) - 1;	// 1ULL !!!
   v        = tmp & mask;
   
   return v;
}


//+****************************************************
//  -- get bits out of buffer (max 32 bit!!!)
//  -- return: value
//  based off DVBSNOOP
//+****************************************************
static unsigned long getBits (u_char *buf, int byte_offset, int startbit, int bitlen)
{
   u_char *b;
   unsigned long  v;
   unsigned long mask;
   unsigned long tmp_long;
   int           bitHigh;
   
   
   b = &buf[byte_offset + (startbit >> 3)];
   startbit %= 8;
   
   switch ((bitlen-1) >> 3) {
   case -1:	// -- <=0 bits: always 0
		return 0L;
		break;
      
	case 0:		// -- 1..8 bit
 		tmp_long = (unsigned long)(
			(*(b  )<< 8) +  *(b+1) );
		bitHigh = 16;
		break;
      
	case 1:		// -- 9..16 bit
 		tmp_long = (unsigned long)(
		 	(*(b  )<<16) + (*(b+1)<< 8) +  *(b+2) );
		bitHigh = 24;
		break;
      
	case 2:		// -- 17..24 bit
 		tmp_long = (unsigned long)(
		 	(*(b  )<<24) + (*(b+1)<<16) +
			(*(b+2)<< 8) +  *(b+3) );
		bitHigh = 32;
		break;
      
	case 3:		// -- 25..32 bit
      // -- to be safe, we need 32+8 bit as shift range 
		return (unsigned long) getBits48 (b, 0, startbit, bitlen);
		break;
      
	default:	// -- 33.. bits: fail, deliver constant fail value
		PRT("ERROR: getBits() request out of bound!!!!\n");
		return (unsigned long) 0xFEFEFEFE;
		break;
   }
   
   startbit = bitHigh - startbit - bitlen;
   tmp_long = tmp_long >> startbit;
   mask     = (1ULL << bitlen) - 1;  // 1ULL !!!
   v        = tmp_long & mask;
   
   return v;
}

//+****************************************************
//
//+****************************************************
static void print_tb90kHz (long long time90kHz, char *time_str)

{
   long long ull = time90kHz;
   
   int     h,m,s,u;
   u_long  p = ull/90;
   
   // -- following lines taken from "dvbtextsubs  Dave Chapman"
   h=(p/(1000*60*60));
   m=(p/(1000*60))-(h*60);
   s=(p/1000)-(h*3600)-(m*60);
   u=p-(h*1000*60*60)-(m*1000*60)-(s*1000);
   
   //PRT("TimeBase: %llu (0x%08llx)\n", ull,ull);
   sprintf(time_str, "%d:%02d:%02d.%03d", h,m,s,u);
}

//+****************************************************
//
//+****************************************************
static void  PES_decodePACKHDR (u_char *b, pack_hdr_t *pk)

{
   unsigned int scr32_30 = getBits(b, 4, 2, 3);
   unsigned int scr29_15 = getBits(b, 4, 6, 15);
   unsigned int scr14_00 = getBits(b, 6, 6, 15);

   pk->scr_all       = (scr32_30 << 30) | (scr29_15 << 15) | scr14_00;
   pk->scr_ext       = getBits(b, 8, 6, 9);
   pk->prog_mux_rate = getBits(b, 10, 0, 22);

   //PRT("scr32..00: 0x%010llX (%llu)\n", pk->scr_all, pk->scr_all);
   //PRT("scr ext:          0x%03X (%u)\n", pk->scr_ext, pk->scr_ext);
   //PRT("mux_rate:      0x%06X (%u): bps=%u\n", pk->prog_mux_rate, pk->prog_mux_rate, 50 * 8 * pk->prog_mux_rate);
}


//+****************************************************
//
//+****************************************************
static void  PES_decodeAUDVIDHDR(u_char *b, audvid_hdr_t *av)

{
   unsigned int pts32_30;
   unsigned int pts29_15;
   unsigned int pts14_00;

   
   av->ptsdts_flags = getBits (b, 7, 0, 2);
   if ( av->ptsdts_flags & 0x02 ) {
      pts32_30 = getBits(b, 9, 4, 3);
      pts29_15 = getBits(b, 10, 0, 15);
      pts14_00 = getBits(b, 12, 0, 15);
      av->pts_all = (pts32_30 << 30) | (pts29_15 << 15) | pts14_00;
   }
   else {
      av->pts_all = 0;
   }

   //PRT("pts32..00: 0x%010llX (%llu)\n", av->pts_all, av->pts_all);
}


//+****************************************************
//
//+****************************************************
static void dump_pes(u_char *buf, __u64 addr, int sz)
{
#define MSTRSZ (128)
   __u32         stream_id = ntohl(*(__u32*)buf);
   char          msg[MSTRSZ];
   char          timestr[128];
   char         *pos = msg;
   char         *stream_str;
   unsigned int  pts_dts;
   pack_hdr_t    pack_hdr;
   audvid_hdr_t  av_hdr;

   stream_str = str_xref(stream_id_xref, stream_id, sizeof(stream_id_xref)/ sizeof(string_xref_t));
   if ( stream_str ) {
      pos += snprintf(msg, MSTRSZ-1, "%s (%08lx) %016llx %6d", stream_str, stream_id, addr, sz);
   }
   else {
      pos += snprintf(msg, MSTRSZ-1, "UNKNOWN (%08lx) %016llx %6d", stream_id, addr, sz);
   }

   switch (stream_id) {
   case MPEG_VID_STREAM_HDR:
   case MPEG_AUD_STREAM_HDR:
      pts_dts = getBits (buf, 6, 8, 2);
      PES_decodeAUDVIDHDR(buf, &av_hdr);
      if ( av_hdr.ptsdts_flags & 0x02 ) {
         print_tb90kHz(av_hdr.pts_all, timestr);
         pos += snprintf(pos, (MSTRSZ-1)-(pos-msg), "       %s", timestr);
      }
      //pos += snprintf(pos, (MSTRSZ-1)-(pos-msg), "   (%1d)(%1d)", av_hdr.ptsdts_flags, pts_dts);
      break;
   case MPEG_PACK_HDR:
      PES_decodePACKHDR(buf, &pack_hdr);
      print_tb90kHz(pack_hdr.scr_all, timestr);
      pos += snprintf(pos, (MSTRSZ-1)-(pos-msg), "       %s", timestr);
      break;
   case MPEG_SYSTEM_HDR:
      break;
   case MPEG_PROGRAM_END:
      break;
   default:
      UNEXPECTED("Unknown streamID: 0x%08lx\n", stream_id);
   } //switch

   if ( hex_verb == 1 ) {
      PRT("\n");
      hex_dump(stream_str, addr, buf, (sz > 64) ? 64 : sz);      
      PRT("\n");
   }
   else if ( hex_verb == 2 ) {
      PRT("\n");
      hex_dump(stream_str, addr, buf, sz);      
      PRT("\n");
   }

   if ( verb > 0 ) {
      PRT("%s\n", msg);
   }

   if ( (verb > 1) && (stream_id == MPEG_VID_STREAM_HDR) ) {
      dump_vid_stream(buf, addr, sz);
   }
}


//+********************************
// pes_SyncRead: based off DVBSNOOP
//+********************************
static int pes_SyncRead (int fd, u_char *buf, u_long len, u_long *skipped_bytes)
{

   unsigned int  skipbuf_pos = 0;
   int           n1,n2;
   unsigned long l;
   unsigned long syncp;   
   
   // -- simple PES sync... seek for 0x000001 (PES_SYNC_BYTE)
   // -- $$$ Q: has this to be byteshifted or bit shifted???
   //
   // ISO/IEC 13818-1:
   // -- packet_start_code_prefix -- The packet_start_code_prefix is
   // -- a 24-bit code. Together with the stream_id that follows it constitutes
   // -- a packet start code that identifies the beginning of a packet.
   // -- The packet_start_code_prefix  is the bit string '0000 0000 0000 0000
   // -- 0000 0001' (0x000001).
   // ==>   Check the stream_id with "dvb_str.c", if you do changes!
   
   
   *skipped_bytes = 0;
   syncp = 0xFFFFFFFF;
   while (1) {
      u_char c;
      
      n1 = read(fd,buf+3,1);
      if (n1 <= 0) {
         if ( n1 == 0 ) {
            PRT("INFO: MPEG EOF.\n");
         }
         else {
            UNEXPECTED("%s: read failed: %d<=>%s\n", __FUNCTION__, errno, strerror(errno));
         }
         return n1;
      }
      
      // -- byte shift for packet_start_code_prefix
      // -- sync found? 0x000001 + valid PESstream_ID
      // -- $$$ check this if streamID defs will be enhanced by ISO!!!
      
      c = buf[3];
      syncp = (syncp << 8) | c;
      if ( (syncp & 0xFFFFFF00) == 0x00000100 ) {
         if (c >= 0xBC)  {
            break;
         }
         else if ( c == 0xBA ||  c== 0xBB || c == 0xb9 ) {
            break;
         }
      }
      
      if ( skipbuf_pos < READ_BUF_SIZE ) {
         skip_buf[skipbuf_pos++] = c;
      }
      else if ( skipbuf_pos == READ_BUF_SIZE ) {
         UNEXPECTED("%s: HOLY CRAP: Skipping more than READ_BUF_SIZE bytes: %u\n", __FUNCTION__, READ_BUF_SIZE);
      }
   }
   
   
   // -- Sync found!
   //
   *skipped_bytes = skipbuf_pos - 3;
   buf[0] = 0x00;   // write packet_start_code_prefix to buffer
   buf[1] = 0x00;
   buf[2] = 0x01;
   // buf[3] = streamID == recent read
     
   if ( buf[3] == 0xBA ) {
      int padding;

      //PACK Header. Length is 14 bytes plus padding specified by byte 14 bits 2..0
      //
      n1 = read(fd,buf+4, 10);
      if (n1 <= 0) {
         UNEXPECTED("%s: read failed (PACK): %d<=>%s\n", __FUNCTION__, errno, strerror(errno));
         return(n1);
      }
      else if ( n1 < 10 ) {
         UNEXPECTED("%s: PACK short READ: got %d expected 10\n", __FUNCTION__, n1);
         return(-1);
      }
      padding = buf[13] & 0x07;
      if ( padding == 0 ) {
         return(14);
      }

      n1 = read(fd,buf+14, padding);
      if (n1 <= 0) {
         UNEXPECTED("%s: read failed (PACK) Padding: %d<=>%s\n", __FUNCTION__, errno, strerror(errno));
         return(n1);
      }
      else if ( n1 < padding ) {
         UNEXPECTED("%s: PACK Padding short READ: got %d expected 10\n", __FUNCTION__, n1);
         return(-1);
      }
      return(14+n1);

   }
   else if ( buf[3] == 0xB9 ) {
      //Program END
      return(4);
   }
   
   // -- read more 2 bytes (packet length)
   // -- read rest ...
   
   n1 = read(fd, buf+4, 2);
   if (n1 <= 0) {
      UNEXPECTED("%s: read failed (LEN): %d<=>%s\n", __FUNCTION__, errno, strerror(errno));
      return(n1);
   }
   if (n1 == 2) {
      l = (buf[4]<<8) + buf[5];		// PES packet size...
      n1 = 6; 				// 4+2 bytes read
      // $$$ TODO    if len == 0, special unbound length

      if ( (l + 6) > len ) {
         UNEXPECTED("%s: Excessive PKT len: %lu\n", __FUNCTION__, l);
      }
      if (l > 0) {
         n2 = read(fd, buf+6, (unsigned int) l );
         if (n2 <= 0) {
            UNEXPECTED("%s: read failed (DATA): %d<=>%s\n", __FUNCTION__, errno, strerror(errno));
            return(n1);
         }
         n1 = (n2 < 0) ? n2 : n1+n2;
      }
   }
   
   return n1;
}



//+****************************************************
//
//+****************************************************
static int process_mpg(int mpg_fd, int ndx_fd)
{
   int            recno       = 0;
   int            ndx_done    = 0;
   int            mpg_done    = 0;
   int            ndx_cnt     = 0;
   int            mpg_cnt     = 0;
   unsigned long  audio_addr  = 0;
   search_state_t state;
   ndx_rec_info_t this, last;
   unsigned long skipped_bytes;


   if ( ndx_fd != -1 ) {
      // Setup for ndx file processing
      state = SRCH_VIDEO;      
      last = this;
      memset(&this, 0, sizeof this);

      // get first ndx rec
      if ( (ndx_cnt = read(ndx_fd, &this.r, sizeof(rtv_ndx_30_record_t))) <= 0 ) {
         UNEXPECTED("WARNING: Failed to read first ndx record. Doing mpg processing only.\n");
         if ( ndx_cnt == -1 ) {
            UNEXPECTED("ERROR: %d<=>%s\n", errno, strerror(errno));
         }
         state    = SRCH_NDX_DONE;
         ndx_done = 1;
      }
      else {
         rtv_convert_30_ndx_rec(&this.r);
      }
   }
   else {
      state    = SRCH_NDX_DONE; //mpg file only
      ndx_done = 1;
   }

   // Process the mpg & ndx files.
   //
   if ( verb > 0 ) {
      prt_pes_banner();
   }
   while ( !(ndx_done) || !(mpg_done) ) {

      if (  !(mpg_done) ) {
         mpg_cnt = pes_SyncRead (mpg_fd, mpg_buf, READ_BUF_SIZE, &skipped_bytes);
         if ( mpg_cnt <= 0 ) {
            mpg_done = 1;
            state = SRCH_MPG_DONE;
            continue;
         }

         // Handle skipped bytes
         //
         if ( skipped_bytes ) {
            if ( mpg_fpos != 0 ) {
               UNEXPECTED("Skipped bytes: fpos=0x%08x cnt=%lu\n", mpg_fpos, skipped_bytes);
            }
            if ( hex_verb == 2 ) {
               hex_dump("MPG SKIPPED BYTES", mpg_fpos, skip_buf, skipped_bytes);      
            }
            else if ( hex_verb > 0 ) {
               hex_dump("MPG SKIPPED BYTES", mpg_fpos, skip_buf, 64);      
            }
            mpg_fpos += skipped_bytes;
         }
      }

      //PRT("%s: found header: %08x  mpg_pos=%08x len=%d\n", __FUNCTION__, ntohl(*(__u32*)mpg_buf), mpg_fpos, mpg_cnt);

      // See if ndx is out of wack with mpg. If so, try to sync back up.
      //
      if ( !(ndx_done) &&  !(mpg_done) && (this.r.filepos_iframe < mpg_fpos) ) {
         UNEXPECTED("NDX Record I-frame not found: fpos=0x%08x ndx_iframe=0x%016llx\n", mpg_fpos, this.r.filepos_iframe);
         if ( verb > 0 ) {
            prt_ndx_banner();
         }
         while (  this.r.filepos_iframe < mpg_fpos ) {

            crunch_ndx_30_rec(recno, &this, &last);
            if ( verb > 0 ) {
               prt_ndx_rec(recno, &this);
            }      
            
            memcpy(&last, &this, sizeof this);
            recno++;
            ndx_fpos += ndx_cnt;
            
            ndx_cnt = read(ndx_fd, &this.r, sizeof(rtv_ndx_30_record_t));
            if ( ndx_cnt == -1 ) {
               UNEXPECTED("ERROR: ndx read read failed: %d<=>%s\n", errno, strerror(errno));
               state    = SRCH_NDX_DONE;
               ndx_done = 1;
               break;
            }
            else if ( ndx_cnt == 0 ) {
               UNEXPECTED("End of ndx file\n");
               state    = SRCH_NDX_DONE;
               ndx_done = 1;
               break;
            }
            rtv_convert_30_ndx_rec(&this.r);
         } //while
         if ( verb > 0 ) {
            prt_pes_banner();
         }
      } // if ndx out of wack
      

      switch (state) {

      case SRCH_VIDEO: {
         if ( this.r.filepos_iframe == mpg_fpos ) {
            if ( verb > 0 ) {
               prt_ndx_banner();
            }
            crunch_ndx_30_rec(recno, &this, &last);
            if ( verb > 0 ) {
               prt_ndx_rec(recno, &this);
               prt_pes_banner();
            }
            audio_addr = this.r.filepos_iframe + this.r.iframe_size;
            state = SRCH_AUDIO_PACK;

            //verify stream id
            //
            if ( ntohl(*(__u32*)mpg_buf) != MPEG_VID_STREAM_HDR ) {
               UNEXPECTED("mpeg VIDEO I-frame start expected: got 0x%08x\n", ntohl(*(__u32*)mpg_buf) );
            }

            // get next ndx rec
            //
            memcpy(&last, &this, sizeof(this));
            recno++;
            ndx_fpos += ndx_cnt;
            ndx_cnt = read(ndx_fd, &this.r, sizeof(rtv_ndx_30_record_t));
            if ( ndx_cnt == -1 ) {
               UNEXPECTED("ERROR: ndx read read failed: %d<=>%s\n", errno, strerror(errno));
               state    = SRCH_NDX_DONE;
               ndx_done = 1;
            }
            else if ( ndx_cnt == 0 ) {
               PRT("INFO: NDX EOF\n");
               state    = SRCH_NDX_DONE;
               ndx_done = 1;
            }
            else {
               rtv_convert_30_ndx_rec(&this.r);
            }
         } //if ndx match
         
         break;
      }

      case SRCH_AUDIO_PACK: {
         if ( audio_addr ) {
            if ( (ntohl(*(__u32*)mpg_buf) == MPEG_VID_STREAM_HDR) &&
                 (mpg_fpos == (audio_addr - 16))                   ) {
               PRT("INFO: RUNT VIDEO PES while searching for NDX AUDIO/PACK\n");
               break;
            }
            if ( (ntohl(*(__u32*)mpg_buf) != MPEG_AUD_STREAM_HDR) &&
                 (ntohl(*(__u32*)mpg_buf) != MPEG_PACK_HDR)       ) {
               UNEXPECTED("mpeg AUDIO I-frame or PACK start expected: got 0x%08x\n",  ntohl(*(__u32*)mpg_buf));
            }
            if ( audio_addr != mpg_fpos ) {
               UNEXPECTED("mpeg AUDIO/PACK address mismatch: got 0x%08lx expected 0x%08x\n", audio_addr, mpg_fpos);
            }
            audio_addr = 0;
            state = SRCH_VIDEO;
         }
         
         break;
      }

      case SRCH_NDX_DONE:
         // Nothing to do
         break;

      case SRCH_MPG_DONE:
         break;

      default:
         UNEXPECTED("SW ERROR: state=%d\n", state);
      } //switch
      
      if ( !(mpg_done) ) {
         dump_pes(mpg_buf, mpg_fpos, mpg_cnt);
         mpg_fpos += mpg_cnt;
      }

   } //while

   PRT("\nDone. Processed %d ndx records\n", recno);
   return(0);
}

//+****************************************************
//
//+****************************************************
static int process_evt(int evt_fd)
{
   int          pos = 0;
   int          cnt;
   struct stat  fst;
   char        *buf;

   rtv_chapter_mark_parms_t ch_parms;
   rtv_chapters_t           chapters;

   if ( fstat(evt_fd, &fst) != 0 ) {
      UNEXPECTED("%s: fstat failed: %d<=>%s\n", __FUNCTION__, errno, strerror(errno));
      return(-1);
   }

   if ( fst.st_size > MAX_EVT_FILE_SZ ) {
      UNEXPECTED("%s: File too large: sz: %ld\n", __FUNCTION__, (long)fst.st_size);
      return(-1);
   }
   PRT("Evt file size: %ld\n", (long)fst.st_size);
   PRT("Evt file recs: %lu\n", ((long)fst.st_size - RTV_EVT_HDR_SZ) / sizeof(rtv_evt_record_t));

   
   buf = malloc(fst.st_size);
   if ( buf == NULL ) {
      UNEXPECTED("%s: malloc failed: sz: %ld\n", __FUNCTION__, (long)fst.st_size);
      return(-1);
   } 

   while ( (cnt = read(evt_fd, &(buf[pos]), 4096)) > 0 ) {      
      pos += cnt;
   } //while

   if ( cnt == -1 ) {
      UNEXPECTED("%s: read failed: %d<=>%s\n", __FUNCTION__, errno, strerror(errno));
      return(-1);
   }
   if ( fst.st_size != pos ) {
      UNEXPECTED("%s: file size mismatch: fstat=%ld read_sz=%d\n", __FUNCTION__, (long)fst.st_size, pos);
      return(-1);
   }
   
   // rtvlib logging level
   //
   if ( verb > 0 ) {
      rtv_set_dbgmask(0x80);
   }

   ch_parms.p_seg_min = 180; //seconds
   ch_parms.scene_min = 3;   //seconds
   ch_parms.buf       = buf;
   ch_parms.buf_sz    = fst.st_size;
   rtv_parse_evt_file( ch_parms, &chapters);

   if ( hex_verb > 0 ) {
      int x;
      int nrec              = (fst.st_size - RTV_EVT_HDR_SZ) / sizeof(rtv_evt_record_t);
      rtv_evt_record_t *rec = (rtv_evt_record_t*)(buf + RTV_EVT_HDR_SZ);

      PRT("\n\n");
      hex_dump("EVT FILE HDR", 0, buf,  RTV_EVT_HDR_SZ);      
      
      PRT("\nrec          timestamp                           aud/vid          audiopower              blacklevel             unknown\n");
      PRT("--------------------------------------------------------------------------------------------------------------------------\n");
      for ( x = 0; x < nrec; x++ ) {
         PRT("%05d   0x%016llx=%016llu   0x%08lx     0x%08lx=%010lu     0x%08lx=%010lu     0x%08lx\n", 
             x, rec[x].timestamp, rec[x].timestamp, rec[x].data_type, 
             rec[x].audiopower, rec[x].audiopower, rec[x].blacklevel,  rec[x].blacklevel, rec[x].unknown1);
      }      
   }

   return(0);
}

//+****************************************************
//
//+****************************************************
static void usage(char *name) 
{
   fprintf(stderr, "\n\n%s: A tool for parsing ReplayTV 5K mpg files, ndx files, and evt files.\n", name);
   fprintf(stderr, "Usage: %s [options] <file base name>\n", name);
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "   -f <output file name> : write results to output file\n");
   fprintf(stderr, "   -n                    : only process ndx file (skip mpg parsing)\n");
   fprintf(stderr, "   -m                    : only process mpg file (skip ndx parsing)\n");
   fprintf(stderr, "   -e                    : process evt file\n");
   fprintf(stderr, "   -v <level>            : results verbosity level (default = 1)\n");
   fprintf(stderr, "       level=0           : Quiet. Only report errors\n");
   fprintf(stderr, "       level=1           : Report ndx & mpg file timestamps\n");
   fprintf(stderr, "       level=2           : Report ndx & mpg file timestamps, report video ES info\n");
   fprintf(stderr, "   -h <level>            : hex dump level (default = 0)\n");
   fprintf(stderr, "       level=0           : Quiet.\n");
   fprintf(stderr, "       level=1           : Dump first 64 bytes of streams.\n");
   fprintf(stderr, "       level=2           : Dump all data..\n");

}


//+****************************************************
//
//+****************************************************
int main(int argc, char ** argv)
{
   int     ndx = -1;
   int     evt = -1;
   int     mpg = -1;
   char    filename[1024];
   char   *basefile;
   char    ch;

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
      case 'm':
         do_mpg_only = 1;
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
      evt = open(filename, O_RDONLY);
      if (evt == -1) {
         UNEXPECTED("ERROR: Couldn't open %s: %d<=>%s\n", filename, errno, strerror(errno));
         exit(1);
      }
      PRT("Found evt file: %s\n", filename);

      //process evt file
      process_evt(evt);
      exit(0);
   }

   //Check/open ndx file
   if ( !(do_mpg_only) ) {
      sprintf(filename, "%s.ndx", basefile);
      ndx = open(filename, O_RDONLY);
      if (ndx == -1) {
         if ( do_ndx_only ) {
            UNEXPECTED("ERROR: Couldn't open %s: %d<=>%s\n", filename, errno, strerror(errno));
            exit(1);
         }
      }
      else {
         ndx_exists = 1;
         PRT("Found ndx file: %s\n", filename);
      }
   }

   //Check/open mpg file
   if ( !(do_ndx_only) ) {
      sprintf(filename, "%s.mpg", basefile);
      mpg = open(filename, O_RDONLY);
      if (mpg == -1) {
         UNEXPECTED("ERROR: Couldn't open mpg file: %s: %d<=>%s\n", filename, errno, strerror(errno));
         exit(1);
      } 
      else {
         PRT("Found mpg file: %s\n", filename);
         mpg_exists = 1;
      }
   }


   if ( !(ndx_exists) && !(mpg_exists) ) {
      UNEXPECTED("ERROR: neither ndx or mpg file found\n");      
      exit(0);
   }

   PRT("------------------------------------------------\n\n");
   
   rtv_init_lib();

   if ( ndx_exists ) {
      if ( process_ndx_header(ndx) < 0 ) {
         exit(1);
      }
      if ( ndx_ver == NDX_VER_22 ) {
         UNEXPECTED("ERROR: RTV 4K header format not supported\n");
         exit(1);
      }
      else if ( ndx_ver != NDX_VER_30 ) {
         UNEXPECTED("ERROR: Unknown ndx header format.\n");
         exit(1);
      }
   }

   if ( ndx_exists && !(mpg_exists) ) {
      PRT("\n*** PERFORMING NDX ONLY FILE PROCESSING ***\n\n");
      process_ndx_30(ndx);
   }
   else if ( mpg_exists && !(ndx_exists) ) {
      PRT("\n*** PERFORMING MPG ONLY FILE PROCESSING ***\n\n");
      process_mpg(mpg, -1);
   }
   else if ( mpg_exists && ndx_exists ) {
      PRT("\n*** PERFORMING MPG AND NDX FILE PROCESSING ***\n\n");
      process_mpg(mpg, ndx);
   }
   else {
      UNEXPECTED("ERROR: Im so confused......\n");
      exit(1);
   }

   exit(0);
}
