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

#ifndef __NDX_CLIENT_H__
#define __NDX_CLIENT_H__

#define NDX_MAX_PATHNAME_LEN (256)

typedef enum rtv_ndx_version_t 
{
   RTV_NDX_22      = 22,
   RTV_NDX_30      = 30,
} rtv_ndx_version_t;


typedef struct rtv_ndx_info_t 
{
   rtv_ndx_version_t         ver;                 //ndx file version
   unsigned int              file_sz;             //file size
   unsigned int              hdr_sz;              //file header size
   unsigned int              rec_sz;              //record size
   unsigned int              max_time_ms;         //timestamp for last ndx record.
   __u64                     base_time;           //timestamp for first ndx rec. (4K only)
   int                       num_rec_in_file;     //number of records in file
   int                       rec_cnt_to_load;     //number of records to load into memory
   int                       start_rec_num;       //starting record number for block in memory
   int                       recs_in_mem;         //number of records in memory
   rtv_http_resp_data_t      file_chunk;          //current chunk of ndx file in memory
   const rtv_device_info_t  *device;
   char                      filename[NDX_MAX_PATHNAME_LEN];
} rtv_ndx_info_t;


#define RTV_NUM_SEC_PER_SLICE (6 * 60)

typedef struct rtv_4k_ndx_slice_t {
   __u32 start_ms;
   int start_rec;
   int num_recs;
} rtv_4k_ndx_slice_t;

typedef struct rtv_4k_ndx_slice_list_t
{
   unsigned int        num_slices;
   int                 sec_per_slice;
   rtv_4k_ndx_slice_t *slice;      //array of ndx slices
} rtv_4k_ndx_slice_list_t;

#endif
