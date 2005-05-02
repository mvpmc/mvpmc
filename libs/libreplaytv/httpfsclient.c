/*
 * Copyright (C) 2004 John Honeycutt
 * Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
 *
 * Parts based on ReplayPC 0.3 by Matthew T. Linehan and others
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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "sleep.h"
#include "crypt.h"
#include "rtvlib.h"
#include "httpfsclient.h"

#define ARGBUFSIZE 2048

static int map_httpfs_status_to_rc(unsigned long status)
{
   int rc;

   // Note: Attempting to get volume info for / returns code 8082000a
   //

   //
   // returncodes: 
   // 80820005 - No such file 
   // 80820018 - File already exists 
   // 80820024 - Insufficiant permission
   if ( status == 0 ) {
      rc = 0;
   }
   else if ( status == 0x80820005 ) {
      rc = -ENOENT;
   }
   else if ( status == 0x80820018 ) {
      rc = -EEXIST;
   }
   else if ( status == 0x80820024 ) {
      rc = -EACCES;
   }
   else if ( status == 0x8082000a ) {
      rc = -EPERM;
   }
   else {
      RTV_ERRLOG("%s: unknown return code: 0x%lx\n", __FUNCTION__, status);
      rc  = -ENOSYS;
   }
   return(rc);

}

static int make_httpfs_url(char *dst, size_t size, const rtv_device_info_t *device, 
                           const char *command, va_list args) 
{
    char * d, * tag, * value, *argp;
    int argno = 0;
    size_t l, argl;
    char argbuf[ARGBUFSIZE];          /* XXX bad, should fix this */

    (void) size;

    l = strlen(device->ipaddr) + strlen(command) + strlen("http:///httpfs-?");
    if (l >= size) {
        RTV_ERRLOG("make_httpfs_url: address + command too long for buffer\n");
        return -1;
    }
    
    d = dst;
    d += sprintf(d, "http://%s/httpfs-%s?", device->ipaddr, command);

    argp = argbuf;
    argl = 0;
    while ((tag = va_arg(args, char *)) != NULL) {
        value = va_arg(args, char *);
        if (value == NULL) {
            continue;
        }
        if (argno)
            argl++;
        argl += strlen(tag)+1+strlen(value);
        if (argl >= sizeof(argbuf)) {
            RTV_ERRLOG("make_httpfs_url: with arg %s, argbuf too small\n", tag);
            return -1;
        }
        if (argno)
            *argp++ = '&';
        argp += sprintf(argp, "%s=%s", tag, value);
        argno++;
    }
    RTV_DBGLOG(RTVLOG_CMD, "%s: argno=%d argbuf: %s\n", __FUNCTION__,argno, argbuf);
    
    if (device->version.major >= 5 ||
        (device->version.major == 4 && device->version.minor >= 3)) {
        unsigned char ctext[ARGBUFSIZE+32] ;
        __u32 ctextlen;
        unsigned int i;

        if (l + strlen("__Q_=") + 2*argl + 32 >= size) {
            RTV_ERRLOG("make_httpfs_url: encrypted args too large for buffer\n");
            return -1;
        }

        strcpy(d, "__Q_=");
        d += strlen(d);
        RTV_DBGLOG(RTVLOG_CMD, "%s: d = %s\n argb = %s\n", __FUNCTION__, dst, argbuf);
        rtv_encrypt(argbuf, argl, ctext, sizeof ctext, &ctextlen, 1);
        for (i = 0; i < ctextlen; i++)
            d += sprintf(d, "%02x", ctext[i]);
    } else {
        if (l + argl >= size) {
            RTV_ERRLOG("make_httpfs_url: args too large for buffer\n");
            return -1;
        }
        strcpy(d, argbuf);
    }
    
    return 0;
}

static int add_httpfs_headers(struct hc * hc)
{
    hc_add_req_header(hc, "Authorization",   "Basic Uk5TQmFzaWM6QTd4KjgtUXQ=" );
    hc_add_req_header(hc, "User-Agent",      "Replay-HTTPFS/1");
    hc_add_req_header(hc, "Accept-Encoding", "text/plain");

    return 0;
}

static struct hc *make_request(const rtv_device_info_t *device, const char * command,
                               va_list ap)
{
    struct hc *hc = NULL;
    char       url[URLSIZE];
    int        http_status;

    RTV_DBGLOG(RTVLOG_CMD, "%s: ip_addr=%s cmd=%s\n", __FUNCTION__, device->ipaddr, command);    
    if (make_httpfs_url(url, sizeof url, device, command, ap) < 0)
        goto exit;

    hc = hc_start_request(url);
    if (!hc) {
        RTV_ERRLOG("make_request(): hc_start_request(): %d=>%s\n", errno, strerror(errno)); 
        goto exit;
    } 

    if (add_httpfs_headers(hc)) {
        goto exit;
    }

    hc_send_request(hc, NULL);

    http_status = hc_get_status(hc);
    if (http_status/100 != 2) {
        RTV_ERRLOG("http status %d\n", http_status);
        goto exit;
    }

    return hc;
    
exit:
    if (hc) {
        hc_free(hc);
    }
    return NULL;
}

    

// hfs_do_simple()
// JBH: This function only works for string results.
//
static int hfs_do_simple(char **presult, const rtv_device_info_t *device, const char * command, ...)
{
    va_list        ap;
    struct hc     *hc;
    char          *tmp, *e;
    unsigned long  rtv_status;
    unsigned int   len, hc_stat;
    int            rc; 
    
    RTV_DBGLOG(RTVLOG_CMD, "%s: ip_addr=%s cmd=%s\n", __FUNCTION__, device->ipaddr, command);    
    *presult = NULL;
    va_start(ap, command);
    hc = make_request(device, command, ap);
    va_end(ap);

    if (!hc) {
       return -ECANCELED;
    }

    rc = hc_read_all(hc, &tmp, &len);
    if ( rc != 0 ) {
       RTV_ERRLOG("%s: hc_read_all call failed rc=%d\n", __FUNCTION__, rc);
       hc_free(hc);
       return(rc);
    }
    hc_stat = hc_get_status(hc);
    hc_free(hc);

    e = strchr(tmp, '\n');
    if (e) {
       *presult = strdup(e+1);
       rtv_status = strtoul(tmp, NULL, 16);
       RTV_DBGLOG(RTVLOG_CMD, "%s: httpfs_status=0x%lx\n", __FUNCTION__, rtv_status);    
       free(tmp);
       return(map_httpfs_status_to_rc(rtv_status));
    } else if ( hc_stat == 204 ) {
       RTV_WARNLOG("%s: http_status == *** 204 ***\n",  __FUNCTION__);
       free(tmp);
       return -ECONNABORTED;
    } else {
       RTV_ERRLOG("%s: end of httpfs status line not found\n", __FUNCTION__);
       free(tmp);
       return -EPROTO;
    }
}

// hfs_do_simple_binary()
// This function works for binary data.
//
static int hfs_do_simple_binary(rtv_http_resp_data_t *data, const rtv_device_info_t *device, const char * command, ...)
{
    va_list        ap;
    struct hc     *hc;
    char          *e;
    unsigned long  rtv_status;
    unsigned int   hc_stat;
    int            rc; 
    
    RTV_DBGLOG(RTVLOG_CMD, "%s: ip_addr=%s cmd=%s\n", __FUNCTION__, device->ipaddr, command);    
    data->buf = NULL;
    va_start(ap, command);
    hc = make_request(device, command, ap);
    va_end(ap);

    if (!hc) {
       return -ECANCELED;
    }

    rc = hc_read_all(hc, &(data->buf), &(data->len));
    if ( rc != 0 ) {
       RTV_ERRLOG("%s: hc_read_all call failed rc=%d\n", __FUNCTION__, rc);
       hc_free(hc);
       return(rc);
    }
    hc_stat = hc_get_status(hc);
    hc_free(hc);

    e = strchr(data->buf, '\n');
    if (e) {
       data->data_start = e + 1;
       data->len        = data->len - (data->data_start - data->buf);
       rtv_status       = strtoul(data->buf, NULL, 16);
       RTV_DBGLOG(RTVLOG_CMD, "%s: http_status=0x%lx\n", __FUNCTION__, rtv_status);    
       return(map_httpfs_status_to_rc(rtv_status));
    } else if (hc_stat == 204) {
       RTV_WARNLOG("%s: http_status == *** 204 ***\n",  __FUNCTION__);
       free(data->buf);
       data->buf = NULL;
       return -ECONNABORTED;
    } else {
       RTV_ERRLOG("%s: end of httpfs status line not found\n", __FUNCTION__);
       free(data->buf);
       data->buf = NULL;
       return -EPROTO;
    }
}

struct hfs_data
{
    rtv_read_file_chunked_cb_t  fn;
    void                       *v;
    unsigned long               status;
    __u16                       msec_delay;
    __u8                        firsttime;
};

static int hfs_callback(unsigned char * buf, size_t len, void * vd)
{
    struct hfs_data *data = vd;
    unsigned char   *buf_data_start;
    int              rc;

    if (data->firsttime) {
        unsigned char *e;

        data->firsttime = 0;

        /* First line: error code */
        e = strchr(buf, '\n');
        if (e) {
            *e = '\0';
        }
        data->status = strtoul(buf, NULL, 16);
        RTV_DBGLOG(RTVLOG_CMD, "%s: status: %s (0x%lx)\n", __FUNCTION__, buf, data->status);
        if ( (rc = map_httpfs_status_to_rc(data->status)) != 0 ) {
           RTV_ERRLOG("%s: bad httpfs returncode: %d\n", __FUNCTION__, rc);
           free(buf);
           return(1); //abort the transfer
        }
        e++;
        len -= (e - buf);
        buf_data_start = e;
    } else {
        buf_data_start = buf;
    }

    // Callers callback 'fn' is responsible for freeing the buffer
    //
    rc = data->fn(buf, len, (buf_data_start - buf), data->v);
    if ( rc == 1 ) {
       RTV_DBGLOG(RTVLOG_CMD, "%s: got abort_read rc from app callback\n", __FUNCTION__);    
    }

    if (data->msec_delay) {
        rtv_sleep(data->msec_delay);
    }
    return(rc);
}

static int hfs_do_chunked(rtv_read_file_chunked_cb_t  fn,
                          void                       *v,
                          const rtv_device_info_t    *device,
                          __u16                       msec_delay,
                          int                         mergechunks,
                          const char                 *command,
                          ...)
{
    struct hfs_data data;
    struct hc *   hc;
    va_list ap;
    int rc;
    
    RTV_DBGLOG(RTVLOG_CMD, "%s: ip_addr=%s cmd=%s\n", __FUNCTION__, device->ipaddr, command);    
    RTV_DBGLOG(RTVLOG_CMD, "%s: cback=%p cback_data=%p delay=%u\n", __FUNCTION__, fn, v, msec_delay);    
    va_start(ap, command);
    hc = make_request(device, command, ap);
    va_end(ap);

    if (!hc) {
        return -ECANCELED;
    }
    memset(&data, 0, sizeof data);
    data.fn         = fn;
    data.v          = v;
    data.firsttime  = 1;
    data.msec_delay = msec_delay;
    
    rc = hc_read_pieces(hc, hfs_callback, &data, mergechunks);
    hc_free(hc);
    if ( rc != 0 ) {
       if ( rc != -ECONNABORTED ) {
          RTV_ERRLOG("%s: hc_read_pieces call failed: rc=%d\n", __FUNCTION__, rc);
       }
       return(rc);
    }
    return(map_httpfs_status_to_rc(data.status));
}

unsigned long hfs_do_post_simple(char **presult, const rtv_device_info_t *device,
                                 int (*fn)(unsigned char *, size_t, void *),
                                 void *v,
                                 unsigned long size,
                                 const char *command, ...)
{
    char          buf[URLSIZE];
    va_list       ap;
    struct hc *   hc;
    char *        tmp, * e;
    int           http_status, rc;
    unsigned long rtv_status;
    unsigned int  len;
    
    va_start(ap, command);
    if (make_httpfs_url(buf, sizeof buf, device, command, ap) < 0)
        return -1;
    va_end(ap);

    RTV_DBGLOG(RTVLOG_CMD, "%s: ip_addr=%s cmd=%s\n", __FUNCTION__, device->ipaddr, command);    
    hc = hc_start_request(buf);
    if (!hc) {
        RTV_ERRLOG("%s: hc_start_request(): %d=>%s",  __FUNCTION__, errno, strerror(errno));
        return -EPROTO;
    } 
    sprintf(buf, "%lu", size);
    if (add_httpfs_headers(hc) != 0)
        return -EPROTO;
    
    hc_add_req_header(hc, "Content-Length",  buf);
    
    hc_post_request(hc, fn, v);

    http_status = hc_get_status(hc);
    if (http_status/100 != 2) {
        RTV_ERRLOG("%s: http status %d\n", __FUNCTION__, http_status);
        hc_free(hc);
        return -ECANCELED;
    }
    
    rc = hc_read_all(hc, &tmp, &len);
    if ( rc != 0 ) {
       RTV_ERRLOG("%s: hc_read_all call failed rc=%d\n", __FUNCTION__, rc);
       hc_free(hc);
       return(rc);
    }
    hc_free(hc);

    e = strchr(tmp, '\n');
    if (e) {
       *presult = strdup(e+1);
       rtv_status = strtoul(tmp, NULL, 16);
       RTV_DBGLOG(RTVLOG_CMD, "%s: httpfs_status=0x%lx\n", __FUNCTION__, rtv_status);    
       free(tmp);
       return(map_httpfs_status_to_rc(rtv_status));
    } else if (http_status == 204) {
       RTV_WARNLOG("%s: http_status == *** 204 ***\n",  __FUNCTION__);
       *presult = NULL;
       free(tmp);
       return 0;
    } else {
       RTV_ERRLOG("%s: end of httpfs status line not found\n",  __FUNCTION__);
       *presult = NULL;
       free(tmp);
       return -EPROTO;
    }
}

static int vstrcmp(const void * v1, const void * v2)
{
   return (strcmp(*(char **)v1, *(char **)v2));
}

//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************

// allocates and returns rtv_fs_volume_t  struct, update volinfo** 
// Returns 0 for success
//
int rtv_get_volinfo( const rtv_device_info_t  *device, const char *name, rtv_fs_volume_t **volinfo )
{
   char             *data;
   int               status;
   rtv_fs_volume_t  *rec;
   int               num_lines, x;
   char            **lines;

   status = hfs_do_simple(&data, device,
                          "volinfo",
                          "name", name,
                          NULL);
   if (status != 0) {
      RTV_ERRLOG("%s:  hfs_do_simple returned %d\n", __FUNCTION__, status);
      if ( data != NULL) free(data);
      *volinfo = NULL;
      return status;
   }
   rec = *volinfo = malloc(sizeof(rtv_fs_volume_t));
   memset(rec, 0, sizeof(rtv_fs_volume_t));
   rec->name = malloc(strlen(name)+1);
   strcpy(rec->name, name);

   num_lines = split_lines(data, &lines);

   for (x = 0; x < num_lines; x++) {
      if (strncmp(lines[x], "cap=", 4) == 0) {
         sscanf(lines[x]+4, "%"U64F"d", &(rec->size));
      } else if (strncmp(lines[x], "inuse=", 6) == 0) {
         sscanf(lines[x]+6, "%"U64F"d", &(rec->used));
      }
      else {
         RTV_WARNLOG("%s: unknown response line: %s\n", __FUNCTION__, lines[x]);
      }
   }
   rec->size_k = rec->size / 1024;
   rec->used_k = rec->used / 1024;
   free(lines);
   free(data);
   return(0);;
}


void rtv_free_volinfo(rtv_fs_volume_t **volinfo) 
{
   rtv_fs_volume_t  *rec;

   if (  volinfo == NULL ) return;
   if ( *volinfo == NULL ) return;

   rec = *volinfo;
   if ( rec->name != NULL ) free(rec->name);
   free(*volinfo);
   *volinfo = NULL;
}


void rtv_print_volinfo(const rtv_fs_volume_t *volinfo) 
{
   if (  volinfo == NULL ) {
      RTV_PRT("volinfo object is NULL!\n");
      return;
   }

   RTV_PRT("RTV Disk Volume: %s\n", volinfo->name);
   RTV_PRT("                 size: %"U64F"d %lu(MB)\n", volinfo->size, volinfo->size_k / 1024);
   RTV_PRT("                 used: %"U64F"d %lu(MB)\n", volinfo->used, volinfo->used_k / 1024);

}


// updates pre-allocated rtv_fs_file_t *fileinfo struct.
// Returns 0 for success
//
int rtv_get_file_info( const rtv_device_info_t  *device, const char *name,  rtv_fs_file_t *fileinfo )
{
   char           *data     = NULL;
   char            type     = RTV_FS_UNKNOWN;
   __u64           filetime = 0;
   __u64           size     = 0;
   int             rc       = 0;
   int             status;
   unsigned int    num_lines, x;
   char          **lines;

   memset(fileinfo, 0, sizeof(rtv_fs_file_t));
   status = hfs_do_simple(&data, device,
                          "fstat",
                          "name", name,
                          NULL);
   if (status != 0) {
      if ( status == -ENOENT ) {
         // File not found. Do nothing.
      }
      else {
         RTV_ERRLOG("%s: hfs_do_simple fstat failed: %d\n", __FUNCTION__, status);
      }
      if ( data != NULL) free(data);
      return(status);
   }

   num_lines = split_lines(data, &lines);
   for (x = 0; x < num_lines; x++) {
      if (strncmp(lines[x], "type=", 5) == 0) {
         type = lines[x][5];
      } else if (strncmp(lines[x], "size=", 5) == 0) {
         sscanf(lines[x]+5, "%"U64F"d", &size);
      } else if (strncmp(lines[x], "ctime=", 6) == 0) {
         sscanf(lines[x]+6, "%"U64F"d", &filetime);
      } else if (strncmp(lines[x], "perm=", 5) == 0) {
         //nothing
      }
      else {
         RTV_WARNLOG("%s: unknown response line: %s\n", __FUNCTION__, lines[x]);
         rc = -EPROTO;
      }
   }

   if ( rc == 0 ) {
      fileinfo->name = malloc(strlen(name)+1);
      strcpy(fileinfo->name, name);
      fileinfo->type          = type;
      fileinfo->time          = filetime;
      fileinfo->time_str_fmt1 = rtv_format_datetime_ms64_1(filetime + 3000); //add 3 seconds to time
      fileinfo->time_str_fmt2 = rtv_format_datetime_ms64_2(filetime + 3000); //add 3 seconds to time
      if ( type == 'f' ) {
         fileinfo->size     = size;
         fileinfo->size_k   = size / 1024;
      }

   //rtv_print_file_info(fileinfo);
   }
   free(lines);
   free(data);
   return(rc);
}


void rtv_free_file_info(rtv_fs_file_t *fileinfo) 
{
   if (  fileinfo == NULL ) return;

   if ( fileinfo->name          != NULL ) free(fileinfo->name);
   if ( fileinfo->time_str_fmt1 != NULL ) free(fileinfo->time_str_fmt1);
   if ( fileinfo->time_str_fmt2 != NULL ) free(fileinfo->time_str_fmt2);
   memset(fileinfo, 0, sizeof(rtv_fs_file_t));
}


void rtv_print_file_info(const rtv_fs_file_t *fileinfo) 
{
   if (  fileinfo == NULL ) {
      RTV_PRT("fileinfo object is NULL!\n");
      return;
   }

   RTV_PRT("RTV File Status: %s\n", fileinfo->name);
   RTV_PRT("                 type: %c\n", fileinfo->type);
   if ( fileinfo->type == 'f' ) {
      RTV_PRT("                 size: %"U64F"d %lu(MB)\n", fileinfo->size, fileinfo->size_k / 1024);
      RTV_PRT("                 time: %"U64F"d\n", fileinfo->time);
      RTV_PRT("                       %s\n", fileinfo->time_str_fmt1);
      RTV_PRT("                       %s\n", fileinfo->time_str_fmt2);
   }
}


// allocates and returns rtv_fs_filelist_t struct. updates filelist** 
// Returns 0 for success
//
int rtv_get_filelist( const rtv_device_info_t  *device, const char *name, int details, rtv_fs_filelist_t **filelist )
{
   char               *data;
   int                 status;
   rtv_fs_filelist_t  *rec;
   unsigned int        num_lines, x;
   char              **lines;
   rtv_fs_file_t       fileinfo;


   // Get file status of the path passed in.
   //
   status = rtv_get_file_info(device, name, &fileinfo);
   if (status != 0) {
      RTV_ERRLOG("%s:  rtv_get_file_info returned %d\n", __FUNCTION__, status);
      *filelist = NULL;
      return status;
   }
   
   // The path is a single file being listed.
   // 
   if ( fileinfo.type == 'f' ) {
      rec = *filelist = malloc(sizeof(rtv_fs_filelist_t));
      memset(rec, 0, sizeof(rtv_fs_filelist_t));
      rec->files = malloc(sizeof(rtv_fs_file_t));
      memcpy(rec->files, &fileinfo, sizeof(rtv_fs_file_t));
      rec->pathname = malloc(strlen(name)+1);
      strcpy(rec->pathname, name);
      rec->num_files = 1;
      return(0);
   }

   // The path is a directory
   //
   status = hfs_do_simple(&data, device,
                          "ls",
                          "name", name,
                          NULL);
   if (status != 0) {
      RTV_ERRLOG("%s:  hfs_do_simple returned %d\n", __FUNCTION__, status);
      if ( data != NULL) free(data);
      *filelist = NULL;
      return status;
   }

   num_lines = split_lines(data, &lines);
   qsort(lines, num_lines, sizeof(char *), vstrcmp);

   rec = *filelist = malloc(sizeof(rtv_fs_filelist_t));
   memset(rec, 0, sizeof(rtv_fs_filelist_t));
   rec->pathname = malloc(strlen(name)+1);
   strcpy(rec->pathname, name);
   rec->num_files = num_lines;  
   rec->files = malloc(sizeof(rtv_fs_file_t) * num_lines);
   memset(rec->files, 0, sizeof(rtv_fs_file_t) * num_lines);
   //printf("*** struct: %p filesbase: %p: numfiles: %d\n", rec, rec->files, num_lines);

   if ( details == 0 ) {
      // Just list the file names
      //
      for (x = 0; x < num_lines; x++) {
         rec->files[x].name = malloc(strlen(lines[x]) + 1);
         strcpy(rec->files[x].name, lines[x]); 
      }
   }
   else {
      // Get all the file info
      //
      for (x = 0; x < num_lines; x++) {
         char pathfile[256];
         snprintf(pathfile, 255, "%s/%s", rec->pathname, lines[x]);
         status = rtv_get_file_info( device, pathfile,  &(rec->files[x]));
         if ( status != 0 ) {
            RTV_ERRLOG("%s: rtv_get_file_info returned: %d\n", __FUNCTION__, status);
            break;
         }
      }
   }
   
   free(lines);
   free(data);
   return(0);
}


void rtv_free_file_list( rtv_fs_filelist_t **filelist ) 
{
   rtv_fs_filelist_t  *rec;
   unsigned int        x;

   if (  filelist == NULL ) return;
   if ( *filelist == NULL ) return;

   rec = *filelist;
   
   for (x=0; x < rec->num_files; x++) {
      rtv_free_file_info(&(rec->files[x]));
   }

   if ( rec->pathname != NULL ) free(rec->pathname);
   free(*filelist);
   *filelist = NULL;
}


void rtv_print_file_list(const rtv_fs_filelist_t *filelist, int detailed) 
{
   unsigned int x;

   if (  filelist == NULL ) {
      RTV_PRT("filelist object is NULL!\n");
      return;
   }

   RTV_PRT("RTV File listing\n");
   RTV_PRT("PATH: %s       Number of files: %u\n", filelist->pathname, filelist->num_files);
   RTV_PRT("------------------------------------------\n");
   for (x=0; x < filelist->num_files; x++) {
      printf("%-25s", filelist->files[x].name);
      if (detailed) {
         printf("   %19s    type=%c  size: %11"U64F"d %7lu(MB)", 
                filelist->files[x].time_str_fmt1, filelist->files[x].type, filelist->files[x].size, filelist->files[x].size_k / 1024 );
      }
      printf("\n");
   }

}


// read a file from the RTV.
// Callback data returned in 128KB chunks
// Returns 0 for success
//
__u32  rtv_read_file_chunked( const rtv_device_info_t    *device, 
                              const char                 *filename, 
                              __u64                       pos,        //fileposition
                              __u64                       size,       //amount of file to read ( 0 reads all of file )
                              unsigned int                ms_delay,   //mS delay between reads
                              rtv_read_file_chunked_cb_t  callback_fxn,
                              void                       *callback_data  )
{
   __u32 status;
   char  pos_str[256];
   char  size_str[256];
   
   snprintf(pos_str, 255, "%"U64F"d", pos);

   if ( size != 0 ) {
      snprintf(size_str, 255, "%"U64F"d", size);
      status = hfs_do_chunked(callback_fxn, callback_data, device, ms_delay, rtv_globals.merge_chunk_sz,
                              "readfile",
                              "pos",  pos_str,
                              "size", size_str,
                              "name", filename,
                              NULL);
   }
   else {
      status = hfs_do_chunked(callback_fxn, callback_data, device, ms_delay, rtv_globals.merge_chunk_sz,
                              "readfile",
                              "pos",  pos_str,
                              "name", filename,
                              NULL);
   }
   return(status);
} 


// read a file.
// File data is returned when call completes
// Returned data is malloc'd. User must free.
//
__u32  rtv_read_file( const rtv_device_info_t  *device, 
                      const char               *filename, 
                      __u64                     pos,        //fileposition
                      __u64                     size,       //amount of file to read
                      rtv_http_resp_data_t     *data  )
{
   int   status;
   char  pos_str[256];
   char  size_str[256];
   
   snprintf(pos_str, 255, "%"U64F"d", pos);
   snprintf(size_str, 255, "%"U64F"d", size);

   status = hfs_do_simple_binary( data, device,
                                  "readfile",
                                  "pos",  pos_str,
                                  "size", size_str,
                                  "name", filename,
                                  NULL);
   return(status);
} 
