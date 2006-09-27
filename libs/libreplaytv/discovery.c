/*
 *  Copyright (C) 2004-2006, John Honeycutt
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/times.h>
#include "rtv.h"
#include "rtvlib.h"
#include "guideparser.h"

#define SSDP_BUF_SZ (1023)
#define TCP_BUF_SZ  (4096)

#define SSDP_QUERY_STR "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN:\"ssdp:discover\"\r\n"\
                       "ST: urn:replaytv-com:device:ReplayDevice:1\r\nMX: 3\r\n\r\n"

//  SSDP_NOTIFY_HEAD_STR takes either "alive" or "byebye" parm
//
#define SSDP_NOTIFY_HEAD_STR "NOTIFY * HTTP/1.1\r\nDate: Mon, 05 Jan 1970 23:11:55 GMT\r\n"\
                             "Server: Unknown/0.0 UPnP/1.0 Virata-EmWeb/R6_0_1\r\n"\
                             "HOST: 239.255.255.250:1900\r\nNTS: ssdp:%s\r\n"\
                             "CACHE-CONTROL: max-age = 1830\r\n"\
                             "LOCATION: http://%s/Device_Descr.xml\r\n"
#define SSDP_NOTIFY_TAIL1_STR "NT: upnp:rootdevice\r\nUSN: uuid:%s::upnp:rootdevice\r\n\r\n"
#define SSDP_NOTIFY_TAIL2_STR "NT: uuid:%s\r\nUSN: uuid:%s\r\n\r\n"
#define SSDP_NOTIFY_TAIL3_STR "NT: urn:replaytv-com:device:ReplayDevice:1\r\nUSN: uuid:%s::urn:replaytv-com:device:ReplayDevice:1\r\n\r\n"

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



//+*********************************
// Queue used to delay closing fd's
//+*********************************

#define FD_QUEUE_SZ (100)

typedef struct fd_item_t {
   int     fd;
   clock_t ticks;
} fd_item_t;

typedef struct clsfd_queue_t {
   int head;
   int tail;
   int hz;
   fd_item_t q[FD_QUEUE_SZ];
} clsfd_queue_t;

//+******************************
// Locals
//+******************************

static clsfd_queue_t fd_queue;

static pthread_t server_thread_id = 0;
static int       num_rtv_discovered;
static int       terminate_discovery_thread;


//+*****************************************************************
//  Name: get_fd_queue_head 
//+*****************************************************************
static int get_fd_queue_head(fd_item_t *item)
{
   int head;

   if ( fd_queue.tail == fd_queue.head ) {
      return(-1);
   }
   head = (fd_queue.head + 1) % FD_QUEUE_SZ;

   item->ticks = fd_queue.q[head].ticks;
   item->fd    = fd_queue.q[head].fd;
   return(0);
}

//+*****************************************************************
//  Name:  pull_fd_queue
//+*****************************************************************
static int pull_fd_queue(fd_item_t *item)
{
   if ( fd_queue.tail == fd_queue.head ) {
      return(-1);
   }
   fd_queue.head = (fd_queue.head + 1) % FD_QUEUE_SZ;

   item->ticks = fd_queue.q[fd_queue.head].ticks;
   item->fd    = fd_queue.q[fd_queue.head].fd;
   RTV_DBGLOG(RTVLOG_DSCVR,"************* pull:  %ld:  idx=%d fd=%d\n", times(NULL), fd_queue.head, item->fd);
   return(0);
}

//+*****************************************************************
//  Name: push_fd_queue 
//+*****************************************************************
static void push_fd_queue(int fd)
{
   int tail = (fd_queue.tail + 1) % FD_QUEUE_SZ;

   if ( fd_queue.head == tail ) {
      RTV_WARNLOG("%s: Queue Full\n", __FUNCTION__);
      close(fd);
      return;
   }

   fd_queue.q[tail].ticks = times(NULL);
   fd_queue.q[tail].fd    = fd;
   RTV_DBGLOG(RTVLOG_DSCVR,"************* push: %ld:  idx=%d fd=%d\n", times(NULL), tail, fd);
   fd_queue.tail = tail;   
   return;
}


//+***********************************************************************************************
//  Name: make_dev_descr_resp 
//+***********************************************************************************************
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


//+***********************************************************************************************
//  Name: make_get_guide_resp 
//+***********************************************************************************************
static int make_get_guide_resp( char *buf, int bufsz )
{
   int   hdrlen, sslen;
   char *bptr, *p1, *ss;
   char  bodylenstr[20];
   
   if ( rtv_globals.rtv_emulate_mode == RTV_DEVICE_4K ) {
      RTV_DBGLOG(RTVLOG_DSCVR, "%s:----------Build 4K guide\n", __FUNCTION__);
      sslen = build_v1_bogus_snapshot(&ss);
   }
   else {
      RTV_DBGLOG(RTVLOG_DSCVR, "%s:----------Build 5K guide\n", __FUNCTION__);
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


//+***********************************************************************************************
//  Name: make_volinfo_resp
//+***********************************************************************************************
static int make_volinfo_resp( char *buf, int bufsz )
{
   int   hdrlen, bodylen;

   hdrlen  = snprintf(buf, bufsz-1, HTTP_HEAD_STR); 
   bodylen = snprintf(buf+hdrlen, bufsz - hdrlen, VOLINFO_RESP_STR);
   return(hdrlen+bodylen);
} 


//+***********************************************************************************************
//  Name: make_ls_resp 
//+***********************************************************************************************
static int make_ls_resp( char *buf, int bufsz )
{
   int   hdrlen, bodylen;

   hdrlen  = snprintf(buf, bufsz-1, HTTP_HEAD_STR); 
   bodylen = snprintf(buf+hdrlen, bufsz - hdrlen, LS_RESP_STR);
   return(hdrlen+bodylen);
} 


//+***********************************************************************************************
//  Name: server_open_port 
//+***********************************************************************************************
static int server_open_port(char *host, int port)
{
   const int    on  = 1;

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
   setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
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
		RTV_ERRLOG("%s: listen failed: port=%d %s\n", __FUNCTION__, port, strerror(errno_sav));
      return(-errno_sav);
   }

   return(fd);
}



//+***********************************************************************************************
//  Name:  server_process_connection
//
// If we don't have a device entry for the requesting address then attempt to get the info.
// Only process the http connection request if it is dvarchive. We need to do this to get dvarchive
// into either RTV 4K or 5K mode. We don't want to respond to a real RTV because it will 
// make us show up as a networked RTV.
//
//+***********************************************************************************************
#define X_USERAGENT_STR "X-User-Agent: "
#define X_HOSTADDR_STR "X-Host-Addr: "
static int server_process_connection(int fd, const char *socket_ip_addr_str)
{
   char               eos_seq[4] = {'\r', '\n', '\r', '\n'};
   int                dva_request = 0;

   int                errno_sav, len, new_entry;
   char               rxbuff[TCP_BUF_SZ+1], txbuff[TCP_BUF_SZ+1];
   rtv_device_t      *rtv;
   char              *str_p;
   char               ip_addr_str[INET_ADDRSTRLEN+1];


   RTV_DBGLOG(RTVLOG_DSCVR, "%s: Enter...\n", __FUNCTION__);

   // Read the request.
   // Attempt to pull IP address from XHOSTADDR string.
   // For DVArchive, we need to use the XHOSTADDR IP in the request instead of the socket's IP because 
   // DVArchive running on an aliased IP address sends requests from the primary IP instaed of aliased IP.
   //
   len = 0;
   while ( 1 ) {
      int rxlen;

      if ( (rxlen = read(fd, &(rxbuff[len]), TCP_BUF_SZ-len)) < 0 ) {
         errno_sav = errno;
         RTV_ERRLOG("%s: read failed: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav) );
         return(-errno_sav);
      }
      len += rxlen;
 
      if ( len < 4 ) {
         // Too small. bail.
         //
         RTV_ERRLOG("%s: message to small: sz=%d\n", __FUNCTION__, len);
         return(-EILSEQ);
      }

      // Check for end of message: \r\n\r\n
      //
      if ( strncmp(&rxbuff[len-4],  eos_seq, 4) == 0 ) {
         break;
      }
   }

//   if ( (len = read(fd, rxbuff, TCP_BUF_SZ)) < 0 ) {
//      errno_sav = errno;
//      RTV_ERRLOG("%s: read failed: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav) );
//      return(-errno_sav);
//   }

   rxbuff[len] = '\0';
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: rx=%s\n", __FUNCTION__, rxbuff);

   if ( (str_p = strstr(rxbuff, X_HOSTADDR_STR)) != NULL ) {
      char *p2;

      // DVArchive. get IP address from X+HOSTADDR string.
      //
      str_p += strlen(X_HOSTADDR_STR);
      if ( (p2 = strchr(str_p, '\r')) == NULL ) {
         RTV_ERRLOG("%s: Invalid x-host string format: (%s)\n", __FUNCTION__, rxbuff);
         return(-EILSEQ);
      }

      strncpy(ip_addr_str, str_p, p2-str_p);
      ip_addr_str[p2-str_p] = '\0';
      dva_request = 1;
   }
   else {

      // ReplayTV. Get IP address from socket.
      //
      strcpy(ip_addr_str, socket_ip_addr_str);
   }


  // See if we have the device-info for the box making the request.
  // If not attempt to get it.
  //
   rtv = rtv_get_device_struct(ip_addr_str, &new_entry);
   if ( new_entry ) {
      RTV_DBGLOG(RTVLOG_DSCVR, "%s (%d): ----- attempt rtv_get_device_info(): for IP=%s ------\n\n", __FUNCTION__, __LINE__, ip_addr_str);
      if ( rtv_get_device_info(ip_addr_str, NULL, &rtv) != 0 ) {
         RTV_ERRLOG("%s: Failed rtv_get_device_info() for IP=%s\n", __FUNCTION__, ip_addr_str);
         return(-EILSEQ);
      }

      RTV_DBGLOG(RTVLOG_DSCVR, "%s: rtv_get_device_info() IP=%s: PASSED\n", __FUNCTION__, ip_addr_str);
      if ( atoi(rtv->device.modelNumber) != 4999 ) {
         // Not DVArchive. Go ahead and mark it as discovered.
         // We don't consider DVArchive discovered until it has requested the guide.
         //
         rtv->device.autodiscovered = 1;
         num_rtv_discovered++;
      }
   }
   
   if ( !(dva_request) ) {
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: Dropping Non-DVArchive request: %s\n", __FUNCTION__, ip_addr_str);
      return(-ENOTSUP);
   }

   // Process the http request
   //
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: Processing DVArchive request.\n", __FUNCTION__);

   if ( strstr(rxbuff, "Device_Descr.xml") != NULL ) {
      if ( rtv_globals.rtv_emulate_mode == RTV_DEVICE_4K ) {
         len = make_dev_descr_resp( txbuff, TCP_BUF_SZ, local_ip_address, local_hostname, rtv_idns.sn_4k, rtv_idns.uuid_4k);
      }
      else {
         len = make_dev_descr_resp( txbuff, TCP_BUF_SZ, local_ip_address, local_hostname, rtv_idns.sn_5k, rtv_idns.uuid_5k);
      }
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: ***Respond*** Device_Descr.xml\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
   }
   else if ( strstr(rxbuff, "get_snapshot") != NULL ) {
      len = make_get_guide_resp(txbuff, TCP_BUF_SZ);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: ***Respond*** get_snapshot\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
      if ( atoi(rtv->device.modelNumber) == 4999 ) {
         // DVArchive. Mark it as discovered
         //
         rtv->device.autodiscovered = 1;
         num_rtv_discovered++;         
      }
   }
   else if ( strstr(rxbuff, "httpfs-volinfo") != NULL ) {
      len = make_volinfo_resp(txbuff, TCP_BUF_SZ);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: ***Respond*** httpfs-volinfo\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
   }
   else if ( strstr(rxbuff, "httpfs-ls") != NULL ) {
      len = make_ls_resp(txbuff, TCP_BUF_SZ);
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: ***Respond*** httpfs-ls\n", __FUNCTION__);
      send(fd, txbuff, len, 0);
   }

   //sleep(1);
   //close(fd);
   push_fd_queue(fd);
   return(0);
}


//+***********************************************************************************************
//  Name:  process_ssdp_response
//+***********************************************************************************************
static int process_ssdp_response( const char *ip_addr, char *resp )
{
   int           rc;
   char         *pt1, *pt2;
   char          query_str[255];
   rtv_device_t *rtv;

   // Verify it's a rtv device that responded
   //
   if ( strstr(resp, "ReplayDevice") == NULL ) {
      RTV_WARNLOG("%s: Received ssdp response from non-replaytv device\n", __FUNCTION__);
      RTV_WARNLOG("%s", resp);
      return(-EBADMSG);
   }

   // Extract the query string
   //
   pt1 = strstr(resp, "LOCATION: http://") + 10;
   pt2 = strstr(pt1, "\r");
   if ( (pt1 == NULL) || (pt2 == NULL) || ((pt2 - pt1) > 254) ) {
      RTV_ERRLOG("%s: Malformed ssdp response\n", __FUNCTION__);
      RTV_WARNLOG("%s", resp);
      return(-EBADMSG);
   } 
   memcpy(query_str, pt1, pt2 - pt1);
   query_str[pt2-pt1] = '\0';
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: resp=%s\n", __FUNCTION__, resp);
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: query_str=%s: l=%d\n\n", __FUNCTION__, query_str, strlen(query_str));
   
   // Query the device
   //
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: attempt rtv_get_device_info() IP=%s\n", __FUNCTION__, ip_addr);
   if ( (rc = rtv_get_device_info(ip_addr, query_str, &rtv)) == 0 ) {
      if ( atoi(rtv->device.modelNumber) != 4999 ) {
         // Not DVArchive. Go ahead and mark it as discovered
         // We don't consider DVArchive discovered until it has requested the guide.
         //
         rtv->device.autodiscovered = 1;
         num_rtv_discovered++;         
      }
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: rtv_get_device_info() IP=%s: PASSED\n", __FUNCTION__, ip_addr);
   }
   else {
      RTV_DBGLOG(RTVLOG_DSCVR, "%s: rtv_get_device_info() IP=%s: FAILED\n", __FUNCTION__, ip_addr);
   }
   return(rc);
}


//+***********************************************************************************************
//  Name:  sighandler
//+***********************************************************************************************
static void sighandler(int sig)
{
   sig = sig; //compiler warning
   terminate_discovery_thread = 1;
}

//+***********************************************************************************************
//  Name:  rtv_discovery_thread
//+***********************************************************************************************
static void* rtv_discovery_thread(void *arg)
{
   const        u_char ttl        = 1;
   const        int    on         = 1;
   const struct linger opt_linger = { 0, 2 };
  

   int child_count = 0;

	int                ssdp_sendfd, resp_recvfd, recv_mcastfd, http_listen_fd, cli_fd;
   int                tmp, rc, len, errno_sav;
   socklen_t          addrlen;
	struct sockaddr_in ssdp_addr, resp_addr, local_addr, recv_addr, cliaddr;
   struct ip_mreq     mreq;
   int                maxFDp1;
   fd_set             fds_setup, rfds;
   char               buff[SSDP_BUF_SZ + 1];
   char               tm_buf[255];
   char               ip_addr[INET_ADDRSTRLEN+1];
   char               *uuid_str;
   time_t             tim;
   sigset_t           sigs;
   struct timeval     tv;
   fd_item_t          fd_item;

   arg=arg; //compiler warning

   errno_sav = 0;

	signal(SIGUSR2, sighandler);
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGUSR2);
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);

   ssdp_sendfd = resp_recvfd = recv_mcastfd = http_listen_fd = -1;

   if ( (fd_queue.hz = sysconf(_SC_CLK_TCK)) == -1 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: sysconf(_SC_CLK_TCK) failed: %s\n", __FUNCTION__, strerror(errno_sav));
      goto error;
   }
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: hz=%d\n", __FUNCTION__, fd_queue.hz);

   fd_queue.head = 0;
   fd_queue.tail = 0;   

   // Setup ssdp multicast send socket
   //
   bzero(&ssdp_addr, sizeof(ssdp_addr));
   ssdp_addr.sin_family = AF_INET;
   ssdp_addr.sin_port   = htons(1900);
   inet_aton("239.255.255.250", &(ssdp_addr.sin_addr));
   ssdp_sendfd = socket(AF_INET, SOCK_DGRAM, 0);
   setsockopt(ssdp_sendfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   setsockopt(ssdp_sendfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
   if ( (rc = connect(ssdp_sendfd, (struct sockaddr*)&ssdp_addr, sizeof(ssdp_addr))) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: ssdp_sendfd connect failed: %s\n", __FUNCTION__, strerror(errno_sav));
      goto error;
   }

   // Setup ssdp response receive socket
   //
   len = sizeof(local_addr);
   getsockname(ssdp_sendfd, (struct sockaddr*)&local_addr, &len);
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: local addr : %s \n", __FUNCTION__, inet_ntoa(local_addr.sin_addr));

   bzero(&resp_addr, sizeof(resp_addr));
   resp_addr.sin_family      = AF_INET;
   resp_addr.sin_addr.s_addr = htons(INADDR_ANY);
   resp_addr.sin_port        = local_addr.sin_port;
   
   resp_recvfd = socket(AF_INET, SOCK_DGRAM, 0);
   setsockopt(resp_recvfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   if ( (rc = bind(resp_recvfd, (struct sockaddr*)&resp_addr, sizeof(resp_addr))) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: resp_recvfd bind failed: %s\n", __FUNCTION__, strerror(errno_sav));
      goto error;       
   }
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: resp_recvfd: bind addr %s   bind port %d\n", __FUNCTION__, inet_ntoa(resp_addr.sin_addr), ntohs(resp_addr.sin_port));

   // Setup multicast receive socket
   //
   recv_mcastfd = socket(AF_INET, SOCK_DGRAM, 0);
   setsockopt(recv_mcastfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   memcpy(&mreq.imr_multiaddr, &(ssdp_addr.sin_addr), sizeof(struct in_addr));
   mreq.imr_interface.s_addr = htonl(INADDR_ANY);
   if ( setsockopt(recv_mcastfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: recv_mcastfd IP_ADD_MEMBERSHIP failed: %s\n", __FUNCTION__, strerror(errno_sav));
      goto error;       
   }
   if ( bind(recv_mcastfd, (struct sockaddr*)&ssdp_addr, sizeof(ssdp_addr)) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: recv_mcastfd bind failed: %s\n", __FUNCTION__, strerror(errno_sav));
      goto error;       
   }

   // Setup to receive port 80 http connections
   //
   if ( (http_listen_fd = server_open_port(NULL, 80)) < 0 ) {
      errno_sav = http_listen_fd;
      RTV_ERRLOG("%s: failed to open http server port\n", __FUNCTION__);
      goto error;
   }

   // Send SSDP query
   //
   snprintf(buff, SSDP_BUF_SZ, SSDP_QUERY_STR);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   if ( rc <= 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: SSDP send failed: %s\n", __FUNCTION__, strerror(errno_sav));
      goto error;
   }

   if ( rtv_globals.rtv_emulate_mode == RTV_DEVICE_4K) {
      uuid_str = rtv_idns.uuid_4k;
   }
   else {
      uuid_str = rtv_idns.uuid_5k;
   }
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL1_STR, "byebye", inet_ntoa(local_addr.sin_addr), uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL2_STR, "byebye", inet_ntoa(local_addr.sin_addr), uuid_str, uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL3_STR, "byebye", inet_ntoa(local_addr.sin_addr), uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);

   sleep(1);

   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL1_STR, "alive", inet_ntoa(local_addr.sin_addr), uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL2_STR, "alive", inet_ntoa(local_addr.sin_addr), uuid_str, uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL3_STR, "alive", inet_ntoa(local_addr.sin_addr), uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   
   // Get responses
   //
   FD_ZERO(&fds_setup);
   FD_SET(recv_mcastfd, &fds_setup);
   FD_SET(resp_recvfd, &fds_setup);
   FD_SET(http_listen_fd, &fds_setup);
   tmp     = (recv_mcastfd > resp_recvfd) ? (recv_mcastfd) : (resp_recvfd);
   maxFDp1 = (tmp > http_listen_fd) ? (tmp +1) : (http_listen_fd + 1);

   while ( !(terminate_discovery_thread) ) {
       tv.tv_sec  = 2;
       tv.tv_usec = 0;
       rfds = fds_setup;
       tim  = time(NULL);
       ctime_r(&tim, tm_buf);
       RTV_DBGLOG(RTVLOG_DSCVR, "%s: select_enter: %s\n", __FUNCTION__, tm_buf);
       rc = select(maxFDp1, &rfds, NULL, NULL, &tv);          
       if ( terminate_discovery_thread ) {
          break;
       }

       tim = time(NULL);
       ctime_r(&tim, tm_buf);
       switch (rc) {
       case 0:
          // Timeout
          // Just break & let old fd's get cleaned up
          RTV_DBGLOG(RTVLOG_DSCVR, "%s: select loop timeout: child_cnt=%d\n",__FUNCTION__, child_count);
          break;
       case -1:
          // Error
          errno_sav = errno;
          RTV_ERRLOG("%s: discover select stmt error: %d=>%s\n",__FUNCTION__, errno_sav, strerror(errno_sav));
          goto error;
          break;
       default:
          // Process the FD needing attention
          if ( FD_ISSET(resp_recvfd, &rfds) ) {
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: FDSET: resp_recvfd (SSDP): %s\n", __FUNCTION__, tm_buf);
             len = sizeof(recv_addr);
             rc = recvfrom(resp_recvfd, buff, SSDP_BUF_SZ, 0, (struct sockaddr*)&recv_addr, &len);
             if ( rc < 1 ) {
                errno_sav = errno;
                RTV_ERRLOG("%s: recv resp_recvfd (SSDP): %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav));
                goto error;
             }
             
             inet_ntop(AF_INET, &(recv_addr.sin_addr), ip_addr, INET_ADDRSTRLEN);
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: SSDP recvfrom IP: %s\n", __FUNCTION__, ip_addr);
             buff[rc] = '\0';
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: SSDP rx-msg=%s", __FUNCTION__, buff);
             process_ssdp_response(ip_addr, buff);
          }
          else if ( FD_ISSET(recv_mcastfd, &rfds) ) {
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: FDSET: MCAST_FD: %s\n", __FUNCTION__, tm_buf);
             len = sizeof(recv_addr);
             rc = recvfrom(recv_mcastfd, buff, SSDP_BUF_SZ, 0, (struct sockaddr*)&recv_addr, &len);
             if ( rc < 1 ) {
                errno_sav = errno;
                RTV_ERRLOG("%s: recv MCAST_FD: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav));
                goto error;
             }

             RTV_DBGLOG(RTVLOG_DSCVR, "%s: MCAST_FD: recvfrom IP: %s\n", __FUNCTION__, inet_ntoa(recv_addr.sin_addr));
             buff[rc] = '\0';
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: MCAST-BUF=%s", __FUNCTION__, buff);
          }
          else if ( FD_ISSET(http_listen_fd, &rfds) ) {
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: FDSET: http_listen_fd: %s\n", __FUNCTION__, tm_buf);

             // Accept the connection
             //
             addrlen = sizeof(cliaddr);
             memset(&cliaddr, 0, addrlen);
             if ( (cli_fd = accept(http_listen_fd, (struct sockaddr *)&cliaddr, &addrlen)) < 0) {
                errno_sav = errno;
                RTV_ERRLOG("%s: accept failed: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav) );
                goto error;
             }
             if ( (rc = setsockopt(cli_fd, SOL_SOCKET, SO_LINGER, &opt_linger, sizeof(opt_linger))) ) {
                errno_sav = errno;
                RTV_ERRLOG("%s: accept failed: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav) );
             }

             inet_ntop(AF_INET, &(cliaddr.sin_addr), ip_addr, INET_ADDRSTRLEN);
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: ----- http connection from IP: %s ------\n\n", __FUNCTION__, ip_addr);

             if ( (errno_sav = server_process_connection(cli_fd, ip_addr)) < 0 ) {
                close(cli_fd);
                break;
             } 
          }
          else {
             RTV_ERRLOG("discover select: invalid FD\n");
             goto error;
          }

          break;
       } //switch

       // Close any file descriptors that have been on the queue for more 
       // than 2 seconds.
       //
       while ( get_fd_queue_head(&fd_item) != -1 ) {
          if ( times(NULL) < (fd_item.ticks + (2 * fd_queue.hz)) ) {
             break;
          }
          pull_fd_queue(&fd_item);
          close(fd_item.fd);
       }

   } //while
   
   close(ssdp_sendfd); close(resp_recvfd); close(recv_mcastfd), close(http_listen_fd);
   
   // Close any file descriptors on the queue
   //
   while ( pull_fd_queue(&fd_item) != -1 ) {
      clock_t tm = times(NULL);
      if ( tm < (fd_item.ticks + (2 * fd_queue.hz)) ) {
         tv.tv_sec  = 0;
         tv.tv_usec = ((fd_item.ticks + (2 * fd_queue.hz)) - tm) * (1000000 / fd_queue.hz);
         select(1, NULL, NULL, NULL, &tv);
      }
      close(fd_item.fd);
   }

   fflush(NULL);
   pthread_exit(NULL);
   
error:
   if ( ssdp_sendfd    != -1 ) close (ssdp_sendfd);
   if ( resp_recvfd    != -1 ) close (resp_recvfd);
   if ( recv_mcastfd   != -1 ) close (recv_mcastfd);
   if ( http_listen_fd != -1 ) close (http_listen_fd);
   while( child_count ) {
      wait(&rc);
      child_count--;
   }
   fflush(NULL);
   pthread_exit(NULL);
}


//+***********************************************************************************************
//  Name:  rtv_send_ssdp_byebye
//+***********************************************************************************************
static int rtv_send_ssdp_byebye( void ) 
{
   const     u_char ttl  = 1;
   const int on          = 1;

   int ssdp_sendfd = -1;
   int errno_sav   = 0;

   int rc, len;
   struct sockaddr_in ssdp_addr, local_addr;
   char               buff[SSDP_BUF_SZ + 1];
   char               *uuid_str;

   // Setup ssdp multicast send socket
   //
   bzero(&ssdp_addr, sizeof(ssdp_addr));
   ssdp_addr.sin_family = AF_INET;
   ssdp_addr.sin_port   = htons(1900);
   inet_aton("239.255.255.250", &(ssdp_addr.sin_addr));
   ssdp_sendfd = socket(AF_INET, SOCK_DGRAM, 0);
   setsockopt(ssdp_sendfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   setsockopt(ssdp_sendfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
   if ( (rc = connect(ssdp_sendfd, (struct sockaddr*)&ssdp_addr, sizeof(ssdp_addr))) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("%s: ssdp_sendfd connect failed: %s\n", __FUNCTION__, strerror(errno_sav));
      goto error;
   }

   len = sizeof(local_addr);
   getsockname(ssdp_sendfd, (struct sockaddr*)&local_addr, &len);

   // Send SSDP messages
   //
   if ( rtv_globals.rtv_emulate_mode == RTV_DEVICE_4K) {
      uuid_str = rtv_idns.uuid_4k;
   }
   else {
      uuid_str = rtv_idns.uuid_5k;
   }
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL1_STR, "byebye", inet_ntoa(local_addr.sin_addr), uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL2_STR, "byebye", inet_ntoa(local_addr.sin_addr), uuid_str, uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, SSDP_NOTIFY_HEAD_STR SSDP_NOTIFY_TAIL3_STR, "byebye", inet_ntoa(local_addr.sin_addr), uuid_str);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);

error:
   if ( ssdp_sendfd != -1 ) {
      close (ssdp_sendfd);
   }

   return(-errno_sav);
}

//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************


//+***********************************************************************************************
//  Name:  rtv_discover
//+***********************************************************************************************
int rtv_discover(unsigned int timeout_ms, rtv_device_list_t **device_list)
{
   int            tmo_sec;
   pthread_attr_t thread_attr;

   if ( server_thread_id != 0 ) {
      RTV_ERRLOG("%s: Thread already started\n", __FUNCTION__);
      return(-EALREADY);
   }

   // sleep for 100mS here to make sure any 'non-replaytv' code that 
   // has been using port '80' has time to free it.
   // Specifically the mvpmc www_mvpmc_start() code.
   //
   usleep(100 * 1000);

   if ( timeout_ms == 0 ) {
      // Use the timeout stored in the rtv_globals structure
      //
       timeout_ms = rtv_globals.discover_tmo * 1000;
   }
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: discover_tmo=%u\n", __FUNCTION__, timeout_ms);

   tmo_sec = timeout_ms / 1000;
   if ( timeout_ms % 1000 ) {
      tmo_sec++;
   }
   
   num_rtv_discovered         = 0;
   terminate_discovery_thread = 0;

   // Start the discovery thread
   //
   pthread_attr_init(&thread_attr);
   pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
//   pthread_create(&server_thread_id, &thread_attr, rtv_discovery_thread, NULL);
   pthread_create(&server_thread_id, NULL, rtv_discovery_thread, NULL);

   while ( tmo_sec-- ) {
      sleep(1);
      if ( rtv_globals.max_num_rtv ) {
         // A maximum number of RTV's to discover was specified
         //
         if ( num_rtv_discovered >=  rtv_globals.max_num_rtv ) {
            sleep(1);
            break;
         }
      }
   } 

   *device_list = &rtv_devices;
   return(0);
}


//+***********************************************************************************************
//  Name:  rtv_halt_discovery_server
//+***********************************************************************************************
int rtv_halt_discovery_server(void)
{
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: Enter\n", __FUNCTION__); 
   if ( server_thread_id == 0 ) {
         RTV_DBGLOG(RTVLOG_DSCVR, "%s: Exit: server_thread_id == 0\n", __FUNCTION__); 
      return(0);
   } 
   rtv_send_ssdp_byebye();
   usleep(500 * 1000); //500mS
   pthread_kill(server_thread_id, SIGUSR2);
   pthread_join(server_thread_id, NULL);
   server_thread_id = 0;
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: Exit\n", __FUNCTION__); 
   return(0);
}
