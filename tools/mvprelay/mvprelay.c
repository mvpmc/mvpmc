/*  
 *  MediaMVP service redirector
 *
 *  Used for crossing subnets
 *
 *  $Id: mvprelay.c,v 1.1 2005/05/02 22:06:04 dom Exp $
 *
 *  gcc -o mvprelay mvprelay.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/* Some handy (de-) serialisation macros */
#define INT16_TO_BUF(src,dest) \
(dest)[0] = (((src) >> 8) & 0xff); \
(dest)[1] = ((src) & 0xff); \
(dest) += 2;

#define INT32_TO_BUF(src,dest) \
(dest)[0] = (((src) >> 24) & 0xff); \
(dest)[1] = (((src) >> 16) & 0xff); \
(dest)[2] = (((src) >> 8) & 0xff); \
(dest)[3] = ((src) & 0xff); \
(dest) += 4;

#define BUF_TO_INT16(dest,src) \
(dest) = (((unsigned char)(src)[0] << 8) | (unsigned char)(src)[1]); \
(src) += 2;

#define BUF_TO_INT32(dest,src) \
(dest) = ( ((unsigned char)(src)[0] << 24) | ((unsigned char)(src)[1] << 16) |((unsigned char)(src)[2] << 8) | (unsigned char)(src)[3]); \
(src) += 4;

typedef struct {
    uint32_t  sequence;
    uint32_t  id1;
    uint32_t  id2;
    uint8_t   mac[6];
    uint8_t   pad[2];
    uint32_t  client_addr;
    uint16_t  client_port;
    uint8_t   pad2[2];
    uint32_t  guiserv_addr;
    uint16_t  guiserv_port;
    uint8_t   pad3[2];
    uint32_t  conserv_addr;
    uint16_t  conserv_port;
    uint8_t   pad4[6];
    uint32_t  serv_addr;
    uint16_t  serv_port;
} udpprot_t;


static int udp_listen(const char *iface, int port);
static int udp_send(char *data, int len, const char *addr, int port);
static void parse_udp(udpprot_t *prot,unsigned char *buf, int len);
static void serialise_udp(udpprot_t *prot, unsigned char *buf, int len);



static char data[2000];


static int c_gui_port = 5906;
static int c_stream_port = 6337;
static uint32_t   c_vdr_host = 0x7f000001;


int
main(int argc, char **argv) {
    int s, len, port;
    udpprot_t      prot;
    int            destport;
    char          *desthost;
    uint32_t       desthostip;
    struct in_addr in;


    if (argc != 5) {
        fprintf(stderr, "usage: %s udpport guiport streamport hostname\n",argv[0]);
        return 0;
    }

    // Become Daemon.  Return pid of child.
#if 0
    if ((i = fork()))
        return i;
#endif

    // Save the port number in network byte order
    port = atoi(argv[1]);
    c_gui_port = atoi(argv[2]);
    c_stream_port = atoi(argv[3]);
    c_vdr_host = htonl(inet_addr(argv[4]));

    // Create Socket and listen to ALL ethernet traffic.
    if ((s = udp_listen(NULL,port) ) < 0)
        return fprintf(stderr, "%s: Error creating socket\n", *argv);

    // Main loop
    while ((len = recvfrom(s, &data, sizeof(data), 0, NULL, 0)) > 0) {

        // Ignore packets that are not UDP Broadcasts on our given port

        if ( len == 52 ) {
            parse_udp(&prot,(unsigned char*)data,len);

            if ( prot.id1 != 0xbabe || prot.id2 != 0xfafe ) {
                printf("id1 = %04x id2=%04x\n",prot.id1,prot.id2);
                continue;
            }

            prot.id1 = 0xfafe;
            prot.id2 = 0xbabe;
            memset(&prot.mac,0,8);
            destport = prot.client_port;
            prot.client_port = 2048;

            /* FIXME Here to support multiple clients */
            prot.guiserv_addr = c_vdr_host;
            prot.guiserv_port = c_gui_port;

            prot.conserv_addr = c_vdr_host;
            prot.conserv_port = c_stream_port;
            prot.serv_addr = c_vdr_host;
            prot.serv_port = 16886;

            serialise_udp(&prot,(unsigned char*)data,52);
            desthostip = ntohl(prot.client_addr);
            memcpy(&in,&desthostip,4);
            desthost = inet_ntoa(in);

            printf("Replying to directory query from host %s\n",desthost);
            udp_send(data,52,desthost,destport);
        }
    }

    // Finish up.  When do we get here?
    close(s);

    return 0;
}




static int udp_listen(const char *iface, int port)
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


static int udp_send(char *data, int len, const char *addr, int port)
{
    int                 n;
    int                 sock;
    struct sockaddr_in  serv_addr;
    struct sockaddr_in  cli_addr;

    memset((char *)&cli_addr, 0, sizeof(cli_addr));
    memset((char *)&serv_addr, 0, sizeof(serv_addr));

    if (inet_aton(addr, &serv_addr.sin_addr) == 0) {
        return -1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == - 1) {
        return -1;
    }
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_port = htons((u_short)1234);
    cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(sock,(struct sockaddr *)&cli_addr,sizeof(cli_addr));


    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((u_short)port);

    n = sendto(sock, data, len, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    close(sock);

    return n;

}

static void parse_udp(udpprot_t *prot,unsigned char *buf, int len)
{
    unsigned char  *ptr = buf;

    BUF_TO_INT32(prot->sequence,ptr);
    BUF_TO_INT16(prot->id1,ptr);
    BUF_TO_INT16(prot->id2,ptr);
    memcpy(&prot->mac,ptr,6);
    ptr += 6;
    ptr += 2;  /* Skip pad */
    BUF_TO_INT32(prot->client_addr,ptr);
    BUF_TO_INT16(prot->client_port,ptr);
    ptr += 2;  /* Skip pad */
    BUF_TO_INT32(prot->guiserv_addr,ptr);
    BUF_TO_INT16(prot->guiserv_port,ptr);
    ptr += 2;  /* Skip pad */
    BUF_TO_INT32(prot->conserv_addr,ptr);
    BUF_TO_INT16(prot->conserv_port,ptr);
    ptr += 6;  /* Skip pad */
    BUF_TO_INT32(prot->serv_addr,ptr);
    BUF_TO_INT16(prot->serv_port,ptr);
}

static void serialise_udp(udpprot_t *prot, unsigned char *buf, int len)
{
    unsigned char *ptr = buf;
    memset(buf,0,len);

    INT32_TO_BUF(prot->sequence,ptr);
    INT16_TO_BUF(prot->id1,ptr);
    INT16_TO_BUF(prot->id2,ptr);
    memcpy(&prot->mac,ptr,6);
    ptr += 6;
    ptr += 2;  /* Skip pad */
    INT32_TO_BUF(prot->client_addr,ptr);
    INT16_TO_BUF(prot->client_port,ptr);
    ptr += 2;  /* Skip pad */
    INT32_TO_BUF(prot->guiserv_addr,ptr);
    INT16_TO_BUF(prot->guiserv_port,ptr);
    ptr += 2;  /* Skip pad */
    INT32_TO_BUF(prot->conserv_addr,ptr);
    INT16_TO_BUF(prot->conserv_port,ptr);
    ptr += 6;  /* Skip pad */
    INT32_TO_BUF(prot->serv_addr,ptr);
    INT16_TO_BUF(prot->serv_port,ptr);
}
