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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "rtv.h"
#include "rtvlib.h"
#include "ndxclient.h"


static const rtv_ndx_30_record_t zero_time_ndx30_rec = { 0ULL, 0ULL, 0, 0 };

static rtv_ndx_info_t ndx_info;

//+*********************************************************************
//  Name: 
//+*********************************************************************

//+*********************************************************************
//  Name: 
//+*********************************************************************

//+*************************************************************************************************************
//           PUBLIC
//+*************************************************************************************************************


//+*********************************************************************
//  Name: rtv_open_ndx_file
//  Opens a ndx file. cache_sz specifies the number of ndx records to cache.
//+*********************************************************************
int rtv_open_ndx_file(const rtv_device_info_t    *devinfo, 
                      const char                 *filename, 
                      unsigned int                cache_sz )
{
   int                   rc = 0;

   rtv_fs_file_t         fileinfo;
   rtv_http_resp_data_t  file_data;

   memset(&ndx_info, 0, sizeof(rtv_ndx_info_t));

   if ( strlen(filename) > NDX_MAX_PATHNAME_LEN - 1 ) {
      RTV_ERRLOG("%s: Pathname too long\n", __FUNCTION__);
      return(-EINVAL);
   } 
   strcpy(ndx_info.filename, filename);
   RTV_DBGLOG(RTVLOG_NDX, "%s: Open: %s\n",  __FUNCTION__, ndx_info.filename);
      
   ndx_info.device = devinfo;

   // Get file info
   //
   rc = rtv_get_file_info(devinfo, ndx_info.filename, &fileinfo );
   if ( (rc != 0) ||  (fileinfo.size <= sizeof(rtv_ndx_30_header_t)) ) {
      if ( rc != 0 ) {
         RTV_ERRLOG("%s: NDX file open failed\n", __FUNCTION__);
      }
      else {
         RTV_ERRLOG("%s: Invalid file format\n", __FUNCTION__);
         rc = -ENODATA;
      }
      rtv_free_file_info(&fileinfo);
      return(rc);
   } 

   // Read the file header and setup our data structures
   //
   rc = rtv_read_file(devinfo, ndx_info.filename, 0, sizeof(rtv_ndx_30_header_t), &file_data);
   if ( rc != 0 ) {
      RTV_ERRLOG("Ndx file read failed: %s: %d\n", ndx_info.filename, rc);
      rtv_free_file_info(&fileinfo);
      return(rc);
   }
   if ( RTVLOG_NDX ) {
      rtv_hex_dump("NDX HDR", 0, file_data.data_start, sizeof(rtv_ndx_30_header_t), 0);
   }

   ndx_info.rec_cnt_to_load = cache_sz;
   ndx_info.file_sz         = fileinfo.size;

   if ( (file_data.data_start[0] == 3) && (file_data.data_start[1] == 0) ) {
      ndx_info.ver    = RTV_NDX_30;
      ndx_info.hdr_sz = sizeof(rtv_ndx_30_header_t);
      ndx_info.rec_sz = sizeof(rtv_ndx_30_record_t);
      if ( ((fileinfo.size - sizeof(rtv_ndx_30_header_t)) % sizeof(rtv_ndx_30_record_t)) != 0 ) {
         RTV_WARNLOG("ndx file size not consistant with record size\n\n");
      }
      ndx_info.num_rec_in_file = (fileinfo.size - sizeof(rtv_ndx_30_header_t)) / sizeof(rtv_ndx_30_record_t);
      if ( ndx_info.rec_cnt_to_load > ndx_info.num_rec_in_file ) {
         ndx_info.rec_cnt_to_load = ndx_info.num_rec_in_file;
      }
   }
   else if ( (file_data.data_start[0] == 2) && (file_data.data_start[2] == 0) ) {
      ndx_info.ver    = RTV_NDX_22;
      ndx_info.hdr_sz = sizeof(rtv_ndx_22_header_t);
      ndx_info.rec_sz = sizeof(rtv_ndx_22_record_t);
      if ( ((fileinfo.size - sizeof(rtv_ndx_22_header_t)) % sizeof(rtv_ndx_22_record_t)) != 0 ) {
         RTV_WARNLOG("ndx file size not consistant with record size\n\n");
      }
      ndx_info.num_rec_in_file = (fileinfo.size - sizeof(rtv_ndx_22_header_t)) / sizeof(rtv_ndx_22_record_t);
      if ( ndx_info.rec_cnt_to_load > ndx_info.num_rec_in_file ) {
         ndx_info.rec_cnt_to_load = ndx_info.num_rec_in_file;
      }
      rc = -ENOTSUP; // We don't support RTV 4K ndx files.
   }
   else {
      RTV_ERRLOG("Invalid ndx file version: %d %d\n", file_data.data_start[0], file_data.data_start[1]);
      rtv_free_file_info(&fileinfo);
      free(file_data.buf);
      return(-EILSEQ);
   }

   RTV_DBGLOG(RTVLOG_NDX, "NDX: ver=%d size=%u rec=%u\n", ndx_info.ver, ndx_info.file_sz, ndx_info.num_rec_in_file);

   rtv_free_file_info(&fileinfo);
   free(file_data.buf);
   return(rc);
}

//+*********************************************************************
//  Name: rtv_close_ndx_file
//  Closes currently open ndx file
//+*********************************************************************
int rtv_close_ndx_file( void )
{
   if ( ndx_info.recs_in_mem ) {
      free(ndx_info.file_chunk.buf);
      ndx_info.recs_in_mem = 0;
   }
   return(0);
}

//+*********************************************************************
//  Name: rtv_get_ndx30_rec
//  returns 0 for success. Returns record in rec
//
//+*********************************************************************
int rtv_get_ndx30_rec(unsigned int time_ms, rtv_ndx_30_record_t *rec)
{
   unsigned int         max_time_ms = (ndx_info.num_rec_in_file / 2) * 1000; //length of show 

   unsigned int         rd_sz, rd_pos;
   int                  rc;
   int                  rec_no, start_rec;
   rtv_ndx_30_record_t *ndx_recs;
   rtv_ndx_30_record_t  tmp_rec;
   
   if ( ndx_info.ver != RTV_NDX_30 ) {
      RTV_ERRLOG("%s: invalid ndx version: %d\n", __FUNCTION__, ndx_info.ver);
      return(-ENOTSUP);
   }
   
   if ( (max_time_ms < 10000) ) { //10 sec
      time_ms = 0;
   }
   else if ( time_ms > (max_time_ms - 3000) ) {
      time_ms = max_time_ms - 3000;
   }
   RTV_DBGLOG(RTVLOG_NDX, "-->%s: time_ms=%u\n", __FUNCTION__, time_ms);
   
   if ( time_ms == 0 ) {
      memcpy(rec, &zero_time_ndx30_rec, sizeof(rtv_ndx_30_record_t));
      if ( RTVLOG_INFO ) {
         rtv_print_30_ndx_rec("JumpToZero  ", 0, rec);
      }
      return(0);
   }

   rec_no = (time_ms / 500) - 1; //2 rec per sec   

   // Check if we need to load a new ndx file block.
   // If so make the record we are looking for 25% of the way into the block.
   //
   if ( (ndx_info.recs_in_mem == 0)                              ||
        (rec_no < ndx_info.start_rec_num)                        || 
        (rec_no >= (ndx_info.start_rec_num + ndx_info.recs_in_mem)) ) {
      
      if ( ndx_info.recs_in_mem ) {
         free(ndx_info.file_chunk.buf); //free current cached records
         ndx_info.recs_in_mem = 0;
      }

      start_rec = rec_no - (ndx_info.rec_cnt_to_load / 4);
      if ( start_rec < 0 ) {
         start_rec = 0;
      }
      else if ( start_rec > (ndx_info.num_rec_in_file - (ndx_info.rec_cnt_to_load * 3/4)) ) {
         start_rec = ndx_info.num_rec_in_file - ndx_info.rec_cnt_to_load; 
         if ( start_rec < 0 ) {
            start_rec = 0;
         }
      }

      RTV_DBGLOG(RTVLOG_NDX, "NDX: loadchunk: start=%d cnt=%d end=%d, rif=%d\n", 
                 start_rec, ndx_info.rec_cnt_to_load, start_rec+ndx_info.rec_cnt_to_load-1, ndx_info.num_rec_in_file);

      rd_pos = (start_rec * ndx_info.rec_sz) + ndx_info.hdr_sz;
      rd_sz  = ndx_info.rec_sz * ndx_info.rec_cnt_to_load;
      if ( (rd_pos + rd_sz) > ndx_info.file_sz ) {
         rd_sz = ndx_info.file_sz - rd_pos; //truncate read to not go past EOF
      }

      rc = rtv_read_file(ndx_info.device, ndx_info.filename, rd_pos, rd_sz, &(ndx_info.file_chunk));
      if ( rc != 0 ) {
         RTV_ERRLOG("%s: NDX file chunk read failed\n", __FUNCTION__);
         return(rc);
      }
      ndx_info.start_rec_num = start_rec;
      ndx_info.recs_in_mem   = rd_sz / ndx_info.rec_sz;

      if ( RTVLOG_INFO ) {
         ndx_recs = (rtv_ndx_30_record_t*)ndx_info.file_chunk.data_start;
         memcpy(&tmp_rec, &(ndx_recs[0]), sizeof(rtv_ndx_30_record_t));
         rtv_convert_30_ndx_rec(&tmp_rec);
         rtv_print_30_ndx_rec("LoadStartRec", ndx_info.start_rec_num, &tmp_rec);
         memcpy(&tmp_rec, &(ndx_recs[ndx_info.recs_in_mem-1]), sizeof(rtv_ndx_30_record_t));
         rtv_convert_30_ndx_rec(&tmp_rec);
         rtv_print_30_ndx_rec("LoadEndRec  ", ndx_info.start_rec_num + ndx_info.recs_in_mem - 1, &tmp_rec);
      }
   }
   
   // Get the ndx rec from cache & return to the caller
   //
   ndx_recs = (rtv_ndx_30_record_t*)ndx_info.file_chunk.data_start;
   
   if ( (rec_no < ndx_info.start_rec_num) || 
        (rec_no >= (ndx_info.start_rec_num + ndx_info.recs_in_mem)) ) {
      RTV_ERRLOG("%s: NDX processing SW BUG: rec=%d start=%d cnt=%d\n", __FUNCTION__, rec_no, ndx_info.start_rec_num, ndx_info.recs_in_mem);
      return(-ESPIPE);
   }

   memcpy(rec, &(ndx_recs[rec_no - ndx_info.start_rec_num]), sizeof(rtv_ndx_30_record_t));
   rtv_convert_30_ndx_rec(rec);
   if ( RTVLOG_INFO ) {
      rtv_print_30_ndx_rec("JumpTo      ", rec_no, rec);
   }
   return(0);
}
