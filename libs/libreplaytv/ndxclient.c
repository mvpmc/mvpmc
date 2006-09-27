/*
 *  Copyright (C) 2005-2006, John Honeycutt
 *  http://www.mvpmc.org/
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
static const rtv_ndx_22_record_t zero_time_ndx22_rec = { 0, 0, 0, 0, 0, 0, 0, 0, 0ULL, 0ULL};

static rtv_4k_ndx_slice_list_t ndx_slice_list = { .num_slices = 0, .sec_per_slice = RTV_NUM_SEC_PER_SLICE, .slice = NULL }; //4K only
static rtv_ndx_info_t          ndx_info;

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
      ndx_info.max_time_ms     = (ndx_info.num_rec_in_file / 2) * 1000; 
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
   unsigned int         rd_sz, rd_pos;
   int                  rc;
   int                  rec_no, start_rec;
   rtv_ndx_30_record_t *ndx_recs;
   rtv_ndx_30_record_t  tmp_rec;
   
   if ( ndx_info.ver != RTV_NDX_30 ) {
      RTV_ERRLOG("%s: invalid ndx version: %d\n", __FUNCTION__, ndx_info.ver);
      return(-ENOTSUP);
   }
   
   if ( (ndx_info.max_time_ms < 10000) ) { //10 sec
      time_ms = 0;
   }
   else if ( time_ms > (ndx_info.max_time_ms - 3000) ) {
      time_ms = ndx_info.max_time_ms - 3000;
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


//+*********************************************************************
//  Name: rtv_get_ndx22_rec
//  returns 0 for success. Returns record in rec
//
//  Description:
//  RTV 4K's don't dump an ndx record exactly every 1/2 second 
//  like 5K's do. 
//  So the algorithm we use to find where to jump is:
//  During init build an ndx record slice array that contains ndx
//  record pointers for every X seconds.
//  This gives us a starting point to know the start & end record number
//  for a X second block of records. eg: a 4 minute block.
//  When jumping, calculate the slice then interpolate a record within
//  the slice for the time we want to jump to.
//  Finally, starting at the record we calculated, read it and do a linear 
//  search for our timestamp.
//  Using this method we shouldn't be off by more than 10 or so records 
//  when jumping.
//
//+*********************************************************************
int rtv_get_ndx22_rec(unsigned int time_ms, rtv_ndx_22_record_t *rec)
{
   int max_srch_cnt = 10;

   unsigned int         rd_sz, rd_pos;
   int                  rc;
   int                  rec_no, start_rec;
   int                  slice_idx;
   rtv_ndx_22_record_t *ndx_recs;
   rtv_ndx_22_record_t  tmp_rec;
   
   if ( ndx_info.ver != RTV_NDX_22 ) {
      RTV_ERRLOG("%s: invalid ndx version: %d\n", __FUNCTION__, ndx_info.ver);
      return(-ENOTSUP);
   }
   
   if ( (ndx_info.max_time_ms < 10000) ) { //10 sec
      time_ms = 0;
   }
   else if ( time_ms > (ndx_info.max_time_ms - 3000) ) {
      time_ms = ndx_info.max_time_ms - 3000;
   }
   RTV_DBGLOG(RTVLOG_NDX, "-->%s: time_ms=%u\n", __FUNCTION__, time_ms);
   
   if ( time_ms == 0 ) {
      memcpy(rec, &zero_time_ndx22_rec, sizeof(rtv_ndx_22_record_t));
      if ( RTVLOG_INFO ) {
         rtv_print_22_ndx_rec("JumpToZero  ", 0, rec);
      }
      return(0);
   }

   slice_idx = (time_ms / 1000) / RTV_NUM_SEC_PER_SLICE;
   if ( RTVLOG_NDX ) {
      char startstr[30], jumpstr[30];
      
      rtv_format_ts_ms32_min_sec_ms(ndx_slice_list.slice[slice_idx].start_ms, startstr);
      rtv_format_ts_ms32_min_sec_ms(time_ms, jumpstr);
      RTV_PRT("NDX-Slice: jmpto=%s  idx=%d  starttime=%s startrec=%d numrec=%d\n", 
              jumpstr, slice_idx, startstr, ndx_slice_list.slice[slice_idx].start_rec, ndx_slice_list.slice[slice_idx].num_recs);
   }

   // Check if we need to load a new ndx slice.
   // If so make the record we are looking for 25% of the way into the block.
   //
   if ( (ndx_info.recs_in_mem == 0)                              ||
        (ndx_slice_list.slice[slice_idx].start_rec != ndx_info.start_rec_num) ) {
      
      if ( ndx_info.recs_in_mem ) {
         free(ndx_info.file_chunk.buf); //free current cached records
         ndx_info.recs_in_mem = 0;
      }

      start_rec = ndx_slice_list.slice[slice_idx].start_rec;
      RTV_DBGLOG(RTVLOG_NDX, "NDX: loadchunk: start=%d cnt=%d end=%d, rif=%d\n", 
                 start_rec, ndx_slice_list.slice[slice_idx].num_recs, start_rec+ndx_slice_list.slice[slice_idx].num_recs-1, ndx_info.num_rec_in_file);

      rd_pos = (start_rec * ndx_info.rec_sz) + ndx_info.hdr_sz;
      rd_sz  = ndx_info.rec_sz * ndx_slice_list.slice[slice_idx].num_recs;
      if ( (rd_pos + rd_sz) > ndx_info.file_sz ) {
         rd_sz = ndx_info.file_sz - rd_pos; //truncate read to not go past EOF
         RTV_ERRLOG("%s: SW BUG: bad ndx record calculation\n", __FUNCTION__);
      }

      rc = rtv_read_file(ndx_info.device, ndx_info.filename, rd_pos, rd_sz, &(ndx_info.file_chunk));
      if ( rc != 0 ) {
         RTV_ERRLOG("%s: NDX file chunk read failed\n", __FUNCTION__);
         return(rc);
      }
      ndx_info.start_rec_num = start_rec;
      ndx_info.recs_in_mem   = rd_sz / ndx_info.rec_sz;

      if ( RTVLOG_INFO ) {
         ndx_recs = (rtv_ndx_22_record_t*)ndx_info.file_chunk.data_start;
         memcpy(&tmp_rec, &(ndx_recs[0]), sizeof(rtv_ndx_22_record_t));
         rtv_convert_22_ndx_rec(&tmp_rec);
         rtv_print_22_ndx_rec("LoadStartRec", ndx_info.start_rec_num, &tmp_rec);
         memcpy(&tmp_rec, &(ndx_recs[ndx_info.recs_in_mem-1]), sizeof(rtv_ndx_22_record_t));
         rtv_convert_22_ndx_rec(&tmp_rec);
         rtv_print_22_ndx_rec("LoadEndRec  ", ndx_info.start_rec_num + ndx_info.recs_in_mem - 1, &tmp_rec);
      }
   }
   
   // Do a linear interpolation guestimate for the record number.
   //
   ndx_recs = (rtv_ndx_22_record_t*)ndx_info.file_chunk.data_start;
   
   rec_no = (((time_ms / 1000) - (slice_idx * RTV_NUM_SEC_PER_SLICE)) * ndx_slice_list.slice[slice_idx].num_recs) / RTV_NUM_SEC_PER_SLICE;
   
   if ( (rec_no < 0 ) || 
        (rec_no >= ndx_info.recs_in_mem) ) {
      RTV_ERRLOG("%s: NDX processing SW BUG: rec=%d start=%d cnt=%d\n", __FUNCTION__, rec_no, ndx_info.start_rec_num, ndx_info.recs_in_mem);
      return(-ESPIPE);
   }

   // Track down the timestamp with a linear search.
   //
   memcpy(rec, &(ndx_recs[rec_no]), sizeof(rtv_ndx_22_record_t));
   rtv_convert_22_ndx_rec(rec);
   if ( ((rec->timestamp - ndx_info.base_time) / 1000000000) > (time_ms / 1000) ) {
      while ( (max_srch_cnt--) && (rec_no-- >= 0) ) {
         memcpy(rec, &(ndx_recs[rec_no]), sizeof(rtv_ndx_22_record_t));
         rtv_convert_22_ndx_rec(rec);
         if ( ((rec->timestamp - ndx_info.base_time) / 1000000000) <= (time_ms / 1000) ) {
            break;
         }
      }
   }
   else if ( ((rec->timestamp - ndx_info.base_time) / 1000000000) < (time_ms / 1000) ) {
      while ( (max_srch_cnt--) && (rec_no++ < ndx_slice_list.slice[slice_idx].num_recs) ) {
         memcpy(rec, &(ndx_recs[rec_no]), sizeof(rtv_ndx_22_record_t));
         rtv_convert_22_ndx_rec(rec);
         if ( ((rec->timestamp - ndx_info.base_time) / 1000000000) >= (time_ms / 1000) ) {
            break;
         }
      }
   }


   // Should check for the RTV 4K video offset race condition problem.
   // I didn't see any issues with the sample 4k ndx/mpg sent to me so
   // I'm not gona bother with it for now.
   //

   if ( RTVLOG_INFO ) {
      rtv_print_22_ndx_rec("JumpTo      ", rec_no+ndx_info.start_rec_num, rec);
   }
   return(0);
}


//+***********************************************************************************************
//  Name: rtv_parse_4k_ndx_file
//  Processes RTV 4K ndx file records and builds list of chapter segments.
//+***********************************************************************************************
int rtv_parse_4k_ndx_file(rtv_comm_blks_t *commercials)
{
   const unsigned int chunk_sz        = 32768;
   unsigned int       pos             = sizeof(rtv_ndx_22_header_t);
   int                rec_num         = 0;
   __u32              comm_start_ms   = 0;
   int                in_commercial   = 0;
   int                num_blocks      = 0;
   rtv_prog_seg_t    *comm_blk_recs   = NULL;
   int                slice_base_time = 0;
   int                current_slice   = 0;

   int                   rc;
   unsigned int          x;
   rtv_ndx_22_record_t  *rec;
   rtv_http_resp_data_t  file_data;



   RTV_DBGLOG(RTVLOG_NDX, "%s: ndx_fn=%s size=%d\n",  __FUNCTION__, ndx_info.filename, ndx_info.file_sz);
   commercials->num_blocks = 0;

   // Free the slice list if it has been used
   //
   if ( ndx_slice_list.slice != NULL ) {
      free(ndx_slice_list.slice);
      ndx_slice_list.slice      = NULL;
      ndx_slice_list.num_slices = 0;
   }

   while ( pos < ndx_info.file_sz ) {

      unsigned int bytes_to_read;
      int          ts_msec;

      if ( (ndx_info.file_sz - pos) < chunk_sz ) {
         bytes_to_read = ndx_info.file_sz - pos;
      }
      else {
         bytes_to_read = chunk_sz;
      }

      rc = rtv_read_file(ndx_info.device, ndx_info.filename, pos, bytes_to_read, &file_data);
      RTV_DBGLOG(RTVLOG_NDX, "%s: Read: pos=%d, count=%d\n",  __FUNCTION__, pos, bytes_to_read);
      if ( rc != 0 ) {
         RTV_ERRLOG("%s: ndx file read failed: pos=%d, count=%d, rc=%d\n", __FUNCTION__, pos, bytes_to_read, rc);
         return(rc);
      }

      rec = (rtv_ndx_22_record_t*)file_data.data_start;
      for ( x=0; x < bytes_to_read / sizeof(rtv_ndx_22_record_t); x++ ) {
 
         //rtv_print_22_ndx_rec("DUMP ", rec_num, &(rec[x]));
         if ( rec_num == 0 ) {
            ndx_info.base_time = rec[x].timestamp;

            ndx_slice_list.slice = (rtv_4k_ndx_slice_t*)malloc(sizeof(rtv_4k_ndx_slice_t));
            ndx_slice_list.slice[0].start_ms  = 0;
            ndx_slice_list.slice[0].start_rec = 0;
            slice_base_time                   = 0;
            current_slice                     = 0;
           
            rtv_print_22_ndx_rec("FIRST", rec_num, &(rec[x]));
         }
         else if ( rec_num == (ndx_info.num_rec_in_file - 1) ) {
            ndx_info.max_time_ms = (rec[x].timestamp - ndx_info.base_time) / 1000000;
            rtv_print_22_ndx_rec("LAST ", rec_num, &(rec[x]));
         }
         
         // Build the slice list
         //
         ts_msec = (rec[x].timestamp - ndx_info.base_time) / 1000000;
         if ( (ts_msec/1000) >= (slice_base_time + RTV_NUM_SEC_PER_SLICE) ) {
            ndx_slice_list.slice[current_slice].num_recs = rec_num - ndx_slice_list.slice[current_slice].start_rec + 1;
            current_slice++;

             ndx_slice_list.slice = (rtv_4k_ndx_slice_t*)realloc(ndx_slice_list.slice, sizeof(rtv_4k_ndx_slice_t) * (current_slice+1));
            slice_base_time += RTV_NUM_SEC_PER_SLICE;
            ndx_slice_list.slice[current_slice].start_ms  = ts_msec;
            ndx_slice_list.slice[current_slice].start_rec = rec_num;
         }
         
         // Parse commercial segments
         //
         if (rec[x].commercial_flag & 0x1) {
            if (!in_commercial) {
               comm_start_ms = (rec[x].timestamp - ndx_info.base_time) / 1000000;
               if ( !(rec[x].commercial_flag & 0x02) )
                  RTV_WARNLOG("%s: start of commercial without 0x02 flag: 0x%02x\n", __FUNCTION__, rec[x].commercial_flag);
               in_commercial = 1;
            }
         } else if (in_commercial) {
            if( num_blocks == 0 ) {
               comm_blk_recs = (rtv_prog_seg_t*)malloc(sizeof(rtv_prog_seg_t));
            } else {
               comm_blk_recs = (rtv_prog_seg_t*)realloc(comm_blk_recs, sizeof(rtv_prog_seg_t) * (num_blocks+1));
            }

            comm_blk_recs[num_blocks].start = comm_start_ms;
            comm_blk_recs[num_blocks].stop  = (rec[x].timestamp - ndx_info.base_time) / 1000000;
            num_blocks++;
            in_commercial = 0;
         }
 
         rec_num++;
      }
      

      pos += bytes_to_read;
      free(file_data.buf);
    }

   ndx_slice_list.slice[current_slice].num_recs = rec_num - ndx_slice_list.slice[current_slice].start_rec + 1;
   ndx_slice_list.num_slices = current_slice + 1;

   if ( RTVLOG_NDX ) {
      char tstr[30];

      RTV_PRT("RTV 4K NDX slices\n");
      RTV_PRT("Slice   StartTime   StartRec   NumRec\n");
      RTV_PRT("-------------------------------------\n");
      for ( x = 0; x < ndx_slice_list.num_slices; x++ ) {
         rtv_format_ts_ms32_min_sec_ms(ndx_slice_list.slice[x].start_ms, tstr);
         RTV_PRT(" %02d   %12s  %5d    %5d\n", x, tstr, ndx_slice_list.slice[x].start_rec, ndx_slice_list.slice[x].num_recs);
      }
   }

   commercials->num_blocks = num_blocks;
   commercials->blocks     = comm_blk_recs;
   return(0);
}


//+*********************************************************************
//  Name: rtv_print_30_ndx_rec
//
//+*********************************************************************
void rtv_print_30_ndx_rec(char *tag, int rec_no, rtv_ndx_30_record_t *rec)
{
   char *ts;

   ts = rtv_format_nsec64(rec->timestamp);
   if ( tag != NULL ) {
      RTV_PRT("NDXREC: %s: rec_no=%05d ts=%s fpos_iframe=0x%010llx(%011llu) iframe_size=%lu\n", 
             tag, rec_no, ts, rec->filepos_iframe, rec->filepos_iframe, rec->iframe_size);
   }
   else {
      RTV_PRT("NDXREC: rec_no=%05d ts=%s fpos_iframe=0x%010llx(%011llu) iframe_size=%lu\n", 
             rec_no, ts, rec->filepos_iframe, rec->filepos_iframe, rec->iframe_size);
   }
   free(ts);
   return;
}


//+*********************************************************************
//  Name: rtv_print_22_ndx_rec
//
//+*********************************************************************
void rtv_print_22_ndx_rec(char *tag, int rec_no, rtv_ndx_22_record_t *rec)
{
   char *ts;

   ts = rtv_format_nsec64(rec->timestamp - ndx_info.base_time);
   if ( tag != NULL ) {
      RTV_PRT("NDXREC: %s: rec_no=%05d ts=%s stream_pos=0x%010llx(%011llu) video_offset=0x%06x(%06u) audio_offset=0x%06lx(%06lu)\n", 
             tag, rec_no, ts, 
             rec->stream_position, rec->stream_position,
             rec->video_offset, rec->video_offset, 
             rec->audio_offset, rec->audio_offset);
   }
   else {
      RTV_PRT("NDXREC: rec_no=%05d ts=%s stream_pos=0x%010llx(%011llu) video_offset=0x%06x(%06u) audio_offset=0x%06lx(%06lu)\n", 
             rec_no, ts, 
             rec->stream_position, rec->stream_position,
             rec->video_offset, rec->video_offset, 
             rec->audio_offset, rec->audio_offset);
   }
   free(ts);
   return;
}

