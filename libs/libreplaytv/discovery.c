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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rtv.h"
#include "rtvlib.h"

#define SSDP_BUF_SZ (1023)

int rtv_discover(unsigned int timeout_ms, rtv_device_list_t **device_list)
{

   const char   queryStr[]       = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nST: urn:replaytv-com:device:ReplayDevice:1\r\nMX: 3\r\n\r\n";
   const char   notifyHeadStr[]  = "NOTIFY * HTTP/1.1\r\nDate: Mon, 05 Jan 1970 23:11:55 GMT\r\nServer: Unknown/0.0 UPnP/1.0 Virata-EmWeb/R6_0_1\r\nHOST: 239.255.255.250:1900\r\nNTS: ssdp:alive\r\nCACHE-CONTROL: max-age = 1830\r\nLOCATION: http://%s/Device_Descr.xml\r\n%s";
   const char   notify1TailStr[] = "NT: upnp:rootdevice\r\nUSN: uuid:a74f4677-c352-8b5c-3ee7-26382ab12cae::upnp:rootdevice\r\n\r\n";
   const char   notify2TailStr[] = "NT: uuid:a74f4677-c352-8b5c-3ee7-26382ab12cae\r\nUSN: uuid:a74f4677-c352-8b5c-3ee7-26382ab12cae\r\n\r\n";
   const char   notify3TailStr[] = "NT: urn:replaytv-com:device:ReplayDevice:1\r\nUSN: uuid:a74f4677-c352-8b5c-3ee7-26382ab12cae::urn:replaytv-com:device:ReplayDevice:1\r\n\r\n";
   const u_char ttl              = 1;
   const int    on               = 1;

	int                ssdp_sendfd, resp_recvfd, recv_mcastfd;
   int                rc, len, done, errno_sav;
	struct sockaddr_in ssdp_addr, resp_addr, local_addr, recv_addr;
   struct ip_mreq     mreq;
   int                maxFDp1;
   fd_set             fds_setup, rfds;
	struct timeval     tv;
   char               buff[SSDP_BUF_SZ + 1];
   char               tm_buf[255];
   time_t             tim;

   ssdp_sendfd = resp_recvfd = recv_mcastfd = -1;
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
      RTV_ERRLOG("ssdp_sendfd connect failed: %s\n", strerror(errno_sav));
      goto error;
   }

   // Setup ssdp response receive socket
   //
   len = sizeof(local_addr);
   getsockname(ssdp_sendfd, (struct sockaddr*)&local_addr, &len);
   bzero(&resp_addr, sizeof(resp_addr));
   resp_addr.sin_family      = AF_INET;
   resp_addr.sin_addr.s_addr = htons(INADDR_ANY);
   resp_addr.sin_port        = local_addr.sin_port;
   
   resp_recvfd = socket(AF_INET, SOCK_DGRAM, 0);
   setsockopt(resp_recvfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   if ( (rc = bind(resp_recvfd, (struct sockaddr*)&resp_addr, sizeof(resp_addr))) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("resp_recvfd bind failed: %s\n", strerror(errno_sav));
      goto error;       
   }
   RTV_DBGLOG(RTVLOG_DSCVR, "resp_recvfd: bind addr %s   bind port %d\n", inet_ntoa(resp_addr.sin_addr), ntohs(resp_addr.sin_port));

   // Setup multicast receive socket
   //
   recv_mcastfd = socket(AF_INET, SOCK_DGRAM, 0);
   setsockopt(recv_mcastfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   memcpy(&mreq.imr_multiaddr, &(ssdp_addr.sin_addr), sizeof(struct in_addr));
   mreq.imr_interface.s_addr = htonl(INADDR_ANY);
   if ( setsockopt(recv_mcastfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("recv_mcastfd IP_ADD_MEMBERSHIP failed: %s\n", strerror(errno_sav));
      goto error;       
   }
   if ( bind(recv_mcastfd, (struct sockaddr*)&ssdp_addr, sizeof(ssdp_addr)) != 0 ) {
      errno_sav = errno;
      RTV_ERRLOG("recv_mcastfd bind failed: %s\n", strerror(errno_sav));
      goto error;       
   }

   // Send SSDP query
   //
   rc = send(ssdp_sendfd, queryStr, strlen(queryStr), 0);

#if 0
   snprintf(buff, SSDP_BUF_SZ, notifyHeadStr, inet_ntoa(local_addr.sin_addr), notify1TailStr);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, notifyHeadStr, inet_ntoa(local_addr.sin_addr), notify2TailStr);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
   snprintf(buff, SSDP_BUF_SZ, notifyHeadStr, inet_ntoa(local_addr.sin_addr), notify3TailStr);
   rc = send(ssdp_sendfd, buff, strlen(buff), 0);
#endif
  if ( rc <= 0 ) {
      errno_sav = errno;
      RTV_ERRLOG(" SSDP send failed: %s\n", strerror(errno_sav));
      goto error;
  }

   // Get responses
   //
   FD_ZERO(&fds_setup);
   FD_SET(recv_mcastfd, &fds_setup);
   FD_SET(resp_recvfd, &fds_setup);
   maxFDp1 = (recv_mcastfd > resp_recvfd) ? (recv_mcastfd + 1) : (resp_recvfd + 1);

   done = 0;
   while ( !(done) ) {
       rfds       = fds_setup;
       tv.tv_sec  = timeout_ms / 1000;
       tv.tv_usec = (timeout_ms % 1000) * 1000;

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
          RTV_ERRLOG("discover select stmt error: %d=>%s\n",errno_sav, strerror(errno_sav));
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
                RTV_DBGLOG(RTVLOG_DSCVR, "recvfrom IP: %s\n", inet_ntoa(recv_addr.sin_addr));
                buff[rc] = '\0';
                RTV_DBGLOG(RTVLOG_DSCVR, "%s", buff);
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
          else {
             RTV_ERRLOG("discover select: invalid FD\n");
             goto error;
          }
          break;
       } //switch
   } //while

   close(ssdp_sendfd); close(resp_recvfd); close(recv_mcastfd);
   *device_list = &rtv_devices;
   printf("done.\n");
   return(0);
   
error:
   if ( ssdp_sendfd  != -1 ) close (ssdp_sendfd);
   if ( resp_recvfd  != -1 ) close (resp_recvfd);
   if ( recv_mcastfd != -1 ) close (recv_mcastfd);
   *device_list = NULL;
   return(-errno_sav);
}
