/*
 * Copyright (C) 2004 John Honeycutt
 * Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
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

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "rtv.h"
#include "rtvlib.h"
#include "guideparser.h"
#include "httpclient.h"
#include "guideclient.h"

#define MIN(x,y) ((x)<(y)?(x):(y))

struct snapshot_data 
{
    int   firsttime;
    int   filesize;
    int   bytes_read;
    char *timestamp;
    char *buf;
    u32   status;
};

static int get_rtv5k_snapshot_callback(unsigned char * buf, size_t len, void * vd)
{
    struct snapshot_data * data = vd;
    unsigned char * buf_data_start;
    unsigned long bytes_to_read;
    
    RTV_DBGLOG(RTVLOG_GUIDE, "%s: data->firsttime=%d buf=%p vd=%p len=%d\n", __FUNCTION__, data->firsttime, buf, vd, len); 
    if (data->firsttime) {
        unsigned char * end, * equal, * cur;

        data->firsttime = 0;

        /* First line: error code */
        cur = buf;
        end = strchr(cur, '\n');
        if (end) *end = '\0';
        data->status = strtoul(cur, NULL, 16);
        RTV_DBGLOG(RTVLOG_GUIDE, "%s: status=%lu\n", __FUNCTION__, data->status);
        if (!end) {
           RTV_ERRLOG("%s: malformed buffer\n", __FUNCTION__);
           return(0);
        }
        do {
            cur = end + 1;
            if (*cur == '#') {
                end = strchr(cur, '\0');
                len -= (end - buf);
                buf_data_start = end;
                RTV_DBGLOG(RTVLOG_GUIDE, "%s: *cur==#: set buf_data_start=%p set len=%d\n", __FUNCTION__, buf_data_start, len);
                break;
            }
            end = strchr(cur, '\n');
            if (!end) {
                RTV_DBGLOG(RTVLOG_GUIDE, "%s: \\n not found: cur=%p: returning\n", __FUNCTION__, cur);
                return(0);
            }
            *end = '\0';
            equal = strchr(cur, '=');
            if (!equal) { 
                RTV_DBGLOG(RTVLOG_GUIDE, "%s: = not found: cur=%p: returning\n", __FUNCTION__, cur);
                return(0); 
            }
            if (strncmp(cur, "guide_file_name=", equal-cur+1) == 0) {
                data->timestamp = malloc(strlen(equal+1)+1);
                strcpy(data->timestamp, equal+1);
                RTV_DBGLOG(RTVLOG_GUIDE, "%s: set guide_file_name= at %p\n", __FUNCTION__, equal+1);
            } else if (strncmp(cur, "FileLength=", equal-cur+1) == 0) {
                data->filesize = strtoul(equal+1, NULL, 0);
                data->buf = malloc(data->filesize);
                RTV_DBGLOG(RTVLOG_GUIDE, "%s: set FileLength= at %p\n", __FUNCTION__, equal+1);
            } /* also "RemoteFileName", but we don't expose it */
        } while (1);
    } else {
        buf_data_start = buf;
    }

    bytes_to_read = MIN(len, (unsigned)(data->filesize - data->bytes_read));
    memcpy(data->buf + data->bytes_read, buf_data_start, bytes_to_read);
    data->bytes_read += bytes_to_read;

    free(buf);
    if ( RTVLOG_GUIDE ) {
       RTV_DBGLOG(RTVLOG_GUIDE, "%s: snapshot_data struct dump:\n", __FUNCTION__);
       RTV_PRT   ("    firsttime=%d   filesize=%d\n", data->firsttime, data->filesize);
       RTV_PRT   ("    bytesread=%d   status=  %lu  timestamp=%s\n", data->bytes_read, data->status, data->timestamp);
    } 
    return(0);
}

//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************

int rtv_get_guide_snapshot( const rtv_device_info_t  *device,
                            const char               *cur_timestamp,
                                  rtv_guide_export_t *guide          )
{
    const char           *send_timestamp = "0";
    char                  url[512];
    struct hc            *hc;
    struct snapshot_data  data;
    int                   rc;

    rtv_free_guide(guide);
    memset(&data, 0, sizeof data);
    data.firsttime = 1;
    data.status    = -1;

    if ( cur_timestamp != NULL ) {
       send_timestamp = cur_timestamp;
    }

    if ( atoi(device->modelNumber) == 4999 ) {
       RTV_PRT("Sorry DVArchive not supported yet\n");
       return(-ENOTSUP);
    }
    if ( device->version.vintage == RTV_DEVICE_4K ) {
       sprintf(url, "http://%s/http_replay_guide-get_snapshot?"
               "guide_file_name=%s&serial_no=RTV4080K0000000000",
               device->ipaddr,
               send_timestamp);
    }
    else if ( device->version.vintage == RTV_DEVICE_5K ) {       
       sprintf(url, "http://%s/http_replay_guide-get_snapshot?"
               "guide_file_name=%s&serial_no=RTV5040J3TR0202999",
               device->ipaddr,
               send_timestamp);
//               "guide_file_name=%s&serial_no=RTV5040K0000000000",
    }
    else {
       RTV_ERRLOG("%s: Invalid device vintage: %d\n", __FUNCTION__, device->version.vintage);
       return(-1);
    }

    RTV_DBGLOG(RTVLOG_GUIDE, "%s: url=%s\n", __FUNCTION__, url); 
    hc = hc_start_request(url);
    if (!hc) {
        RTV_ERRLOG("guide_client_get_snapshot(): hc_start_request(): %d=>%s\n", errno, strerror(errno));
        return(-1);
    }

    hc_send_request(hc, ":80\r\nAccept-Encoding: gzip\r\n");
    hc_read_pieces(hc, get_rtv5k_snapshot_callback, &data, RTV_MERGECHUNKS_0);
    hc_free(hc);

    guide->timestamp =  data.timestamp;
    guide->status    =  data.status;

    rc = parse_v2_guide_snapshot(data.buf, guide);
    free(data.buf);
    return(rc);
}

void rtv_free_show(rtv_show_export_t *show) 
{
   if ( show != NULL ) {
      if ( show->title       != NULL ) free(show->title);
      if ( show->episode     != NULL ) free(show->episode);
      if ( show->description != NULL ) free(show->description);
      if ( show->actors      != NULL ) free(show->actors);
      if ( show->guest       != NULL ) free(show->guest);
      if ( show->suzuki      != NULL ) free(show->suzuki);
      if ( show->producer    != NULL ) free(show->producer);
      if ( show->director    != NULL ) free(show->director);
      if ( show->file_name   != NULL ) free(show->file_name);
   }
   memset(show, 0, sizeof(rtv_show_export_t));
}

void rtv_free_guide(rtv_guide_export_t *guide) 
{
   unsigned int x;

   if ( guide != NULL ) {
      if ( guide->timestamp != NULL ) {
         free( guide->timestamp);
      }
      for (x=0; x < guide->num_rec_shows; x++) {
         rtv_free_show(&(guide->rec_show_list[x]));
      }
      memset(guide, 0, sizeof(rtv_guide_export_t));
   }
}

void rtv_print_show(const rtv_show_export_t *show, int num) 
{
   char *tmpstr[512];
   char *strp = tmpstr;

   if ( show != NULL ) {
      RTV_PRT("Show #%d:\n", num);
      RTV_PRT("  title:       %s\n", show->title);
      RTV_PRT("  episode:     %s\n", show->episode);
      RTV_PRT("  description: %s\n", show->description);
      RTV_PRT("  actors:      %s\n", show->actors);
      RTV_PRT("  guest:       %s\n", show->guest);
      RTV_PRT("  suzuki:      %s\n", show->suzuki);
      RTV_PRT("  producer:    %s\n", show->producer);
      RTV_PRT("  director:    %s\n", show->director);

      strp += sprintf(strp, "  flags:      ");
      if (show->flags.multipart)  strp += sprintf(strp, " MPART"); 
      if (show->flags.guaranteed) strp += sprintf(strp, " GUAR"); 
      if (show->flags.guide_id)   strp += sprintf(strp, " GID"); 
      if (show->flags.cc)         strp += sprintf(strp, " CC"); 
      if (show->flags.stereo)     strp += sprintf(strp, " STEREO"); 
      if (show->flags.repeat)     strp += sprintf(strp, " REPEAT"); 
      if (show->flags.sap)        strp += sprintf(strp, " SAP"); 
      if (show->flags.letterbox)  strp += sprintf(strp, " LBOX"); 
      if (show->flags.movie)      strp += sprintf(strp, " MOVIE"); 
      strp += sprintf(strp, "\n");
      RTV_PRT("%s", tmpstr);
      RTV_PRT("  filename:    %s\n", show->file_name);
   }
   else {
      RTV_PRT("Show object is NULL!\n");
   }
}

void rtv_print_guide(const rtv_guide_export_t *guide) 
{
   unsigned int x;

   RTV_PRT("RTV Guide Snapshot Dump\n");
   RTV_PRT("-----------------------\n");
   RTV_PRT("timestamp:   %s\n", guide->timestamp); 
   RTV_PRT("status:      0x%08lx\n", guide->status); 
   RTV_PRT("numshows:    %u\n\n", guide->num_rec_shows); 
   for ( x=0; x < guide->num_rec_shows; x++ ) {
      rtv_print_show(&(guide->rec_show_list[x]), x);
   }

}
