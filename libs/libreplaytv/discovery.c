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
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rtv.h"
#include "rtvlib.h"
#include "rtvserver.h"

#define SSDP_BUF_SZ (1023)

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
   RTV_DBGLOG(RTVLOG_DSCVR, "%s: query_str: %s: l=%d\n\n", __FUNCTION__, query_str, strlen(query_str));
   
   // Query the device
   //
   if ( (rc = rtv_get_device_info(ip_addr, query_str, &rtv)) == 0 ) {
      rtv->device.autodiscovered = 1;
   }
   return(rc);
}

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


//+***********************************************************************************
//                         PUBLIC FUNCTIONS
//+***********************************************************************************

int rtv_discover(unsigned int timeout_ms, rtv_device_list_t **device_list)
{
   const u_char ttl              = 1;
   const int    on               = 1;

   int child_count = 0;

	int                ssdp_sendfd, resp_recvfd, recv_mcastfd, http_listen_fd, cli_fd;
   int                tmp, rc, len, done, errno_sav, pid, new_entry;
   socklen_t          addrlen;
	struct sockaddr_in ssdp_addr, resp_addr, local_addr, recv_addr, cliaddr;
   struct ip_mreq     mreq;
   int                maxFDp1;
   fd_set             fds_setup, rfds;
	struct timeval     tv;
   char               buff[SSDP_BUF_SZ + 1];
   char               tm_buf[255];
   char               ip_addr[INET_ADDRSTRLEN+1];
   char               *uuid_str;
   time_t             tim;
   rtv_device_t      *rtv;


   ssdp_sendfd = resp_recvfd = recv_mcastfd = http_listen_fd = -1;
   errno_sav = 0;

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
   tv.tv_sec  = timeout_ms / 1000;
   tv.tv_usec = (timeout_ms % 1000) * 1000;
   done = 0;
   while ( !(done) ) {
       rfds = fds_setup;
       rc   = select(maxFDp1, &rfds, NULL, NULL, &tv);          
       tim  = time(NULL);
       ctime_r(&tim, tm_buf);

       switch (rc) {
       case 0:
          // Timeout
          done = 1;
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
             RTV_DBGLOG(RTVLOG_DSCVR, "FSSET: resp_recvfd: %s\n", tm_buf);
             len = sizeof(recv_addr);
             rc = recvfrom(resp_recvfd, buff, SSDP_BUF_SZ, 0, (struct sockaddr*)&recv_addr, &len);
             if ( rc < 1 ) {
                errno_sav = errno;
                RTV_ERRLOG("recv resp_recvfd: %d=>%s\n", errno_sav, strerror(errno_sav));
                goto error;
             }
             else {
                inet_ntop(AF_INET, &(recv_addr.sin_addr), ip_addr, INET_ADDRSTRLEN);
                RTV_DBGLOG(RTVLOG_DSCVR, "recvfrom IP: %s\n", ip_addr);
                buff[rc] = '\0';
                RTV_DBGLOG(RTVLOG_DSCVR, "%s", buff);
                process_ssdp_response(ip_addr, buff);
             }
          }
          else if ( FD_ISSET(recv_mcastfd, &rfds) ) {
             RTV_DBGLOG(RTVLOG_DSCVR, "FSSET: recv_mcastfd: %s\n", tm_buf);
             len = sizeof(recv_addr);
             rc = recvfrom(recv_mcastfd, buff, SSDP_BUF_SZ, 0, (struct sockaddr*)&recv_addr, &len);
             if ( rc < 1 ) {
                errno_sav = errno;
                RTV_ERRLOG("recv recv_mcastfd: %d=>%s\n", errno_sav, strerror(errno_sav));
                goto error;
             }
             else {
                RTV_DBGLOG(RTVLOG_DSCVR, "recvfrom IP: %s\n", inet_ntoa(recv_addr.sin_addr));
                buff[rc] = '\0';
                RTV_DBGLOG(RTVLOG_DSCVR, "%s", buff);
             }
          }
          else if ( FD_ISSET(http_listen_fd, &rfds) ) {
             RTV_DBGLOG(RTVLOG_DSCVR, "FSSET: http_listen_fd: %s\n", tm_buf);

             // Accept the connection
             //
             addrlen = sizeof(cliaddr);
             memset(&cliaddr, 0, addrlen);
             if ( (cli_fd = accept(http_listen_fd, (struct sockaddr *)&cliaddr, &addrlen)) < 0) {
                errno_sav = errno;
                RTV_ERRLOG("%s: accept failed: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav) );
                goto error;
             }
             inet_ntop(AF_INET, &(cliaddr.sin_addr), ip_addr, INET_ADDRSTRLEN);
             RTV_DBGLOG(RTVLOG_DSCVR, "%s: ----- http connection from IP: %s ------\n\n", __FUNCTION__, ip_addr);

             // If we don't have a device entry for this address then attempt to get the info.
             // Only process the connection if it is dvarchive. We need to do this to get dvarchive
             // into either RTV 4K or 5K mode. We don't want to respond to a real RTV because it will 
             // make us show up as a networked RTV.
             //
             rtv = rtv_get_device_struct(ip_addr, &new_entry);
             if ( new_entry ) {
                if ( (rc = rtv_get_device_info(ip_addr, NULL, &rtv)) != 0 ) {
                   // Just close the fd and don't worry about it.
                   //
                   close(cli_fd);
                }
                else {
                   // Should have passed with new_entry=0
                   //
                   rtv->device.autodiscovered = 1;
                   rtv = rtv_get_device_struct(ip_addr, &new_entry);
                }
             }
             
             if ( !(new_entry) ) {
                if ( atoi(rtv->device.modelNumber) == 4999 ) {
                   // Is dvarchive: Fork child process to handle the connection
                   //
                   if ( (pid = fork()) < 0) {
                      errno_sav = errno;
                      RTV_ERRLOG("%s: fork error: %d=>%s\n", __FUNCTION__, errno_sav, strerror(errno_sav));
                      goto error;
                   }
                   if (pid ==  0) {
                      // child
                      server_process_connection(cli_fd);
                      // child never returns
                   }
                   else {
                      // parent
                      close(cli_fd);
                      child_count++;
                   }
                } 
                else {
                   // Not dvarchive
                   //
                   close(cli_fd);
                }
             } //new_entry
          }
          else {
             RTV_ERRLOG("discover select: invalid FD\n");
             goto error;
          }
          break;
       } //switch
   } //while

   close(ssdp_sendfd); close(resp_recvfd); close(recv_mcastfd), close(http_listen_fd);
   while( child_count ) {
      wait(&rc);
      child_count--;
   }
   *device_list = &rtv_devices;
   return(0);
   
error:
   if ( ssdp_sendfd    != -1 ) close (ssdp_sendfd);
   if ( resp_recvfd    != -1 ) close (resp_recvfd);
   if ( recv_mcastfd   != -1 ) close (recv_mcastfd);
   if ( http_listen_fd != -1 ) close (http_listen_fd);
   while( child_count ) {
      wait(&rc);
      child_count--;
   }
   *device_list = NULL;
   return(-errno_sav);
}
