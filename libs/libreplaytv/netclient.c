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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "netclient.h"
#include "rtv.h"

#ifdef _WIN32
#  include <winsock2.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET socket_fd;
#else
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>

#  define closesocket(fd) close(fd)
   typedef int socket_fd;
#  define INVALID_SOCKET (-1)
#endif

#ifndef MIN
#define MIN(x,y) ((x)<(y) ? (x) : (y))
#endif

static int nc_do_dump = 0;
static int nc_initted = 0;

struct nc
{
    u8        rcv_buf[32768];
    size_t    low, high;
    socket_fd fd;
};

static void nc_fini(void)
{
    nc_initted = 0;

#ifdef _WIN32
    WSACleanup();
#endif
}

static void nc_dump(char * tag, unsigned char * buf, size_t sz)
{
    unsigned int  rows, row, col, i, c;
    char          tmpstr[512];
    char         *strp = tmpstr;

    if (!nc_do_dump)
        return;
    RTV_PRT("rtv:NC DUMP: %s\n", tag);
    rows = (sz + 15)/16;
    for (row = 0; row < rows; row++) {
        strp += sprintf(strp, "| ");
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
        strp += sprintf(strp, "  |  ");
        for (col = 0; col < 16; col++) {
            i = row * 16 + col;
            if (i < sz) {
                c = buf[i];
                strp += sprintf(strp, "%c", (c >= ' ' && c <= '~') ? c : '.');
            } else {
                strp += sprintf(strp, " ");
            }
            if ((i & 3) == 3) {
                strp += sprintf(strp, " ");
            }
        }
        RTV_PRT("%s |\n", tmpstr);
        strp = tmpstr;
    }
}

void nc_error(char * where)
{
#ifdef _WIN32
    RTV_ERRLOG("NC error:%s:%d/%d\n", where, errno, WSAGetLastError());
#else
    RTV_ERRLOG("NC error:%s:%d:%s\n", where, errno, strerror(errno));
#endif
}

static int nc_init(void)
{
#ifdef _WIN32
    WSADATA wd;
    
    nc_dump("initting", NULL, 0);
    
    if (WSAStartup(MAKEWORD(2,2), &wd) == -1) {
        nc_error("WSAStartup");
        return -1;
    }
#endif
    nc_initted = 1;

    if (getenv("NC_DUMP"))
        nc_do_dump = 1;
    if ( RTVLOG_NETDUMP ) 
        nc_do_dump = 1; 

    atexit(nc_fini);
    return 0;
}

struct nc * nc_open(char * address_str, short port)
{
    struct nc * nc;
    struct sockaddr_in address;
    int rcvbuf_window_size =  4 * 1024;	/*  4 kilobytes */

    RTV_DBGLOG(RTVLOG_NET, "%s: addr=%s port=%d\n", __FUNCTION__, address_str, port); 
    if (!nc_initted) {
        if (nc_init()) {
            RTV_ERRLOG("Couldn't initialize netclient library\n");
            return NULL;
        }
    }
    
    nc = malloc(sizeof *nc);
    if (!nc) {
        RTV_ERRLOG("Couldn't allocate memory for struct nc\n");
        return NULL;
    }
    memset(nc, 0, sizeof *nc);
    nc->fd = -1;

    memset(&address, 0, sizeof address);
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = inet_addr(address_str);

    nc_dump("creating socket", NULL, 0);
    nc->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (nc->fd == INVALID_SOCKET) {
        nc_error("open_nc socket");
        return NULL;
    }
    nc_dump("created", NULL, 0);

    // Set a small 4K RCVBUF window size to force tcp stack to quickly ack packets.
    // Without this, streaming highres shows is jerky.
    //
    setsockopt(nc->fd, SOL_SOCKET, SO_RCVBUF,
               (char *) &rcvbuf_window_size, sizeof(rcvbuf_window_size));

    nc_dump("connecting", NULL, 0);
    if (connect(nc->fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        nc_error("open_nc connect");
        return NULL;
    }
    nc_dump("connected", NULL, 0);

    return nc;
}

int nc_close(struct nc * nc)
{
    int r = 0;
    
    if (nc->fd >= 0) {
        nc_dump("closing", NULL, 0);
        r = closesocket(nc->fd);
        nc_dump("closed", NULL, 0);
    }
    free(nc);

    return r;
}

static void fill_rcv_buf(struct nc * nc)
{
    int r;
    
    nc_dump("receiving...", NULL, 0);
    r = recv(nc->fd, nc->rcv_buf, sizeof(nc->rcv_buf), 0);
    nc->low  = 0;
    if (r <= 0) {
        if (r < 0)
            nc_error("fill_rcv_buf recv");
        /* XXX -- error or closed, return whatever we got*/
        nc->high = 0;
    } else {
        nc->high = r;
    }
    nc_dump("received", nc->rcv_buf, nc->high);
}

int nc_read(struct nc * nc, unsigned char * buf, size_t len)
{
    size_t l;
    int r = 0;
    
    while (len) {
        if (nc->high == nc->low) {
            fill_rcv_buf(nc);
        }
        if (nc->high == 0) {
            return r;
        }
        l = MIN(len, nc->high - nc->low);
        memcpy(buf + r, nc->rcv_buf + nc->low, l);
        len     -= l;
        r       += l;
        nc->low += l;
    }
    return r;
}

int nc_read_line(struct nc * nc, unsigned char * buf, size_t maxlen)
{
    size_t r = 0;

    while (r < maxlen) {
        if (nc_read(nc, buf + r, 1) <= 0) {
            return r;
        }
        if (buf[r] == '\012') {
            buf[r] = '\0';
            if (r > 0 && buf[r-1] == '\015') {
                r--;
                buf[r] = '\0';
            }
            return r;
        }
        r++;
    }
    return r;
}

int nc_write(struct nc * nc, unsigned char * buf, size_t len)
{
    int r;
    nc_dump("sending...", buf, len);
    r = send(nc->fd, buf, len, 0);
    if (r < 0)
        nc_error("nc_write");
    nc_dump("sent", NULL, 0);
    return r;
}
