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
#include <unistd.h>

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

static int map_guide_status_to_rc(unsigned long status)
{
   int rc;

   // returncodes: 
   // 0x94780001: for invalid show_id or invalid channel_id
   // 0x94780004: invalid request string format
   //
   if ( status == 0 ) {
      rc = 0;
   }
   else {
      RTV_ERRLOG("%s: guide http response code: 0x%08lx:(%lu)\n", __FUNCTION__, status, status);
      rc  = -EPROTO;
   }
   return(rc);

}
static int get_rtv5k_snapshot_callback(unsigned char *buf, size_t len, void *vd)
{
    struct snapshot_data *data = vd;
    unsigned char        *buf_data_start;
    unsigned long         bytes_to_read;
    
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
           return(-EBADMSG);
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
                RTV_ERRLOG("%s: \\n not found: cur=%p: returning\n", __FUNCTION__, cur);
                return(-EBADMSG);
            }
            *end = '\0';
            equal = strchr(cur, '=');
            if (!equal) { 
                RTV_ERRLOG("%s: = not found: cur=%p: returning\n", __FUNCTION__, cur);
                return(-EBADMSG); 
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
    fflush(NULL);
    return(0);
}

static void rtv_free_channel(rtv_channel_export_t *chan) 
{
   if ( chan != NULL ) {
      if ( chan->days_of_week != NULL ) free(chan->days_of_week);
   }
}

static void rtv_free_show(rtv_show_export_t *show) 
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
      if ( show->genre       != NULL ) free(show->genre);
      if ( show->rating_extended != NULL ) free(show->rating_extended);
      if ( show->sch_st_tm_str   != NULL ) free(show->sch_st_tm_str);
      if ( show->duration_str    != NULL ) free(show->duration_str);

      if ( show->file_info != NULL ) rtv_free_file_info(show->file_info);
   }
}

//+******************************************
// get_guide_ss()
// Internal get guide snapshot function
// Returns 0 for success
//+******************************************
static int get_guide_ss( const rtv_device_info_t  *device,
                         const char               *cur_timestamp,
                         rtv_guide_export_t       *guide          )
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

    if ( (atoi(device->modelNumber) == 4999) && (device->autodiscovered != 1) ) {
       RTV_ERRLOG("%s: DVArchive must be auto-discovered before guide snapshot can be retrieved\n", __FUNCTION__);
       return(-ENOTSUP);
    }
    if ( device->version.vintage == RTV_DEVICE_4K ) {
       sprintf(url, "http://%s/http_replay_guide-get_snapshot?"
               "guide_file_name=%s&serial_no=%s",
               device->ipaddr,
               send_timestamp,
               rtv_idns.sn_4k);
    }
    else if ( device->version.vintage == RTV_DEVICE_5K ) {       
       sprintf(url, "http://%s/http_replay_guide-get_snapshot?"
               "guide_file_name=%s&serial_no=%s",
               device->ipaddr,
               send_timestamp,
               rtv_idns.sn_5k);
    }
    else {
       RTV_ERRLOG("%s: Invalid device vintage: %d\n", __FUNCTION__, device->version.vintage);
       return(-1);
    }

    RTV_DBGLOG(RTVLOG_GUIDE, "%s: url=%s\n", __FUNCTION__, url); 
    hc = hc_start_request(url);
    if (!hc) {
        RTV_ERRLOG("%s: hc_start_request(): %d=>%s\n", __FUNCTION__, errno, strerror(errno));
        return(-1);
    }

    hc_send_request(hc, ":80\r\nAccept-Encoding: gzip\r\n");
    rc = hc_read_pieces(hc, get_rtv5k_snapshot_callback, &data, 0);
    hc_free(hc);
    if ( rc != 0 ) {
       RTV_ERRLOG("%s: hc_read_pieces call failed: rc=%d\n", __FUNCTION__, rc);
       return(rc);
    }

    guide->timestamp =  data.timestamp;
    guide->status    =  data.status;

    rc = parse_guide_snapshot(data.buf, data.bytes_read, guide);
    free(data.buf);    
    return(rc);
}

//+******************************************
// update_shows_file_info()
// Update the file info for guide show mpg files.
// Returns 0 for success
//+******************************************
static int update_shows_file_info( const rtv_device_info_t  *device,
                                   rtv_guide_export_t       *guide  )
{
    int          rc = 0;
    unsigned int x;

    // Get file info for the show mpg files
    //
    for ( x=0; x < guide->num_rec_shows; x++ ) {
       rtv_fs_file_t *fileinfo;
       char           path[255];
       
       fileinfo = malloc(sizeof(rtv_fs_file_t));
       sprintf(path, "/Video/%s", guide->rec_show_list[x].file_name);
       RTV_DBGLOG(RTVLOG_GUIDE, "%s: idx=%d: show=%s\n", __FUNCTION__, x, path); 
       rc = rtv_get_file_info(device, path, fileinfo);
       if ( rc != 0 ) {
          RTV_ERRLOG("%s: rtv_get_file_info failed: %s: %d=>%s\n", __FUNCTION__, rc, path, strerror(abs(rc)));
          memset(fileinfo, 0, sizeof(fileinfo));
       }
       guide->rec_show_list[x].file_info = fileinfo;
    }
    
    return(0);
}

//+******************************************
// guide_do_request()
// Send a guide request
// If response is not NULL then parses http response and
// returns in *malloc'd *response. 
// Returns 0 for success
//+******************************************
static int guide_do_request(  const char  *url,
                              char       **response )
{
    char                 *tmp, *e;
    struct hc            *hc;
    int                   rc, hc_stat;
    unsigned int          len;
    unsigned long         status;


    RTV_DBGLOG(RTVLOG_GUIDE, "%s: url=%s\n", __FUNCTION__, url); 
    *response = NULL;

    // Send the request & get the response back
    //
    hc = hc_start_request((char*)url); //JBH: hack cast to eliminate warning
    if (!hc) {
       RTV_ERRLOG("%s: hc_start_request(): %d=>%s\n", __FUNCTION__, errno, strerror(errno));
       return(-1);
    }
    hc_send_request(hc, ":80\r\nAccept-Encoding: gzip\r\n");
    
    rc = hc_read_all(hc, &tmp, &len);
    
    if ( rc != 0 ) {
       RTV_ERRLOG("%s: hc_read_all call failed rc=%d\n", __FUNCTION__, rc);
       hc_free(hc);
       if ( tmp != NULL ) {
          free(tmp);
       }
       return(rc);
    }
    if ( ((hc_stat = hc_get_status(hc)) / 100) != 2 ) {
       RTV_ERRLOG("%s: http_status == %d\n",  __FUNCTION__,  hc_stat);
       hc_free(hc);
       if ( tmp != NULL ) {
          free(tmp);
       }
       return -ECONNABORTED;
    }
    hc_free(hc);
    
    if ( response != NULL ) {
       
       // A text response is expected
       //
       RTV_DBGLOG(RTVLOG_GUIDE, "%s: response=%s\n", __FUNCTION__, tmp); 
       
       e = strchr(tmp, '\n');
       if (e) {
          *response = strdup(e+1);
          status = strtoul(tmp, NULL, 16);
          RTV_DBGLOG(RTVLOG_CMD, "%s: http_status=0x%08lx(%lu)\n", __FUNCTION__, status);    
          rc = map_guide_status_to_rc(status);
       } else if (hc_stat == 204) {
          RTV_WARNLOG("%s: http_status == *** 204 ***\n",  __FUNCTION__);
          *response = NULL;;
          rc = 0;;
       } else {
          RTV_ERRLOG("%s: end of http guide status line not found\n", __FUNCTION__);
          rc = -EPROTO;
       }
    }
    free(tmp);
    return(rc);
}

//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************

int rtv_get_guide_snapshot( const rtv_device_info_t  *device,
                            const char               *cur_timestamp,
                                  rtv_guide_export_t *guide          )
{
    int rc;

    // Get the guide snapshot
    //
    if ( (rc = get_guide_ss(device, cur_timestamp, guide)) != 0 ) {
       //If get_guide_ss failed then the guide has already been free'd
       return(rc);
    }

    // Get file info for the show mpg files
    //
    if ( (rc = update_shows_file_info(device, guide)) != 0 ) {
       rtv_free_guide(guide);
    }
    
    return(rc);
}

void rtv_free_guide(rtv_guide_export_t *guide) 
{
   unsigned int x;

   if ( guide != NULL ) {
      if ( guide->timestamp != NULL ) {
         free( guide->timestamp);
      }
      for (x=0; x < guide->num_channels; x++) {
         rtv_free_channel(&(guide->channel_list[x]));
      }
      if ( guide->channel_list != NULL ) {
         free(guide->channel_list);
      }
      
      for (x=0; x < guide->num_rec_shows; x++) {
         rtv_free_show(&(guide->rec_show_list[x]));
      }
      if ( guide->rec_show_list != NULL ) {
         free(guide->rec_show_list);
      }
      memset(guide, 0, sizeof(rtv_guide_export_t));
   }
}

void rtv_print_show(const rtv_show_export_t *show, int num) 
{
   char  tmpstr[512];
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
      RTV_PRT("  genre:       %s\n", show->genre);
      RTV_PRT("  rating:      %s\n", show->rating);
      RTV_PRT("  rating ext:  %s\n", show->rating_extended);
      RTV_PRT("  quality:     %s\n", rtv_xref_quality(show->quality));
      RTV_PRT("  input src:   %s\n", rtv_xref_input_source(show->input_source));
      RTV_PRT("  tuning:      %d  (%s) %s\n", show->tuning, show->tune_chan_name, show->tune_chan_desc);
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
      if (show->flags.movie) {
         RTV_PRT("  movie_stars: %d    movie_year: %d    movie_runtime: %d\n", show->movie_stars, show->movie_year, show-> movie_runtime);
      }      
      RTV_PRT("  show_id:  0x%08x   channel_id:  0x%08x\n", show->show_id, show->channel_id);
      RTV_PRT("  filename:           %s\n", show->file_name);
      if ( show->file_info != NULL ) {
      RTV_PRT("  file size           %"U64F"d %lu(MB)\n", show->file_info->size, show->file_info->size_k / 1024);
      RTV_PRT("  file creation time: %"U64F"d\n", show->file_info->time);
      RTV_PRT("                      %s\n", show->file_info->time_str_fmt1);
      RTV_PRT("                      %s\n", show->file_info->time_str_fmt2);
      }
      else {
         RTV_PRT("************* WARNING: show->fileinfo == NULL\n");
      }
      RTV_PRT("  GOP_count:          %u\n", show->gop_count);
      RTV_PRT("  duration (seconds): %u\n", show->duration_sec);
      RTV_PRT("  duration:           %s\n", show->duration_str);
      RTV_PRT("  sch start time:     %u\n", show->sch_start_time);
      RTV_PRT("  sch start time:     %s\n", show->sch_st_tm_str);
      RTV_PRT("  sch len (minutes):  %u\n", show->sch_show_length);
      RTV_PRT("  minutes padding before: %u minutes padding after: %u\n", show->padding_before, show->padding_after);

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
      RTV_PRT("\n");
   }

}


//+******************************************
// rtv_is_show_inuse()
// *in_use set to 1 is show is in use 
// Returns 0 for success
//+******************************************
int rtv_is_show_inuse( const rtv_device_info_t   *device,
                       const rtv_guide_export_t  *guide, 
                       const unsigned int         show_idx,
                       int                       *in_use )
{
   char  *response = NULL;
   char   url[512];
   char  *e;
   int    rc;
   
   if ( (atoi(device->modelNumber) == 4999) && (device->autodiscovered != 1) ) {
      RTV_ERRLOG("%s: DVArchive must be auto-discovered before guide snapshot can be retrieved\n", __FUNCTION__);
      return(-ENOTSUP);
   }
   
   if ( guide == NULL ) {
      RTV_ERRLOG("%s: Guide is NULL\n", __FUNCTION__);
      return(-EINVAL);
   }
   if ( show_idx >= guide->num_rec_shows ) {
      RTV_ERRLOG("%s: show_idx out-of-range: idx=%u, num_rec_shows=%u\n", __FUNCTION__, show_idx, guide->num_rec_shows);
      return(-EINVAL);
   }
   
   sprintf(url, "http://%s/http_replay_guide-is_show_in_use?"
           "channel_id=0x%08lx&show_id=0x%08lx&serial_no=%s",
           device->ipaddr,
           guide->rec_show_list[show_idx].channel_id,
           guide->rec_show_list[show_idx].show_id,
           device->serialNum);
   
   RTV_DBGLOG(RTVLOG_GUIDE, "%s: url=%s\n", __FUNCTION__, url); 
   
   rc = guide_do_request(url, &response);
   
   //response format: "show_in_use=0x0" or "show_in_use=0x1"
   if ( rc == 0 ) {
      if ( (e = strchr(response, '=')) == NULL ) {
         rc = -EILSEQ;
         *in_use = 1;
      }
      else {
         *in_use = e[3] - '0';
      }
      free(response);
   }
   
   return(rc);
}

//+******************************************
// rtv_delete_show()
// Returns 0 for success
//+******************************************
int rtv_delete_show( const rtv_device_info_t   *device,
                     const rtv_guide_export_t  *guide, 
                     const unsigned int         show_idx)
{
    char   url[512];
    char  *response;
    int    rc;

    if ( (atoi(device->modelNumber) == 4999) && (device->autodiscovered != 1) ) {
       RTV_ERRLOG("%s: DVArchive must be auto-discovered before guide snapshot can be retrieved\n", __FUNCTION__);
       return(-ENOTSUP);
    }

    if ( guide == NULL ) {
       RTV_ERRLOG("%s: Guide is NULL\n", __FUNCTION__);
       return(-EINVAL);
    }
    if ( show_idx >= guide->num_rec_shows ) {
       RTV_ERRLOG("%s: show_idx out-of-range: idx=%u, num_rec_shows=%u\n", __FUNCTION__, show_idx, guide->num_rec_shows);
       return(-EINVAL);
    }

    sprintf(url, "http://%s/http_replay_guide-delete_show?"
            "channel_id=0x%08lx&show_id=0x%08lx&serial_no=%s",
            device->ipaddr,
            guide->rec_show_list[show_idx].channel_id,
            guide->rec_show_list[show_idx].show_id,
            device->serialNum);

    RTV_DBGLOG(RTVLOG_GUIDE, "%s: url=%s\n", __FUNCTION__, url); 

    rc = guide_do_request(url, &response);

    //response format: nothing
    if ( rc == 0 ) {
       free(response);
    }
    return(rc);
}

//+******************************************
// rtv_release_show_and_wait()
// Waits for a deleted show to be released and updates
// the guide snapshot
// Returns 0 for success
//+******************************************
int rtv_release_show_and_wait( const rtv_device_info_t   *device,
                                     rtv_guide_export_t  *guide, 
                               const unsigned int         show_idx)
{
   int           trys_left = 10;
   int           done      = 0;
   unsigned int  x;
   int           rc;
   __u32         show_id;
   

   if ( (atoi(device->modelNumber) == 4999) && (device->autodiscovered != 1) ) {
      RTV_ERRLOG("%s: DVArchive must be auto-discovered before guide snapshot can be retrieved\n", __FUNCTION__);
      return(-ENOTSUP);
   }
   
   if ( guide == NULL ) {
      RTV_ERRLOG("%s: Guide is NULL\n", __FUNCTION__);
      return(-EINVAL);
   }
   if ( show_idx >= guide->num_rec_shows ) {
      RTV_ERRLOG("%s: show_idx out-of-range: idx=%u, num_rec_shows=%u\n", __FUNCTION__, show_idx, guide->num_rec_shows);
      return(-EINVAL);
   }
   
   show_id = guide->rec_show_list[show_idx].show_id;

   // Wait for the guide to update
   //
   while ( (trys_left-- > 0) && !(done) ) {
      sleep(5);
      if ( (rc = get_guide_ss(device, NULL, guide)) != 0 ) {
         return(rc);
      }
      
      // See if the show is gone yet.
      //
      for (x=0; x < guide->num_rec_shows; x++ ) {
         if ( guide->rec_show_list[show_idx].show_id == show_id ) {
            break;
         }
      }

      if ( x == guide->num_rec_shows ) {
         done = 1;;
      }
   } //while
   
   if ( !(done) ) {
      return(-ETIMEDOUT);
   }

   if ( (rc = update_shows_file_info(device, guide)) != 0 ) {
      rtv_free_guide(guide);
   }
   
   return(rc);
}

//+******************************************
// rtv_get_play_position()
// *play_pos is set to the current play position (GOP)
// Returns 0 for success
//+******************************************
int rtv_get_play_position( const rtv_device_info_t   *device,
                           const rtv_guide_export_t  *guide, 
                           const unsigned int         show_idx,
                           unsigned int              *play_pos )
{
    char   url[512];
    char  *response, *e;
    int    rc;

    if ( (atoi(device->modelNumber) == 4999) && (device->autodiscovered != 1) ) {
       RTV_ERRLOG("%s: DVArchive must be auto-discovered before guide snapshot can be retrieved\n", __FUNCTION__);
       return(-ENOTSUP);
    }

    if ( guide == NULL ) {
       RTV_ERRLOG("%s: Guide is NULL\n", __FUNCTION__);
       return(-EINVAL);
    }
    if ( show_idx >= guide->num_rec_shows ) {
       RTV_ERRLOG("%s: show_idx out-of-range: idx=%u, num_rec_shows=%u\n", __FUNCTION__, show_idx, guide->num_rec_shows);
       return(-EINVAL);
    }

    sprintf(url, "http://%s/http_replay_guide-get_play_position?"
            "channel_id=0x%08lx&show_id=0x%08lx&serial_no=%s",
            device->ipaddr,
            guide->rec_show_list[show_idx].channel_id,
            guide->rec_show_list[show_idx].show_id,
            device->serialNum);

    RTV_DBGLOG(RTVLOG_GUIDE, "%s: url=%s\n", __FUNCTION__, url); 

    rc = guide_do_request(url, &response);

    //response format: "gop_pos=0xXXXXXXXX"
    if ( rc == 0 ) {
       if ( (e = strchr(response, '=')) == NULL ) {
          rc = -EILSEQ;
          *play_pos = 0;;
       }
       else {
          *play_pos = strtoul(&(e[1]), NULL, 16);
       }
       free(response);
    }

    return(rc);
}

