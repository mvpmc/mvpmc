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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "rtv.h"
#include "rtvlib.h"
#include "httpclient.h"

struct hc_headers 
{
    char * tag;
    char * value;
    struct hc_headers * next;
};

struct hc 
{
    struct nc * nc;
    char * hostname;
    short port;
    char * localpart;
    char * status;
    struct hc_headers * req_headers;
    struct hc_headers * rsp_headers;
};


static void hc_dump_struct( const struct hc * hc_struct ) 
{
   RTV_PRT("         hostname:  %s  port: %d\n", hc_struct->hostname,  hc_struct->port);
   RTV_PRT("         status:    %s\n", hc_struct->status);
   if ( hc_struct->req_headers != NULL ) 
      RTV_PRT("         reqhdrs->tag: %s   reqhdrs->value: %s\n", hc_struct->req_headers->tag, hc_struct->req_headers->value);
   if ( hc_struct->rsp_headers != NULL ) 
      RTV_PRT("         rsphdrs->tag: %s   rsphdrs->value: %s\n", hc_struct->rsp_headers->tag, hc_struct->rsp_headers->value);
   RTV_PRT("         localpart: %s\n", hc_struct->localpart);
}

static void hc_h_free(struct hc_headers * hh)
{
    free(hh->tag);
    free(hh->value);
    free(hh);
}

static struct hc_headers * hc_h_make(const char * tag, const char * value)
{
    struct hc_headers * h;
    
    h = malloc(sizeof *h);
    if (!h)
        goto error;
    memset(h, 0, sizeof *h);
    
    h->tag = strdup(tag);
    if (!h->tag)
        goto error;
    
    h->value = strdup(value);
    if (!h->value)
        goto error;
    
    return h;
error:
    if (h)
        hc_h_free(h);
    return NULL;
}

void hc_free(struct hc * hc)
{
    struct hc_headers * n;

    if (hc) {
        free(hc->hostname);
        free(hc->localpart);
        free(hc->status);
        while (hc->req_headers) {
            n = hc->req_headers->next;
            hc_h_free(hc->req_headers);
            hc->req_headers = n;
        }
        while (hc->rsp_headers) {
            n = hc->rsp_headers->next;
            hc_h_free(hc->rsp_headers);
            hc->rsp_headers = n;
        }
        if (hc->nc)
            nc_close(hc->nc);
        free(hc);
    }
}

int hc_add_req_header(struct hc * hc, const char * tag, const char * value)
{
    struct hc_headers * h;

    RTV_DBGLOG(RTVLOG_HTTP_VERB, "%s: tag=%s val=%s\n", __FUNCTION__, tag, value); 
    h = hc_h_make(tag, value);
    if (!h)
        goto error;
    h->next = hc->req_headers;
    hc->req_headers = h;
    return 0;
error:
    return -1;
}

struct hc * hc_start_request(char * url)
{
    struct hc * hc;
    char * e;

    hc = malloc(sizeof *hc);
    memset(hc, 0, sizeof *hc);
    
    RTV_DBGLOG(RTVLOG_HTTP_VERB, "%s: url=%s\n", __FUNCTION__, url); 
    if (strncmp(url, "http://", 7) != 0) {
        RTV_ERRLOG("%s: malformed url:%s\n", __FUNCTION__, url); 
        goto error;
    }
    url += 7;
    e = strchr(url, '/');
    if (!e) {
        hc->hostname = strdup(url);
        hc->localpart = strdup("/");
    } else {
        hc->hostname        = malloc(e - url + 1);
        if (!hc->hostname) goto error;
        memcpy(hc->hostname, url, e - url);
        hc->hostname[e-url] = '\0';
        hc->localpart       = strdup(e);
        if (!hc->localpart) goto error;
    }
    RTV_DBGLOG(RTVLOG_HTTP_VERB, "%s: hostname=%s local_part=%s\n", __FUNCTION__, hc->hostname, hc->localpart); 

    if (hc_add_req_header(hc, "Host", hc->hostname) < 0) {
        RTV_ERRLOG("%s: hc_add_req_header failed: %s\n", __FUNCTION__, hc->hostname); 
        goto error;
    }

    e = strchr(hc->hostname, ':');
    if (e) {
        *e = '\0';
        hc->port = (short)strtoul(e+1, NULL, 10);
    } else {
        hc->port = 80;
    }

    hc->nc = nc_open(hc->hostname, hc->port);
    if (!hc->nc) {
        RTV_ERRLOG("%s: nc_open failed.\n", __FUNCTION__); 
        goto error;
    }
    
    return hc;
error:
    if (hc)
        hc_free(hc);
    return NULL;
}

int hc_send_request(struct hc * hc, const char *append)
{
    char buffer[1024];
    size_t l;
    struct hc_headers * h;
    int x = 1;

    l = strlen(hc->localpart) + strlen("GET  HTTP/1.1\r\n");
    for (h = hc->req_headers; h; h = h->next)
        l += strlen(h->tag) + strlen(h->value) + 4;
    l += 3;
    if (l > sizeof buffer) {
        RTV_ERRLOG("hc_send_request: buffer too small; need %lu\n",
                (unsigned long)l);
        return -1;
    }

    l = sprintf(buffer, "GET %s HTTP/1.1\r\n", hc->localpart);
    for (h = hc->req_headers; h; h = h->next)
        l += sprintf(buffer+l, "%s: %s\r\n", h->tag, h->value);

    if ( append != NULL ) {
       l -=2;
       l += sprintf(buffer+l, "%s\r\n", append);
    }
    else {
       l += sprintf(buffer+l, "\r\n");
    }
    RTV_DBGLOG(RTVLOG_HTTP_VERB, "%s: nc_write: %s\n", __FUNCTION__, buffer); 
    nc_write(hc->nc, buffer, l);

    if (nc_read_line(hc->nc, buffer, sizeof buffer) <= 0) {
        RTV_ERRLOG("hc_send_request nc_read_line: %d=>%s\n", errno, strerror(errno));
        return -1;
    }
    
    hc->status = strdup(buffer);
    RTV_DBGLOG(RTVLOG_HTTP, "%s: status: %s\n", __FUNCTION__, hc->status); 
    RTV_DBGLOG(RTVLOG_HTTP, "%s: start nc_read_line...\n", __FUNCTION__); 
    while (nc_read_line(hc->nc, buffer, sizeof buffer) > 0) {
        char *e;
        RTV_DBGLOG(RTVLOG_HTTP, "%s: line: %d: %s\n", __FUNCTION__, x++, buffer); 
        e = strchr(buffer, ':');
        if (e) {
            *e = '\0';
            e++;
            while (isspace(*e))
                e++;
            RTV_DBGLOG(RTVLOG_HTTP, "%s: value: %s\n", __FUNCTION__, e); 
            h = hc_h_make(buffer, e);
            h->next = hc->rsp_headers;
            hc->rsp_headers = h;
        } else {
            RTV_ERRLOG("hc_send_request got invalid header line :%s:\n", buffer);
        }
    }
    RTV_DBGLOG(RTVLOG_HTTP, "%s: done nc_read_line.\n", __FUNCTION__); 
    return 0;
}

int hc_post_request(struct hc *hc,
                    int (*callback)(unsigned char *, size_t, void *),
                    void * v)
{
    char buffer[1024];
    size_t l;
    struct hc_headers * h;

    l = strlen(hc->localpart) + strlen("POST  HTTP/1.0\r\n");
    for (h = hc->req_headers; h; h = h->next)
        l += strlen(h->tag) + strlen(h->value) + 4;
    l += 3;
    if (l > sizeof buffer) {
       RTV_ERRLOG("ERROR: hc_post_request: buffer too small; need %lu\n",
                  (unsigned long)l);
        return -1;
    }

    l = sprintf(buffer, "POST %s HTTP/1.0\r\n", hc->localpart);
    for (h = hc->req_headers; h; h = h->next)
        l += sprintf(buffer+l, "%s: %s\r\n", h->tag, h->value);
    l += sprintf(buffer+l, "\r\n");
    nc_write(hc->nc, buffer, l);

    while ((l = callback(buffer, sizeof buffer, v)) != 0) {
        int r;
        r = nc_write(hc->nc, buffer, l);        
    }
  
    if (nc_read_line(hc->nc, buffer, sizeof buffer) <= 0) {
        RTV_ERRLOG("hc_post_request nc_read_line: %d =>%s\n", errno, strerror(errno));
        return -1;
    }
    hc->status = strdup(buffer);
    while (nc_read_line(hc->nc, buffer, sizeof buffer) > 0) {
        char * e;
        e = strchr(buffer, ':');
        if (e) {
            *e = '\0';
            e++;
            while (isspace(*e))
                e++;
            h = hc_h_make(buffer, e);
            h->next = hc->rsp_headers;
            hc->rsp_headers = h;
        } else {
            RTV_ERRLOG("hc_post_request got invalid header line :%s:\n", buffer);
        }
    }
    
    return 0;
}

int hc_get_status(const struct hc *hc)
{
    return strtoul(hc->status + 9, NULL, 10);
}

char *hc_lookup_rsp_header(const struct hc *hc, const char *tag)
{
    struct hc_headers * h;
    
    for (h = hc->rsp_headers; h; h = h->next)
        if (strcasecmp(tag, h->tag) == 0)
            return h->value;
    return NULL;
}

extern int hc_read_pieces(const struct hc       *hc,
                          rtv_read_chunked_cb_t  callback,
                          void                  *v,
                          rtv_mergechunks_t      mergechunks)
{
    int          chunked     = 0;
    int          done        = 0;
    int          x           = 1;
    int          rc          = 0;
    unsigned int multichunk  = 0;
    size_t       len_total   = 0;
    char        *buf         = NULL; 
    char        *bufstart    = NULL;
    char        *te;
    size_t       len, len_read;
    
    RTV_DBGLOG(RTVLOG_HTTP_VERB, "%s: hc struct dump:\n", __FUNCTION__);
    if ( RTVLOG_HTTP_VERB ) {
       hc_dump_struct(hc);
    }
    if ( hc->status ) {
       if ( (hc_get_status(hc) / 100) != 2 ) {
          RTV_ERRLOG("%s: HTTP status error code: %s\n", __FUNCTION__, hc->status);
          return(-EBADMSG);
       }
    }
    te = hc_lookup_rsp_header(hc, "Transfer-Encoding");
    RTV_DBGLOG(RTVLOG_HTTP, "%s: lookup_rsp_header: tag: %s    value: %s\n", __FUNCTION__, "Transfer-Encoding", te);    
    if (te && strcmp(te, "chunked") == 0) {
        chunked = 1;
    }

    if (mergechunks > 1) {
       multichunk = mergechunks;
    } 
    else {
       mergechunks = 0;
    }

    while (!done) {
        if (chunked) {
            char lenstr[32];

            nc_read_line(hc->nc, lenstr, sizeof lenstr);
            len = strtoul(lenstr, NULL, 16);
            RTV_DBGLOG(RTVLOG_HTTP, "%s: lenstr: %d: 0x%s = %d\n", __FUNCTION__, x++, lenstr, len); 
        } else {
            len = 4096;
        }
        if (len) {
            if ( mergechunks == 0 ) {
               buf = malloc(len+1);
            }
            else if ( multichunk == mergechunks ) {
               bufstart = buf = malloc((RTV_CHUNK_SZ * multichunk) + 1);
               len_total = 0;
               RTV_DBGLOG(RTVLOG_HTTP, "%s: multichunk start: malloc=%p sz=%d \n", __FUNCTION__, buf, (RTV_CHUNK_SZ * multichunk) + 1); 
            }

            len_read = nc_read(hc->nc, buf, len);
            if (len_read < len) {
                done = 1;
            }

            if  ( mergechunks == 0 ) {
               buf[len_read] = '\0';
               RTV_DBGLOG(RTVLOG_HTTP_VERB, "%s: line: %d: len=%d len_rd=%d\n %s\n", __FUNCTION__, x++, len, len_read, buf); 
               if ( (rc = callback(buf, len_read, v)) != 0 ) {
                  RTV_DBGLOG(RTVLOG_HTTP, "%s: callback rc=%d: abort transfer\n", __FUNCTION__, rc); 
                  break;
               }
            }
            else {
               len_total += len_read;
               buf[len_read] = '\0';
               if ( multichunk > 1 ) {
                  RTV_DBGLOG(RTVLOG_HTTP, "%s: multichunk add: bp=%p sz=%d \n", __FUNCTION__, buf, len_read); 
                  multichunk--;
                  buf += len_read;
               }
               else {
                  RTV_DBGLOG(RTVLOG_HTTP, "%s: multichunk callback: bstart=%p sz_tot=%d \n", __FUNCTION__, bufstart, len_total); 
                  if ( (rc = callback(bufstart, len_total, v)) != 0 ) {
                     RTV_DBGLOG(RTVLOG_HTTP, "%s: callback rc=%d: abort transfer\n", __FUNCTION__, rc); 
                     break;
                  }
                  multichunk = mergechunks;
               }
            }


        } else {
            if ( (mergechunks) && (multichunk != mergechunks) ) {
               RTV_DBGLOG(RTVLOG_HTTP, "%s: multichunk DONE callback: bstart=%p sz_tot=%d \n", __FUNCTION__, bufstart, len_total); 
 
               //don't worry about the rc as we are done anyway.
               rc = callback(bufstart, len_total, v);
                
            }
            RTV_DBGLOG(RTVLOG_HTTP, "%s: LEN=0\n", __FUNCTION__); 
            done = 1;
        }

        if (chunked) {
            char linebuf[80];
            /* if done, then any non-blank lines read here are
               supposed to be treated as HTTP header lines, but since
               we didn't say we could accept trailers, the server's
               only allowed to send them if it's happy with us
               discarding them. (2616 3.7b).  The Replay doesn't use
               trailers anyway */
            while (nc_read_line(hc->nc, linebuf, sizeof linebuf) > 0) {
               RTV_DBGLOG(RTVLOG_HTTP, "%s: trailer: %s\n", __FUNCTION__, linebuf); 
            }
        }
    } //while
    return(rc);;
}

struct chunk
{
    char         *buf;
    size_t        len;
    struct chunk *next;
};

struct read_all_data 
{
    struct chunk *start;
    struct chunk *end;
    size_t total;
};

static int read_all_callback(unsigned char * buf, size_t len, void * vd)
{
    struct read_all_data *data   = vd;
    struct chunk         *chunk;

    chunk = malloc(sizeof *chunk);
    chunk->buf = buf;
    chunk->len = len;
    chunk->next = NULL;
    
    if (data->end) {
        data->end->next = chunk;
        data->end = chunk;
    } else {
        data->start = data->end = chunk;
    }

    data->total += len;
    return(0);
}

int hc_read_all(struct hc *hc, char **data_p)
{
    struct read_all_data  data;
    struct chunk         *chunk, *next;
    size_t                cur;
    unsigned char        *r;
    int                   rc;

    data.start = data.end = NULL;
    data.total = 0;
    
    rc = hc_read_pieces(hc, read_all_callback, &data, RTV_MERGECHUNKS_0);
    if ( rc != 0 ) {
       RTV_ERRLOG("%s: hc_read_pieces call failed: rc=%d\n", __FUNCTION__, rc);
       *data_p = NULL;
       return(rc);
    }
    
    r = malloc(data.total + 1);
    cur = 0;
    for (chunk = data.start; chunk; chunk = next) {
        memcpy(r+cur, chunk->buf, chunk->len);
        cur += chunk->len;
        free(chunk->buf);
        next = chunk->next;
        free(chunk);
    }

    r[data.total] = '\0';
    *data_p = r;
    return (0);
}



