/*
 *  Copyright (C) 2004, John Honeycutt
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rtv.h"
#include "rtvlib.h"
#include "guideparser.h"
#include "rtvserver.h"

#define TCP_BUF_SZ (4096)

#define HTTP_HEAD_STR "HTTP/1.1 200 OK\r\nDate: Fri, 02 Jan 1970 11:23:09 GMT\r\nServer: Unknown/0.0 UPnP/1.0 Virata-EmWeb/R6_0_1\r\n"\
                      "Transfer-Encoding: chunked\r\nContent-Type: text/xml\r\nExpires: Fri, 02 Jan 1970 11:23:09 GMT\r\n"\
                      "Last-Modified: Fri, 02 Jan 1970 11:23:09 GMT\r\nCache-Control: no-cache\r\nPragma: no-cache\r\n\r\n"

// Parms: IP, name, sn, uuid
//
#define GET_DEV_DESCR_RESP_STR \
 "<?xml version=\"1.0\"?>\n\n\n"\
 "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"\
 "    <specVersion>\n\t<major>1</major>\n\t<minor>0</minor>\n    </specVersion>\n"\
 "    <URLBase>http://%s/</URLBase>\n"\
 "    <device>\n"\
 "\t<deviceType>urn:replaytv-com:device:ReplayDevice:1</deviceType>\n"\
 "\t<friendlyName>%s</friendlyName>\n"\
 "\t<modelDescription>ReplayTV 5000 Digital Video Recorder</modelDescription>\n"\
 "\t<modelName>ReplayTV 5000</modelName>\n"\
 "\t<modelNumber>5040</modelNumber>\n"\
 "\t<version>530511440</version>\n"\
 "\t<modelURL>http://www.sonicblue.com</modelURL>\n"\
 "\t<manufacturer>SONICblue Incorporated</manufacturer>\n"\
 "\t<manufacturerURL>http://www.sonicblue.com</manufacturerURL>\n"\
 "\t<serialNumber>%s</serialNumber>\n"\
 "\t<UDN> uuid:%s </UDN>\n"\
 "\t<UPC>None</UPC>\n"\
 "    </device>\n"\
 "</root>\n"

// Parms: Attached file length
//
#define GUIDE_SS_START_STR "00000000\nguide_file_name=1099596117\nRemoteFileName=1099596117\nFileLength=%04d\n#####ATTACHED_FILE_START#####"

#define VOLINFO_RESP_STR "0000001d\r\n0\ncap=999948288\ninuse=589824\n\r\n0\r\n\r\n"

#define LS_RESP_STR "00000002\r\n0\n\r\n0\r\n\r\n"

static int make_dev_descr_resp( char *buf, int bufsz, char *ip, char *name, char *sn, char *uuid )
{
   int   hdrlen, bodylen, taillen;
   char *bptr;
   char  bodylenstr[20];

   hdrlen  = snprintf(buf, bufsz-1, HTTP_HEAD_STR); 
   bptr    = buf + hdrlen + 10; // 10 is body size string (8 ASCII digits plus cr/lf)
   bodylen = snprintf(bptr, bufsz - hdrlen - 10, GET_DEV_DESCR_RESP_STR, ip, name, sn, uuid);
   taillen = snprintf(bptr + bodylen, bufsz - hdrlen - bodylen - 10, "\n\r\n0\r\n\r\n");
   sprintf(bodylenstr, "%08x\r\n", bodylen+2);
   memcpy(buf+hdrlen, bodylenstr, 10);
   
   return(hdrlen+10+bodylen+taillen);
} 

static int make_get_guide_resp( char *buf, int bufsz )
{
   int   hdrlen, sslen;
   char *bptr, *p1, *ss;
   char  bodylenstr[20];
   
   if ( rtv_emulate_mode == RTV_DEVICE_4K ) {
      sslen = build_v1_bogus_snapshot(&ss);
   }
   else {
      sslen = build_v2_bogus_snapshot(&ss);
   }
   RTV_DBGLOG(RTVLOG_DSCVR, "%s:----------guide_sz=%d\n", __FUNCTION__, sslen);
   hdrlen  = snprintf(buf, bufsz-1, HTTP_HEAD_STR); 
   bptr    = buf + hdrlen + 10; // 10 is body size string (8 ASCII digits plus cr/lf)
   p1 = bptr + snprintf(bptr, bufsz - hdrlen - 10, GUIDE_SS_START_STR, sslen);
   memcpy(p1, ss, sslen);
   p1 += sslen;
   p1 += snprintf(p1, bufsz - (p1-buf), "#####ATTACHED_FILE_END#####");
   sprintf(bodylenstr, "%08x\r\n", p1-bptr);
   RTV_DBGLOG(RTVLOG_DSCVR, "%s:body len ---------------> %d\n", __FUNCTION__, p1-bptr);
   memcpy(buf+hdrlen, bodylenstr, 10);
   p1 += snprintf(p1, bufsz - (p1-buf), "\r\n0\r\n\r\n");
   free(ss);
   return(p1-buf);
} 

static int make_volinfo_resp( char *buf, int bufsz )
{
   int   hdrlen, bodylen;

   hdrlen  = snprintf(buf, bufsz-1, HTTP_HEAD_STR); 
   bodylen = snprintf(buf+hdrlen, bufsz - hdrlen, VOLINFO_RESP_STR);
   return(hdrlen+bodylen);
} 

static int make_ls_resp( char *buf, int bufsz )
{
   int   hdrlen, bodylen;

   hdrlen  = snprintf(buf, bufsz-1, HTTP_HEAD_STR); 
   bodylen = snprintf(buf+hdrlen, bufsz - hdrlen, LS_RESP_STR);
   return(hdrlen+bodylen);
} 

int server_open_port(char *host, int port)
{
   int                errno_sav;
	int                fd;
	struct in_addr		 inaddr;
   struct sockaddr_in servaddr;

   // Setup the addr struct
   //
   bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	if (host == NULL) {
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
   }
	else {
		if (inet_aton(host, &inaddr) == 0) {
			RTV_ERRLOG("%s: invalid host name for server: %s\n", __FUNCTION__, host);
         return(-EINVAL);
      }
		servaddr.sin_addr = inaddr;
	}
   servaddr.sin_port = htons(port);

   // bind the socket
   //
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (bind(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
      errno_sav = errno;
		RTV_ERRLOG("%s: unable to bind local address: port=%d: %s\n", __FUNCTION__, port, strerror(errno_sav));
      if ( errno_sav == EACCES ) {
         RTV_ERRLOG("%s: must be root to bind to ports below 1024\n", __FUNCTION__);
      }
      return(-errno_sav);
   }

   // monitor for incoming connections
   //
	if ( listen(fd, MAX_RTVS) != 0 ) {
      errno_sav = errno;
		RTV_ERRLOG("%s: listen failed: %s\n", __FUNCTION__, port, strerror(errno_sav));
      return(-errno_sav);
   }

   return(fd);
}


int server_process_connection(int fd)
{
   int                errno_sav, len;
   char               rxbuff[TCP_BUF_SZ+1], txbuff[TCP_BUF_SZ+1];

   RTV_DBGLOG(RTVLOG_DSCVR, "%s: Enter...\n", __FUNCTION__);
   if ( (len = read(fd, rxbuff, TCP_BUF_SZ)) < 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: read failed: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav) );
      exit(-errno_sav);
   }

   rxbuff[len] = '\0';
   RTV_DBGLOG(RTVLOG_DSCVR, "%s\n", rxbuff);
   if ( strstr(rxbuff, "Device_Descr.xml") != NULL ) {
      if ( rtv_emulate_mode == RTV_DEVICE_4K ) {
         len = make_dev_descr_resp( txbuff, TCP_BUF_SZ, local_ip_address, local_hostname, rtv_idns.sn_4k, rtv_idns.uuid_4k);
      }
      else {
         len = make_dev_descr_resp( txbuff, TCP_BUF_SZ, local_ip_address, local_hostname, rtv_idns.sn_5k, rtv_idns.uuid_5k);
      }
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: Respond Device_Descr.xml\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
   }
   else if ( strstr(rxbuff, "get_snapshot") != NULL ) {
      len = make_get_guide_resp(txbuff, TCP_BUF_SZ);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: get_snapshot\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
   }
   else if ( strstr(rxbuff, "httpfs-volinfo") != NULL ) {
      len = make_volinfo_resp(txbuff, TCP_BUF_SZ);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: httpfs-volinfo\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
   }
   else if ( strstr(rxbuff, "httpfs-ls") != NULL ) {
      len = make_ls_resp(txbuff, TCP_BUF_SZ);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: httpfs-ls\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
   }

   sleep(2);
   close(fd);
   exit(0);
}
