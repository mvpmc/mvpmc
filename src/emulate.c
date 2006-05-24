/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *  
 *   Based on MediaMVP VNC Viewer
 *   http://www.rst38.org.uk/vdr/mediamvp
 *
 *   Copyright (C) 2005, Dominic Morris
 *
 *   $Id: main.c,v 1.1.1.1 2005/02/28 15:19:06 dom Exp $
 *   $Date: 2005/02/28 15:19:06 $
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * 
 *
 */


#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <pwd.h>
#include "zlib.h"


#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>


#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>

#include "mvpmc.h"
#include "emulate.h"
#include "config.h"
#include "mvp_osd.h"

#include <vncviewer.h>
#include <nano-X.h>


void query_host_parameters(void);
void client_play(char *filename);
void client_stop(void);
void client_pause(void);
void client_rewind(void);
void client_forward(void);

Bool media_init(stream_t *stream, char *hostname, int port);
Bool media_send_request(stream_t *stream, char *uri);
Bool media_send_read(stream_t *stream);
Bool media_send_step(stream_t *stream, Bool forward);
Bool media_send_seek(stream_t *stream, int64_t offset);
Bool media_send_stop(stream_t *stream);

int platform_init(void);
int rfb_init(char *hostname, int port);
int udp_listen(const char *iface, int port);
int udp_broadcast(char *data, int len, int port);

void ClearSurface(osd_surface_t *sfc);
Bool direct_init(stream_t *stream, char *hostname, int port);
#define rfbRdcAck 7

#define  programName "mvpmc"

//static char    *c_config_file = NULL;

static char     *c_server_host = NULL;
static int      c_gui_port    = 5906;
static int      c_stream_port = 6337;
static int      c_direct_port = 0;
static char     c_query_host  = 0;
//static char     c_help        = 0;
static char    *c_interface   = "eth0";

static char    *c_addr        = NULL;

stream_t  mystream;

extern Bool shareDesktop;

extern char *passwdFile;
extern int updateRequestX;
extern int updateRequestY;
extern int updateRequestW;
extern int updateRequestH;


static Bool debug = 0;

static osd_surface_t    *surface = NULL;
static osd_surface_t    *surface_blank = NULL;
static osd_surface_t    *surface2 = NULL;

static int surface_x;

void connect_to_servers(void);

int mvp_server_init(void)
{

    c_server_host=vnc_server;

    passwdFile = NULL;
    updateRequestX = 0;
    updateRequestY = 0;
    updateRequestW = 0;
    updateRequestH = 0;
    shareDesktop = False;
    if ( c_query_host == 0 && c_server_host == NULL ) {
        printf("Should either query for connection parameters or specify them\n");
        exit(1);
    }

    surface_x = si.rows;

    if ( (surface = osd_create_surface(720,surface_x) )  == NULL ) {
        printf("Couldn't create surface\n");
    }
    if ( (surface2 = osd_create_surface(720,surface_x) )  == NULL ) {
        printf("Couldn't create surface\n");
    }
    if ( (surface_blank = osd_create_surface(720,surface_x) )  == NULL ) {
        printf("Couldn't create surface\n");
    }

    osd_display_surface(surface);
    
    ClearSurface(surface_blank);

    printf("Created surface %p\n",surface);
  
    connect_to_servers();

    return 0;
}

/* Connect to servers */
void connect_to_servers(void)
{
    int   done = 0;

    /* Close any previously established connections */

    if ( mystream.sock != -1 ) {
        close(mystream.sock);
        mystream.sock = -1;
    }

    if ( mystream.directsock != -1 ) {
        close(mystream.directsock);
        mystream.directsock = -1;
        //mvpav_setfd(-1);
    }
    if ( 1 ) {
        query_host_parameters();
        c_server_host = c_addr;  /* Leak first time if configured... */
    }

    while ( done == 0 ) {
        printf("RFB address %s:%d\n",c_server_host,c_gui_port);
        printf("Media address %s:%d\n",c_server_host,c_stream_port);
        printf("Direct address %s:%d\n",c_server_host,c_direct_port);
        if ( (rfbsock = rfb_init(c_server_host,c_gui_port) ) == -1 ) {
            sleep(1);
            continue;
        }  
//		GrRegisterInput(rfbsock); /* register the RFB socket */
        if ( c_direct_port <= 0 ) {
            if ( media_init(&mystream,c_server_host,c_stream_port) == False ) {
                sleep(1);
                continue;
            }
        } else {
            if ( direct_init(&mystream,c_server_host,c_direct_port) == False ) {
                sleep(1);
                continue;
            }
        }
        done = 1;
    }
}

/** \brief Poll for host parameters and keep going till then
 */
void query_host_parameters(void)
{
    u_int8_t   ether[6];
    u_int8_t   buf[52];
    u_int8_t  *ptr;
    u_int32_t  ipaddr;
    int        found = 0;
    int        s;
    int        listen;
    struct     timeval tv;
    fd_set     fds;

#ifdef linux
    struct ifreq s_ether;
    struct ifreq s_ipaddr;
    struct sockaddr_in *sin;


    printf("Using interface %s\n",c_interface);
    /* Nasty bit of hacking for linux */
    s = socket(PF_INET, SOCK_DGRAM, 0);
    memset(&s_ether, 0x00, sizeof(s_ether));
    memset(&s_ipaddr, 0x00, sizeof(s_ipaddr));
    strcpy(s_ether.ifr_name, c_interface);
    strcpy(s_ipaddr.ifr_name, c_interface);
    ioctl(s, SIOCGIFHWADDR, &s_ether);
    ioctl(s, SIOCGIFADDR, &s_ipaddr);
    memcpy(ether,s_ether.ifr_hwaddr.sa_data,6);
    sin = (struct sockaddr_in *)&s_ipaddr.ifr_addr;
    memcpy(&ipaddr,&sin->sin_addr,4);
    ipaddr = ntohl(ipaddr);
    close(s);
#endif

    listen = udp_listen(NULL,16882);

    while ( !found ) {

        memset(buf,0,sizeof(buf));
        ptr = buf;
        INT32_TO_BUF(1,ptr);
        INT16_TO_BUF(0xbabe,ptr);
        INT16_TO_BUF(0xfafe,ptr);
        memcpy(ptr,ether,6);
        ptr += 6;
        INT16_TO_BUF(0,ptr);
        INT32_TO_BUF(ipaddr,ptr);
        INT16_TO_BUF(16882,ptr);

        printf("Querying host parameters\n");
        if ( udp_broadcast(buf,52,16881) == -1 ) {
            perror("broadcast\n");
            exit(1);
        }

        FD_ZERO(&fds);
        FD_SET(listen,&fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(FD_SETSIZE,&fds,NULL,NULL,&tv) < 0 ) {
            perror("select");
            continue;
        }

        if ( FD_ISSET(listen,&fds) ) {
            if  ( ReadExact(listen,buf,52) ) {
                char        addr[32];

                u_int16_t   val16;
                u_int32_t   val32;

                ptr = buf + 4;
                BUF_TO_INT16(val16,ptr);
                if ( val16 != 0xfafe ) {
                    continue;
                }
                BUF_TO_INT16(val16,ptr);
                if ( val16 != 0xbabe ) {
                    continue;
                }
                ptr = buf + 24;
                BUF_TO_INT32(val32,ptr);

                BUF_TO_INT16(c_gui_port,ptr);
                ptr += 6;
                BUF_TO_INT16(c_stream_port,ptr);
                ptr += 4;
                BUF_TO_INT16(c_direct_port,ptr);

                snprintf(addr,sizeof(addr),"%d.%d.%d.%d",
                         ( val32 >> 24 ) & 0xFF,
                         ( val32 >> 16 ) & 0xFF,
                         ( val32 >> 8 ) & 0xFF,
                         val32 & 0xFF);
                
                FREENULL(c_addr);
                c_addr = STRDUP(addr);
                return;

            }
        }
    }
}
void mvp_server_delete(void) 
{
//    if ( mystream.rfbsock != -1 ) {
//        close(mystream.rfbsock);
//        mystream.rfbsock = -1;

//    }
    if ( mystream.sock != -1 ) {
        close(mystream.sock);
        mystream.sock = -1;
    }

    if ( mystream.directsock != -1 ) {
        close(mystream.directsock);
        mystream.directsock = -1;
        //mvpav_setfd(-1);
    }

    osd_destroy_surface(surface2);
    surface2 = NULL;
    osd_destroy_surface(surface_blank);
    surface_blank = NULL;
    osd_destroy_surface(surface);
    surface = NULL;
}


int udp_listen(const char *iface, int port)
{
    int                 sock;
    struct sockaddr_in  serv_addr;
    int                 trueval = 1;


    memset((char *)&serv_addr, 0, sizeof(serv_addr));

    if (iface == NULL) {
        serv_addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_aton(iface, &serv_addr.sin_addr) == 0) {
        return -1;
    }


    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == -1) {
        return -1;
    }


    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&trueval, sizeof(trueval) ) == -1 ) {
        close(sock);
        return -1;
    }


    serv_addr.sin_port = htons((u_short)port);
    serv_addr.sin_family = AF_INET;

    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1 ) {
        close(sock);
        return -1;
    }

    return sock;

}


int udp_broadcast(char *data, int len, int port)
{
    int                 n;
    int                 sock;
    struct sockaddr_in  serv_addr;
    int                 on = 1;

    memset((char *)&serv_addr, 0, sizeof(serv_addr));


    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == - 1) {
        return -1;
    }

    if(setsockopt (sock, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof(on))< 0) {
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((u_short)port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    n = sendto(sock, data, len, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    close(sock);

    return n;
}




/*
 * sockets.c - functions to deal with sockets.
 */


/* fix bad MIPS sys headers...*/
#ifndef SOCK_STREAM
#define SOCK_STREAM	2	/* <asm/socket.h>*/
#endif

void PrintInHex(char *buf, int len);

Bool errorMessageFromReadExact = True;

/*
 * Read an exact number of bytes, and don't return until you've got them.
 */

Bool
ReadExact(int sock, char *buf, int n)
{
    int i = 0;
    int j;

    while (i < n) {
        j = read(sock, buf + i, (n - i));
        if (j <= 0) {
            if (j < 0) {
                fprintf(stderr,programName);
                perror(": read");
            } else {
                if (errorMessageFromReadExact) {
                    fprintf(stderr,"%s: read failed\n",programName);
                }
            }
            return False;
        }
        i += j;
    }
    if (debug && n < 100)
        PrintInHex(buf,n);
    return True;
}


/*
 * Write an exact number of bytes, and don't return until you've sent them.
 */

Bool
WriteExact(int sock, char *buf, int n)
{
    int i = 0;
    int j;

    while (i < n) {
	j = write(sock, buf + i, (n - i));
	if (j <= 0) {
	    if (j < 0) {
		fprintf(stderr,programName);
		perror(": write");
	    } else {
		fprintf(stderr,"%s: write failed\n",programName);
	    }
	    return False;
	}
	i += j;
    }
    return True;
}


/*
 * ConnectToTcpAddr connects to the given TCP port.
 */

int
ConnectToTcpAddr(unsigned int host, int port)
{
    int sock;
    struct sockaddr_in addr;
    int one = 1;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = host;


    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	    fprintf(stderr,programName);
	    perror(": ConnectToTcpAddr: socket");
	    return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    fprintf(stderr,programName);
	    perror(": ConnectToTcpAddr: connect");
	    close(sock);
	    return -1;
    }

    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,(char *)&one, sizeof(one)) < 0) {
	    fprintf(stderr,programName);
	    perror(": ConnectToTcpAddr: setsockopt");
	    close(sock);
	    return -1;
    }
    return sock;
}



/*
 * ListenAtTcpPort starts listening at the given TCP port.
 */

int
ListenAtTcpPort(int port)
{
    int sock;
    struct sockaddr_in addr;
    int one = 1;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: socket");
	return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		   (const char *)&one, sizeof(one)) < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: setsockopt");
	close(sock);
	return -1;
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: bind");
	close(sock);
	return -1;
    }

    if (listen(sock, 5) < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: listen");
	close(sock);
	return -1;
    }

    return sock;
}


/*
 * AcceptTcpConnection accepts a TCP connection.
 */

int
AcceptTcpConnection(int listenSock)
{
    int sock;
    struct sockaddr_in addr;
    unsigned int addrlen = sizeof(addr);
    int one = 1;

    sock = accept(listenSock, (struct sockaddr *) &addr, &addrlen);
    if (sock < 0) {
	fprintf(stderr,programName);
	perror(": AcceptTcpConnection: accept");
	return -1;
    }

    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
		   (char *)&one, sizeof(one)) < 0) {
	fprintf(stderr,programName);
	perror(": AcceptTcpConnection: setsockopt");
	close(sock);
	return -1;
    }

    return sock;
}


/*
 * StringToIPAddr - convert a host string to an IP address.
 */

int
StringToIPAddr(const char *str, unsigned int *addr)
{
    struct hostent *hp;

    if ((*addr = inet_addr(str)) == -1)
    {
	if (!(hp = gethostbyname(str)))
	    return 0;

	*addr = *(unsigned int *)hp->h_addr;
    }

    return 1;
}


/*
 * Test if the other end of a socket is on the same machine.
 */

Bool
SameMachine(int sock)
{
    struct sockaddr_in peeraddr, myaddr;
    unsigned int addrlen = sizeof(struct sockaddr_in);

    getpeername(sock, (struct sockaddr *)&peeraddr, &addrlen);
    getsockname(sock, (struct sockaddr *)&myaddr, &addrlen);

    return (peeraddr.sin_addr.s_addr == myaddr.sin_addr.s_addr);
}


/*
 * Print out the contents of a packet for debugging.
 */

void
PrintInHex(char *buf, int len)
{
    int i, j;
    char c, str[17];

    str[16] = 0;

    fprintf(stderr,"ReadExact: ");

    for (i = 0; i < len; i++)
    {
	if ((i % 16 == 0) && (i != 0)) {
	    fprintf(stderr,"           ");
	}
	c = buf[i];
	str[i % 16] = (((c > 31) && (c < 127)) ? c : '.');
	fprintf(stderr,"%02x ",(unsigned char)c);
	if ((i % 4) == 3)
	    fprintf(stderr," ");
	if ((i % 16) == 15)
	{
	    fprintf(stderr,"%s\n",str);
	}
    }
    if ((i % 16) != 0)
    {
	for (j = i % 16; j < 16; j++)
	{
	    fprintf(stderr,"   ");
	    if ((j % 4) == 3) fprintf(stderr," ");
	}
	str[i % 16] = 0;
	fprintf(stderr,"%s\n",str);
    }

    fflush(stderr);
}

/*
 * vncviewer.c - viewer for MVP - based on nano-X version
 *
 * The rfb_viewer() is called from a separate thread
 */


#ifdef NANOX
static void HandleEvents(GR_EVENT *ev);
#endif

/** \brief Initialise a connection to the vnc server
 *
 *  \param hostname Hostname
 *  \param port Pot 
 *
 *  \retval -1 - Connection failed
 *  \retval other - socket
 */
int rfb_init(char *hostname, int port)
{
    
    if ( !ConnectToRFBServer(hostname, port)) {
        return -1;
    } 

    printf("Socket %d\n",rfbsock);
    if (!InitialiseRFBConnection(rfbsock)) {
        close(rfbsock);
        return -1;
    }

    if (!SetFormatAndEncodings()) {
        close(rfbsock);
        return -1;
    }
    updateRequestH = si.rows;
    if (!SendFramebufferUpdateRequest(updateRequestX, updateRequestY,
                                      updateRequestW, updateRequestH, False)) {
        close(rfbsock);
        return -1;
    }
    printf("End rfb_init\n");
    
    return rfbsock;
}

#if 0
    while (True) {
        if (!HandleKeyBoard() ) {
            ShutdownDisplay();
            return;
        }
        tvp = NULL;

        if (sendUpdateRequest) {
            gettimeofday(&tv, NULL);

            msWait = (updateRequestPeriodms +
                      ((updateRequestTime.tv_sec - tv.tv_sec) * 1000) +
                      ((updateRequestTime.tv_usec - tv.tv_usec) / 1000));

            if (msWait > 0) {
                tv.tv_sec = msWait / 1000;
                tv.tv_usec = (msWait % 1000) * 1000;

                tvp = &tv;
            } else {
                if (!SendIncrementalFramebufferUpdateRequest()) {
                    ShutdownDisplay();
                    return;
                }
            }
        }

        FD_ZERO(&fds);
        FD_SET(rfbsock,&fds);

        if (select(FD_SETSIZE, &fds, NULL, NULL, tvp) < 0) {
            perror("select");
            ShutdownDisplay();
            exit(1);
        }

        if (FD_ISSET(rfbsock, &fds)) {
            printf("Fah\n");
            if (!HandleRFBServerMessage()) {
                return;
            }
        }
    }

    return;
}
#endif


/*
 *
 *   Handle the Hauppauge extensions to the RFB protocol
 */



Bool RDCSendStop(int sock)
{
    char    buf[34];

    memset(buf,0,sizeof(buf));

    buf[0] = rfbRdcAck;
    buf[1] = RDC_STOP;

    if (!WriteExact(sock,buf,sizeof(buf)) ) {
        return False;
    }
    return True;
}


Bool HandleRDCMessage(int sock)
{
    char   buf[64];

    if ( !ReadExact(sock,buf + 1,33) ) {
        return False;
    }

    buf[0] = rfbRdcAck;

    printf("Received RDC command %d\n",buf[1]);

    switch ( buf[1] ) {
    case RDC_PLAY:
    {
        int   length = buf[8];   
        char *filename;

        filename = malloc(length + 1);

        if (!ReadExact(sock,filename,length) ) {
            return False;
        }
        filename[length] = 0;

        printf("We need to play %s\n",filename);

        client_play(filename);

        free(filename);
        break;
    }
    case RDC_PAUSE:
    {
        client_pause();
        break;
    }

    case RDC_STOP:
    {
        client_stop();
        break;
    }
    case RDC_REWIND:
    {
        client_rewind();
        break;
    }
    case RDC_FORWARD:
    {
        client_forward();
        break;
    }
    case RDC_VOLUP:
    {
        break;
    }
    case RDC_VOLDOWN:
    {
        break;
    }
    case RDC_MENU:
    {
        char display[2];
        if ( !ReadExact(sock,display,2) ) {
            return False;
        }
        printf("Display is state %d\n",!display[0]);
        //SetDisplayState(!display[0]);
        
        return True;
    }
    case RDC_MUTE:
    {
        break;
    }
    case RDC_SETTINGS:
    {
        char   settings[20];

        printf("Buf length %d\n",buf[7]);
        if ( !ReadExact(sock,settings,buf[7]) ) {
            return False;
        }
        printf("Settings command %d\n",settings[0]);
        if ( settings[0] == RDC_SETTINGS_GET ) {
            memcpy(&buf[34],settings,sizeof(settings));
            if ( !WriteExact(sock,buf,34 + buf[7]) ) {
                return False;
            }
        }
        return True;
        break;
    }
    

    }

    if ( !WriteExact(sock,buf,34) ) {
        return False;
    }

    return True;
}


/*
client.c
 */


void client_play(char *filename)
{
    media_send_request(&mystream,filename);

}

void client_stop(void)
{
    media_send_stop(&mystream);
}

void client_pause(void)
{

}

void client_rewind(void)
{

}

void client_forward(void)
{


}


/*
 *   stream.c
 *   Implementation of Hauppauge Streaming protocol
 */

// static pthread_t     media_thread = 0;
// static void         *media_read_start(void *);


Bool media_init(stream_t *stream, char *hostname, int port)
{
    unsigned int host;

    printf("%s %d\n",hostname,port);

    if (!StringToIPAddr(hostname, &host)) {
        fprintf(stderr,"%s: couldn't convert '%s' to host address\n",
                programName,hostname);
        return -1;
    }

    stream->sock = ConnectToTcpAddr(host, port);


    return stream->sock != -1 ? True : False;
}


Bool media_send_request(stream_t *stream, char *uri)
{
    u_int8_t *buf;
    u_int16_t len = strlen(uri);
    u_int8_t *ptr;


    printf("Requesting %s\n",uri);

    buf = CALLOC(40 + len + 1, sizeof(char));

    buf[0] = MEDIA_REQUEST;

    ptr = buf + 36;
    INT16_TO_BUF(len + 1,ptr);
    strcpy(buf+40,uri);

    if (!WriteExact(stream->sock,buf,len + 40 + 1) ) {
        FREE(buf);
        return False;
    }
    FREE(buf);

    stream->last_command = MEDIA_REQUEST;

    return True;
}

Bool media_send_read(stream_t *stream)
{
    u_int8_t    buf[40];
    u_int8_t   *ptr;


    memset(buf,0,sizeof(buf));
    
    buf[0] = MEDIA_BLOCK;

    ptr = buf + 8;
    INT32_TO_BUF(stream->blocklen,ptr);

    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_BLOCK;

    return True;
}



Bool media_send_step(stream_t *stream, Bool forward)
{
    u_int8_t    buf[40];

    memset(buf,0,sizeof(buf));

    buf[0] = MEDIA_STEP;
    buf[9] = forward ? 1 : 0;

    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_STEP;

    return True;
}


Bool media_send_seek(stream_t *stream, int64_t offset)
{
    u_int8_t   buf[40];
    u_int8_t  *ptr;

    memset(buf,0,sizeof(buf));
    buf[0] = MEDIA_SEEK;

    ptr = buf + 8;
    INT32_TO_BUF(offset,ptr);

    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_SEEK;

    return True;
}


Bool media_send_stop(stream_t *stream)
{
    u_int8_t  buf[40];

    memset(buf,0,sizeof(buf));
    buf[0] = MEDIA_STOP;

    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_STOP;

    return True;
}


Bool media_read_message(stream_t *stream)
{
    u_int8_t   buf[40];
    u_int8_t  *ptr;

    if (!ReadExact(stream->sock,buf,40) ) {
        return False;
    }

    switch ( buf[0] ) {
    case MEDIA_REQUEST:
    {
        if ( buf[4] == 0 ) {
            printf("Aborted reading\n");
        } else {
            stream->mediatype = buf[4];
            if ( buf[4] == TYPE_VIDEO ) {
                stream->blocklen = 200000;
            } else {
                stream->blocklen = 20000;
            }
            ptr = buf + 16;
            BUF_TO_INT32(stream->length,ptr);
            
            switch ( buf[4] ) {
            case 0x01:           /* MPEG */
                //mvpav_play(0);
            case 0x02:           /* MP3 */
                //mvpav_play(1);
                break;
            }
            media_send_seek(stream,0);        
        }
        break;
    }
    case MEDIA_BLOCK:
    {
        int32_t    blocklen;
//        u_int8_t *block;

        ptr = buf + 8;

        BUF_TO_INT32(blocklen,ptr);

        if ( blocklen != 0 ) {
//            int  n = 0;
            stream->inbuf = MALLOC(blocklen);

            if (!ReadExact(stream->sock,stream->inbuf,blocklen) ) {
                return False;
            }
            stream->inbuflen = blocklen;
            stream->inbufpos = 0;
            //media_queue_data(stream);

        } else {
            RDCSendStop(rfbsock);
        }
        break;
    }

    case MEDIA_STEP:
    case MEDIA_SEEK:
    {
        media_send_read(stream);
        break;
    }
    case MEDIA_STOP:
        //mvpav_stop();
        break;
    }
    return True;
}

void media_queue_data(stream_t *stream)
{
    int      n;

    n = write(stream->sock,stream->inbuf + stream->inbufpos, stream->inbuflen - stream->inbufpos);
    if ( n > 0 ) {
        stream->inbufpos += n;
        if ( stream->inbufpos == stream->inbuflen ) {
            media_send_read(stream);
            stream->inbufpos = 0;
            stream->inbuflen = 0;
            FREENULL(stream->inbuf);
        }
    }

    return ;
}


/*
*   mvp.c
*   Native MVP viewer
*/


static int               visible = 1;



void RectangleUpdateYUV(int x0, int y0, int w, int h,  unsigned char *buf)
{
    int   x,y;
    unsigned char   y1,y2,u,v;
    unsigned char *ptr = buf;

    // printf("YUV update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
        for ( x = x0; x < x0 +w ; x+= 2 ) {           
            y1 = *ptr++;
            u = *ptr++;
            v = *ptr++;
            y2 = *ptr++;
            osd_draw_pixel_ayuv(surface2,x,y,255,y1,u,v);
            osd_draw_pixel_ayuv(surface2,x+1,y,255,y2,u,v);
        }
    }
}

void RectangleUpdateYUV2(int x0, int y0, int w, int h,  unsigned char *buf1, unsigned char *buf2)
{
    int   x,y;
    unsigned char   y1,y2,u,v;
    unsigned char *ptr1 = buf1;
    unsigned char *ptr2 = buf2;

    // printf("YUV2 update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
        for ( x = x0; x < x0 +w ; x+= 2 ) {           
            y1 = *ptr1++;
            y2 = *ptr1++;
            u = *ptr2++;
            v = *ptr2++;
            osd_draw_pixel_ayuv(surface2,x,y,255,y1,u,v);
            osd_draw_pixel_ayuv(surface2,x+1,y,255,y2,u,v);
        }
    }
}


void RectangleUpdateAYVU(int x0, int y0, int w, int h,  unsigned char *buf)
{
    int   x,y;
    unsigned char   a1,a2,y1,y2,u,v;
    unsigned char *ptr = buf;

    // printf("AYUV update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
        for ( x = x0; x < x0 +w ; x+= 2 ) {
            a1 = *ptr++;
            a2 = *ptr++;
            y1 = *ptr++;
            u = *ptr++;
            v = *ptr++;
            y2 = *ptr++;
            osd_draw_pixel_ayuv(surface2,x,y,255-a1,y1,u,v);
            osd_draw_pixel_ayuv(surface2,x+1,y,255-a2,y2,u,v);
        }
    }
}

void RectangleUpdateARGB(int x0, int y0, int w, int h,  unsigned char *buf)
{
    int   x,y;
    unsigned char   a,r,g,b;
    unsigned char *ptr = buf;

    // printf("ARGB update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
#if 1
        for ( x = x0; x < x0 +w ; x++ ) {
            a = *ptr++;
            r = *ptr++;
            g = *ptr++;
            b = *ptr++;   
            a = 0xff - a;
            osd_draw_pixel(surface2,x,y,osd_rgba(r,g,b,a));
        }
#endif 
    }
}

void UpdateFinished()
{
    if ( visible ) {
        osd_blit(surface,0,0,surface2,0,0,720,surface_x);
    } else {
        osd_blit(surface,0,0,surface_blank,0,0,720,surface_x);
    }
}

/** \brief Set the display to be visible/not
 */
void SetDisplayState(int state)
{
    visible = state;

    if ( visible ) {
        osd_blit(surface,0,0,surface2,0,0,720,surface_x);
    } else {
        osd_blit(surface,0,0,surface_blank,0,0,720,surface_x);
    }
}

void ClearSurface(osd_surface_t *sfc)
{
    int      x,y;

    for ( x = 0; x < 720; x++ ) {
        for ( y = 0; y < surface_x; y++ ) {
            osd_draw_pixel(sfc,x,y,0x00000000);
        }
    }
}


/*
 *   $Id: direct.c,v 1.1.1.1 2005/02/28 15:19:06 dom Exp $
 *   Implementation of a direct socket connection
 */

Bool direct_init(stream_t *stream, char *hostname, int port)
{
    unsigned int host;

    if (!StringToIPAddr(hostname, &host)) {
        fprintf(stderr,"%s: couldn't convert '%s' to host address\n",
                programName,hostname);
        return False;
    }

    stream->directsock = ConnectToTcpAddr(host, port);

    //mvpav_setfd(stream->directsock);

    return stream->directsock != -1 ? True : False;
}



