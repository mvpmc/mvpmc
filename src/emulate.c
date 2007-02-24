/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
 *  http://www.mvpmc.org/
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
#include <zlib.h>


#include <sys/stat.h>
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
#include "mythtv.h"
#include "config.h"
#include "web_config.h"

#include <vncviewer.h>
#include <nano-X.h>


#if 0
	#define PRINTF(x...) printf(x)
	#define TRC(fmt, args...) printf(fmt, ## args)
	#define VERBOSE_DEBUG 1
#else
	#define PRINTF(x...)
	#define TRC(fmt, args...)
#endif

void mvp_timer_callback(mvp_widget_t *widget);

void query_host_parameters(void);
void client_play(char *filename);
void client_stop(void);
void client_pause(void);
void client_rewind(void);
void client_forward(void);
int goThirty(void);
extern video_callback_t mvp_functions;
void UpdateFinished(void);
void SetDisplayState(int state);
void SetSurface(void);
void RestoreSurface(void);

Bool media_init(stream_t *stream, char *hostname, int port);
Bool media_send_request(stream_t *stream, char *uri);
Bool media_send_read(stream_t *stream);
Bool media_send_step(stream_t *stream, Bool forward);
Bool media_send_seek(stream_t *stream, int32_t offset);
Bool media_send_stop(stream_t *stream);
Bool media_send_eight(stream_t *stream);
void media_queue_data(stream_t *stream);

int media_seek(int value);
int needPing = 0;

void pauseFileAck(int);
int platform_init(void);
int rfb_init(char *hostname, int port);
void mvp_fdinput_callback(mvp_widget_t *widget, int fd);
void vnc_timer_callback(mvp_widget_t *widget);
void mvp_server_remote_key(char key);
void mvp_server_cleanup(void);
void mvp_key_callback(mvp_widget_t *widget, char key);
int udp_listen(const char *iface, int port);
int udp_broadcast(char *data, int len, int port);

void ClearSurface(osd_surface_t *sfc);
Bool direct_init(stream_t *stream, char *hostname, int port);

#define rfbRdcAck 7
#define rfbRdcProgress 9

Bool RDCSendStop(void);
Bool RDCSendProgress(stream_t *stream);
Bool RDCSendRequestAck(int type, int responding);
int set_route(int rtwin);

extern Bool vnc_debug;

int wol_getmac(char *ip);
int wol_wake(void);

int is_live = 0;
extern demux_handle_t *handle;
void demux_set_iframe(demux_handle_t *handle,int type);

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
static int osd_visible = 0;
static int numRead=0;

stream_t  mystream;

extern Bool shareDesktop;

extern char *passwdFile;
extern int updateRequestX;
extern int updateRequestY;
extern int updateRequestW;
extern int updateRequestH;
extern int useHauppageExtentions;

extern int rfb_mode;
extern int flicker;

void PauseDisplayState(void);

static osd_surface_t    *surface = NULL;
static osd_surface_t    *surface_blank = NULL;
static osd_surface_t    *surface2 = NULL;
static osd_surface_t    *surface3 = NULL;


static int surface_y;


static int mvp_media = 0;
int output_pipe=-1;

typedef enum {
	EMU_RUNNING=0,
	EMU_RDC_STOP,
	EMU_STOP_PENDING,
	EMU_STOPPED,
	EMU_STOPPED_AGAIN,
	EMU_EMPTY_BUFFER,
	EMU_STEP,
	EMU_STEP_PENDING,
	EMU_STEP_SEEK,
	EMU_STEP_ACK,
	EMU_STEP_TO_PLAY,
	EMU_SEEK_TO_PLAY,
	EMU_OFF,
	EMU_REPLAY_PENDING,
	EMU_SKIP_PENDING,
	EMU_SKIP_REPLAY_OK,
	EMU_PERCENT_PENDING,
	EMU_PERCENT_RUNNING,
	EMU_PERCENT,
} emu_state_t;


static struct {
	int mode;
	int x;
	int y;
} gb_scale;

static int mvp_state = EMU_RUNNING;
extern volatile long long jump_target;

static int is_stopping = EMU_RUNNING;
int displayOSDFile(char *filename);

#define MVP_NAMED_PIPE "/tmp/FIFO"

//int output_file;

int connect_to_servers(void);
Bool media_read_message(stream_t *stream);

extern osd_font_t font_CaslonRoman_1_25;

int mvp_server_init(void)
{
	char buffer[30];
	c_server_host=mvp_server;

	passwdFile = NULL;
	updateRequestX = 0;
	updateRequestY = 0;
	updateRequestW = 0;
	updateRequestH = 0;
	shareDesktop = False;

	if ( c_query_host == 0 && c_server_host == NULL ) {
		printf("Should either query for connection parameters or specify them\n");
		return -1;
	}
	surface_y = si.rows;

	av_video_blank();

	if ( (surface = osd_create_surface(720,surface_y,0,OSD_GFX) )  == NULL ) {
		printf("Couldn't create surface\n");
		return -1;
	}
	if ( (surface2 = osd_create_surface(720,surface_y,0,OSD_GFX) )  == NULL ) {
		printf("Couldn't create surface\n");
		osd_destroy_surface(surface);
		surface = NULL;
		return -1;
	}
	if ( (surface_blank = osd_create_surface(720,surface_y,0,OSD_GFX) )  == NULL ) {
		printf("Couldn't create surface\n");
		osd_destroy_surface(surface2);
		surface2 = NULL;
		osd_destroy_surface(surface);
		surface = NULL;
		return -1;
	}
	if ( (surface3 = osd_create_surface(720,surface_y,0,OSD_GFX) )  == NULL ) {
		printf("Couldn't create surface3 \n");
		osd_destroy_surface(surface_blank);
		surface_blank = NULL;
		osd_destroy_surface(surface2);
		surface2 = NULL;
		osd_destroy_surface(surface);
		surface = NULL;
		return -1;
	}
	osd_drawtext(surface, 150, 150, "Please Wait",
		     osd_rgba(93,200, 237, 255),
		     osd_rgba(0, 0, 0, 255), 1, &font_CaslonRoman_1_25);

	if (strcmp(mvp_server,"?")) {
		snprintf(buffer,sizeof(buffer),"Connecting to %s",mvp_server);
	} else {
		snprintf(buffer,sizeof(buffer),"Connecting to first server");
	}

	osd_drawtext(surface, 150, 190, buffer,
		     osd_rgba(93,200, 237, 255),
		     osd_rgba(0, 0, 0, 255), 1, &font_CaslonRoman_1_25);

	osd_display_surface(surface);

	ClearSurface(surface_blank);

	PRINTF("Created surface %p\n",surface);

	set_route(4096);
	if ( connect_to_servers() == -1 ) {
		mvp_server_cleanup();
		return -1;
	}
	return 0;
}

/* Connect to servers */
int connect_to_servers(void)
{
	int   i = 0;

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
	wol_wake();
	query_host_parameters();
	if (c_addr==NULL) {
		return -1;
	} else {
		wol_getmac(c_addr);
	}
	c_server_host = c_addr;	 /* Leak first time if configured... */

	osd_drawtext(surface, 150, 230, "Starting Application   ",
		     osd_rgba(93,200, 237, 255),
		     osd_rgba(0, 0, 0, 255), 1, &font_CaslonRoman_1_25);

	for (i=0;i<10;i++) {
		printf("RFB address %s:%d\n",c_server_host,c_gui_port);
		printf("Media address %s:%d\n",c_server_host,c_stream_port);
		PRINTF("Direct address %s:%d\n",c_server_host,c_direct_port);
		if ( rfb_init(c_server_host,c_gui_port)  == -1 ) {
			sleep(1);
			continue;
		}
		if ( c_direct_port <= 0 ) {
			if ( media_init(&mystream,c_server_host,c_stream_port) == False ) {
				printf("Could not connect to media server\n");
				sleep(1);
				continue;
			}
		} else {
			if ( direct_init(&mystream,c_server_host,c_direct_port) == False ) {
				sleep(1);
				continue;
			}
		}
		break;
	}
	return 0;
}

/** \brief Poll for host parameters and keep going till then
 */
void query_host_parameters(void)
{
	char   ether[6];
	char   buf[52];
	char   buffer[20];
	char  *ptr;
	u_int32_t  ipaddr;
	int        s;
	int        listen;
	struct     timeval tv;
	fd_set     fds;
	int attempts;

#ifdef linux
	struct ifreq s_ether;
	struct ifreq s_ipaddr;
	struct sockaddr_in *sin;


	PRINTF("Using interface %s\n",c_interface);
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

	c_addr = NULL;

	listen = udp_listen(NULL,16882);

	if (listen==-1) {
		return;
	}
	attempts = 0;

	while ( attempts < 30 ) {

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

		PRINTF("Querying host parameters\n");
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
			if ( ReadExact(listen,buf,52) ) {
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
				if (strcmp(mvp_server,"?") && strcmp(mvp_server,"1")) {
					if (strcmp(mvp_server,c_addr)==0) {
						return;
					} else {
						printf("IP %s did not match %s\n",c_addr,mvp_server);
						FREENULL(c_addr);
						attempts++;
					}
				} else {
					return;
				}
			}
		} else {
			attempts++;
			snprintf(buffer,sizeof(buffer),"Atempt ( %d / 30)  ",attempts);
			osd_drawtext(surface, 150, 230, buffer,
				     osd_rgba(93,200, 237, 255),
				     osd_rgba(0, 0, 0, 255), 1, &font_CaslonRoman_1_25);
		}
	}
}

pthread_t mvp_server_thread;

pthread_mutex_t mymut = PTHREAD_MUTEX_INITIALIZER;

void *mvp_server_start(void *arg);


int mvp_server_register(void)
{

	pthread_create(&mvp_server_thread, &thread_attr_small,mvp_server_start, NULL);
	printf("output thread started\n");
	return 1;
}


void *mvp_server_start(void *arg)
{
	fd_set      fds;
	fd_set      wfds;
	struct timeval tv;

	printf("Starting mvp media writer\n");


	/* Create a pipe for funnelling through to the appropriate thread */
	pipe(&mystream.socks[0]);

	/* Set the writer to be non-blocking */
	fcntl(mystream.socks[1],F_SETFL,O_NONBLOCK);

	unlink(MVP_NAMED_PIPE);
	mkfifo(MVP_NAMED_PIPE, S_IRWXU);
	mvp_media = 1;
	mystream.out_position = 0;
	mystream.request_read = 0;
	mystream.queued=0;
	mystream.mediatype= 0;
	mystream.volume = volume;
	paused = 0;
	gb_scale.x = gb_scale.y = gb_scale.mode = 0;
	mvpw_set_timer(vnc_widget, mvp_timer_callback, 5000);
	mvpw_set_fdinput(vnc_widget, mvp_fdinput_callback);
	mvpw_set_key(vnc_widget, mvp_key_callback);
//    GrRegisterInput(rfbsock);

//    SendIncrementalFramebufferUpdateRequest();
	mvp_state=EMU_RUNNING;

	while ( mvp_media == 1 ) {
		if ( mystream.sock <= 0) {
			usleep(10000);
			continue;
		}
		tv.tv_sec = 1;
		tv.tv_usec = 100;

		FD_ZERO(&fds);
		FD_ZERO(&wfds);

		if ( mystream.inbuflen ) {
			FD_SET(mystream.socks[1],&wfds);
		}
//        FD_SET(rfbsock,&fds);
		FD_SET(mystream.sock,&fds);

		if (select(FD_SETSIZE, &fds, NULL, NULL, &tv) < 0) {
			perror("select");
			break;
		}

		if (FD_ISSET(mystream.socks[1], &wfds) ) {
			printf("blocked\n");
			media_queue_data(&mystream);
		}
		/*
		if (FD_ISSET(rfbsock, &fds)) {
		    if (mvp_state== EMU_STEP || mvp_state== EMU_STEP_PENDING|| mvp_state== EMU_STEP_SEEK || mvp_state== EMU_STEP_TO_PLAY) {
			if (!HandleRFBServerMessage(rfbsock)) {
			    connect_to_servers();
			    continue;
			}
		    }
		}
		*/
		if (FD_ISSET(mystream.sock, &fds)) {
			if (!media_read_message(&mystream)) {
				break;
			}
		}
	}
	if ( mystream.sock != -1 ) {
		close(mystream.sock);
		mystream.sock = -1;
	} else if ( mystream.directsock != -1 ) {
		close(mystream.directsock);
		mystream.directsock = -1;
	}
	close(mystream.socks[0]);
	close(mystream.socks[1]);
	unlink(MVP_NAMED_PIPE);
	mvpw_set_timer(vnc_widget, NULL, 5000);
	mvpw_set_fdinput(vnc_widget, NULL);
	mvpw_set_key(vnc_widget, NULL);
	mvpw_set_bg(root, MVPW_BLACK);
	av_set_volume(volume);
	printf("Stopped mvp media writer\n");
	if (mvp_media == 1) {
		mvp_media = 2;
		mvp_key_callback(vnc_widget,MVPW_KEY_POWER);
	}
	return NULL;
}

void mvp_server_stop(void)
{
	if (mvp_media != 2) {
		mvp_server_remote_key(MVPW_KEY_POWER);
	} else {
		printf("Emulation Mode Server abort\n");
	}
	GrUnregisterInput(rfbsock);
	close(rfbsock);
	mvp_media = 0;
	mvp_server_cleanup();
}

void mvp_server_cleanup(void)
{
	osd_destroy_surface(surface3);
	surface3 = NULL;
	osd_destroy_surface(surface2);
	surface2 = NULL;
	osd_destroy_surface(surface_blank);
	surface_blank = NULL;
	osd_destroy_surface(surface);
	surface = NULL;
	set_route(web_config->rtwin);
}
#define HAUP_KEY_FORWARD 0x0f
#define HAUP_KEY_PLAY    0x19
#define HAUP_KEY_STOP    0x1b
#define HAUP_KEY_PAUSE   0x1c
#define HAUP_KEY_REPLAY  0x2a
#define HAUP_KEY_SKIP    0x2b



void mvp_server_remote_key(char key)
{
	static int hauppageKey[] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x36, 0x24, 0x28, 0x1e, 0x37, 0x15,
		0x11, 0x10, 0x35, 0x00, 0x33, 0x34, 0x32, 0x31,
		0x2d, 0x2e, 0x2f, 0x00, 0x2c, 0x30, HAUP_KEY_SKIP, 0x20,
		0x12, 0x13, 0x00, 0x00, HAUP_KEY_REPLAY, 0x0d, 0x00, 0x00,
		0x00, 0x27, 0x00, 0x00, 0x00, 0x00, 0x25, 0x00,
		HAUP_KEY_PAUSE, 0x00, 0x0e, 0x00, HAUP_KEY_FORWARD, HAUP_KEY_PLAY, HAUP_KEY_STOP, 0x1a,
		0x26, 0x00, 0x00, 0x23, 0x29, 0x14
	};

	int key1;
	mystream.last_key = key;
	if ( mystream.mediatype==TYPE_AUDIO || mystream.mediatype==TYPE_VIDEO ) {

		if (is_stopping != EMU_RUNNING  && key!=MVPW_KEY_STOP   &&  key != MVPW_KEY_MENU) {
			// not all keys valid when media stream is finished
			return;
		}

		key1 = hauppageKey[(int)key];
		PRINTF("av keymap %i = %x\n", key, key1);
		switch (key) {
		case MVPW_KEY_STOP:
			if (mvp_state==EMU_RUNNING || mvp_state==EMU_STEP) {
				is_stopping = EMU_STOP_PENDING;
				osd_visible = -1;
				SendKeyEvent(key1, 0);
			} else {
				is_stopping = EMU_EMPTY_BUFFER;
				osd_visible = -1;
			}
			break;
		case MVPW_KEY_PLAY:
			if (mvp_state==EMU_STEP || mvp_state==EMU_STEP_SEEK) {
				mvp_state=EMU_STEP_TO_PLAY;
				SendKeyEvent(key1, 0);
			} else {
				SendKeyEvent(key1, 0);
//                    SendIncrementalFramebufferUpdateRequest();
			}
			SendIncrementalFramebufferUpdateRequest();
			break;
		case MVPW_KEY_PAUSE:
			if (mystream.length > 0 ) {
				SendKeyEvent(key1, 0);
				SendIncrementalFramebufferUpdateRequest();
			}
			break;
		case MVPW_KEY_OK:
			SendKeyEvent(key1, 0);
			SendIncrementalFramebufferUpdateRequest();
			break;
		case MVPW_KEY_POWER:
			mvp_state = EMU_OFF;
			SendKeyEvent(key1, 0);
			break;
		case MVPW_KEY_FFWD:
			if (mvp_state==EMU_RUNNING) {
				if (mystream.length > 0 ) {
					mvp_state = EMU_STEP_PENDING;
					mystream.direction = 1;                        
					PRINTF("FFWD Key\n");
					SendKeyEvent(key1, 0);
					SendIncrementalFramebufferUpdateRequest();
				}
			} else if (mvp_state==EMU_STEP) {
				if (mystream.direction == 1 ) {
					// speed up on day
				} else {
					// change direction
				}
			}
			break;
		case MVPW_KEY_REWIND:
			if (mvp_state==EMU_RUNNING) {
				if (mystream.length > 0 ) {
					mvp_state = EMU_STEP_PENDING;
					mystream.direction = 0;
					PRINTF("REWIND Key\n");
					SendKeyEvent(key1, 0);
					SendIncrementalFramebufferUpdateRequest();
				}
			} else if (mvp_state==EMU_STEP) {
				if (mystream.direction == 0 ) {
				} else {
				}
			}
			break;
		case MVPW_KEY_REPLAY:
			if (mystream.length > 0 && mvp_state==EMU_RUNNING ) {
				mvp_state = EMU_REPLAY_PENDING;
				SendKeyEvent(key1, 0);
				SendIncrementalFramebufferUpdateRequest();
			}
			break;
		case MVPW_KEY_SKIP:
			if (mystream.length > 0 && mvp_state==EMU_RUNNING) {
				mvp_state = EMU_SKIP_PENDING;
				SendKeyEvent(key1, 0);
				SendIncrementalFramebufferUpdateRequest();
			}
			break;
		default:
			break;
		case MVPW_KEY_ZERO ... MVPW_KEY_NINE:
			if ( mystream.length > 0 && mvp_state==EMU_RUNNING) {
				// only Hauppauge protocol supported
				if (useHauppageExtentions == 2 ) {
					mvp_state = EMU_PERCENT_PENDING;
				}
				mystream.direction = key1;
				SendKeyEvent(key1, 0);
				SendIncrementalFramebufferUpdateRequest();
//                    }

			}
			break;
		case MVPW_KEY_CHAN_UP:
		case MVPW_KEY_CHAN_DOWN:
		case MVPW_KEY_PREV_CHAN:
		case MVPW_KEY_RECORD:
			SendKeyEvent(key1, 0);
			SendIncrementalFramebufferUpdateRequest();
			break;
		case MVPW_KEY_MENU:
		case MVPW_KEY_RED:
		case MVPW_KEY_GREEN:
		case MVPW_KEY_YELLOW:
		case MVPW_KEY_BLUE:
		case MVPW_KEY_GO:
		case MVPW_KEY_FULL:
		case MVPW_KEY_RIGHT:
		case MVPW_KEY_LEFT:
		case MVPW_KEY_UP:
		case MVPW_KEY_DOWN:
		case MVPW_KEY_EXIT:
			SendKeyEvent(key1, 0);
			SendIncrementalFramebufferUpdateRequest();
			break;
		case MVPW_KEY_VOL_UP:
			if (mystream.volume < 255) {
				mystream.volume++;
				av_set_volume(mystream.volume);
			}
			SendKeyEvent(key1, 0);
			SendIncrementalFramebufferUpdateRequest();
			break;
		case MVPW_KEY_VOL_DOWN:
			if (mystream.volume > 225) {
				mystream.volume--;
				av_set_volume(mystream.volume);
			}
			SendKeyEvent(key1, 0);
			SendIncrementalFramebufferUpdateRequest();
			break;
		case MVPW_KEY_MUTE:
			av_mute();
			SendKeyEvent(key1, 0);
			SendIncrementalFramebufferUpdateRequest();
			break;
		}
	} else {
		if (key > 0x61) {
			key1 = key;
		} else {
			key1 = hauppageKey[(int)key];
			if (key1==0) {
				key1 = key;
			}
		}
		if (is_stopping == EMU_STOPPED_AGAIN ) {
			is_stopping = EMU_RUNNING;
		}
		PRINTF("keymap %i = %x\n", key, key1);
		SendKeyEvent(key1, 0);
		if (key!=MVPW_KEY_POWER) {
			SendIncrementalFramebufferUpdateRequest();
		}
	}
	return;
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

	if (setsockopt (sock, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof(on))< 0) {
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons((u_short)port);

	if (strcmp(mvp_server,"?")==0) {
		serv_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	} else {
		serv_addr.sin_addr.s_addr = inet_addr(mvp_server);
	}

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
	osd_visible = -1;
	if ( !ConnectToRFBServer(hostname, port)) {
		return -1;
	}

	PRINTF("Socket %d\n",rfbsock);
	updateRequestH = si.rows;
	if (!InitialiseRFBConnection(rfbsock)) {
		close(rfbsock);
		return -1;
	}
	// 10 0 OSDPACK.MVP

	current = strdup("OSDPACK.MVP");
	pauseFileAck(0x00);
	free(current);

	updateRequestH = si.rows;
	if (rfb_mode==3 || rfb_mode < 0 || rfb_mode > 3) {
		if (c_stream_port>=8337 && c_stream_port < 8347) {
			/* allow for 1st ten gbpvr connections */
			useHauppageExtentions = 1;
		} else {
			useHauppageExtentions = 2;
		}
	} else {
		useHauppageExtentions = rfb_mode;
	}

	if (!SetFormatAndEncodings()) {
		close(rfbsock);
		return -1;
	}
	usleep(1000);
	if (!SendFramebufferUpdateRequest(updateRequestX, updateRequestY,
					  updateRequestW, updateRequestH, False)) {
		close(rfbsock);
		return -1;
	}

	PRINTF("End rfb_init\n");
	osd_visible = 0;

	return rfbsock;
}

/*
 *
 *   Handle the Hauppauge extensions to the RFB protocol
 */

Bool RDCSendStop(void)
{
	char    buf[34];

	memset(buf,0,sizeof(buf));

	buf[0] = RDC_STOP;

	if (!WriteExact(rfbsock,buf,sizeof(buf)) ) {
		return False;
	}
	return True;
}

Bool RDCSendRequestAck(int type, int responding)
{
	char    buf[34];

	memset(buf,0,sizeof(buf));

	buf[0] = rfbRdcAck;
	buf[1] = type;
	buf[2]= responding;

	PRINTF("ACK Request %d %d\n",buf[1],responding);
	if (!WriteExact(rfbsock,buf,sizeof(buf)) ) {
		return False;
	}
	return True;
}

Bool RDCSendProgress(stream_t *stream)
{
	char    buf[10];
	char *ptr;

	memset(buf,0,sizeof(buf));

	buf[0] = rfbRdcProgress;
	ptr = buf + 4;
	INT32_TO_BUF(stream->current_position,ptr);

	if (!WriteExact(rfbsock,buf,sizeof(buf)) ) {
		return False;
	}
	return True;
}

Bool RDCSendPing(void)
{
	char    buf[2];

	buf[0] = 0x08;
	buf[1] = 0;

	if (!WriteExact(rfbsock,buf,sizeof(buf)) ) {
		return False;
	}
	return True;
}


Bool HandleRDCMessage(int sock)
{
	char   buf[64];
	char *ptr;
	unsigned int value;

	PRINTF("Received RDC command ");
	if ( !ReadExact(sock,buf + 1,33) ) {
		return False;
	}

	buf[0] = rfbRdcAck;
	buf[2] = 1;
	PRINTF("%d\n",buf[1]);

	switch ( buf[1] ) {
	case RDC_PLAY:
		{
			PRINTF("RDC_PLAY\n");
			int   length = buf[8];
			char *filename;

			if ( mystream.mediatype==0 ) {
				osd_blit(surface3,0,0,surface,0,0,720,surface_y);
			}

			filename = malloc(length + 1);

			if (!ReadExact(sock,filename,length) ) {
				return False;
			}
			if (length==0 && current!=NULL) {
				free(filename);
				filename = strdup(current);
			} else {
				filename[length] = 0;
				current = strdup(filename);
			}
			PRINTF("We need to play %d switch %x %x %s %d\n",mvp_state,buf[15],buf[19],filename,length);
			switch (buf[15]) {
			case 0:
				if (paused) {
					av_pause();
					paused = 0;
					pauseFileAck(0x01);
					RDCSendRequestAck(RDC_PLAY,1);
				} else {
					if (mvp_state==EMU_STEP_TO_PLAY) {
						mvp_state=EMU_SEEK_TO_PLAY;
						SetSurface();
						media_send_step(&mystream,mystream.direction);
					} else {
						if (mystream.mediatype == TYPE_VIDEO) {
							if (length==0 && mvp_state == EMU_RUNNING ) {
								// just a play during a play
								break;
							}
							PRINTF("FAKE STOP\n");
							jumping = 1;
							jump_target = -1;
							mystream.request_read = mystream.queued - mystream.out_position;
							close(output_pipe);
							mystream.queued = 0;
							output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
							jump_target = 0;
							is_stopping = mvp_state = EMU_RUNNING;
							if (mystream.last_key!=MVPW_KEY_CHAN_UP ||mystream.last_key!=MVPW_KEY_CHAN_DOWN ) {
							}
						}
						while ( is_stopping==EMU_RUNNING && av_empty() == 0 ) {
							usleep(100000);
						}
						client_play(filename);
					}
				}
				break;
			case 1:
				client_play(filename);
				break;
			case 2:
				ptr = buf + 18;
				BUF_TO_INT16(value,ptr);
				mvp_state=EMU_SKIP_REPLAY_OK;
				media_send_seek(&mystream,media_seek(value));
				break;
			case 3:
				ptr = buf + 18;
				BUF_TO_INT16(value,ptr);
				mvp_state=EMU_SKIP_REPLAY_OK;
				media_send_seek(&mystream,media_seek(-1*value));
				break;
			case 4:
				mvp_state=EMU_PERCENT_RUNNING;
				media_send_seek(&mystream,(mystream.length*buf[19])/100);
				break;
			default:
				if (mvp_state==EMU_STEP_TO_PLAY) {
					mvp_state=EMU_SEEK_TO_PLAY;
					SetSurface();
					media_send_step(&mystream,mystream.direction);
				}
				break;
			}
			free(filename);
			return True; /* We don't want to send an ack at this stage */
			break;
		}
	case RDC_PAUSE:
		{
			PRINTF("RDC_PAUSE\n");
			client_pause();
			break;
		}

	case RDC_STOP:
		{
			PRINTF("RDC_STOP\n");
			if (paused) {
				av_pause();
			}
			if (mystream.mediatype == 0) {
				PRINTF("Ignore this RDC_STOP\n");
				/*
				if ( media_init(&mystream,c_server_host,c_stream_port) == False ) {
				}
				*/
				break;
			} else if (is_stopping == EMU_STOPPED ) {
				PRINTF("Ignore this because STOP PENDING \n");
				is_stopping = EMU_STOPPED_AGAIN;            
			} else if (is_stopping != EMU_RUNNING || mystream.mediatype == TYPE_AUDIO ) {
				usleep(200000);
				client_stop();
//            RDCSendRequestAck(RDC_STOP,1);
				audio_stop = 1;
				is_stopping = EMU_STOPPED;
			} else if (mvp_state == EMU_RUNNING || mvp_state == EMU_STOP_PENDING ) {
				PRINTF("EOF RDC_STOP\n");
				if (mvp_state == EMU_RUNNING ) {
					client_stop();
					is_stopping = EMU_STOPPED;
				}
//            osd_visible=-1;
			} else {
				PRINTF("EMU_RDC_STOP\n");
				if ( media_init(&mystream,c_server_host,c_stream_port) == False ) {
				}
				client_stop();
				mvp_state = EMU_RUNNING;
//            RDCSendRequestAck(RDC_STOP,1);
				audio_stop = 1;
				is_stopping = EMU_STOPPED;
			}
			/* ack after playout */
/*
	if ((mvp_state!=EMU_RUNNING && mvp_state!=EMU_STEP) || mystream.mediatype == TYPE_AUDIO) {
	    RDCSendRequestAck(RDC_STOP);
	    mvp_state = EMU_STOPPED;
	}
*/
			return True;
			break;
		}
	case RDC_REWIND:
		{
			PRINTF("RDC_REWIND\n");
			if (mvp_state==EMU_PERCENT) {
				break;
			}
			client_rewind();
			jumping = 1;
			jump_target = -1;
			mystream.request_read = mystream.queued - mystream.out_position;
			close(output_pipe);
			mystream.queued = 0;
			media_send_read(&mystream);
			output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
			jump_target = 0;
			/* ack when loaded */
			return True;
			break;
		}
	case RDC_FORWARD:
		{
			PRINTF("RDC_FORWARD\n");
			if (mvp_state==EMU_PERCENT) {
				break;
			}
			client_forward();
//		RDCSendRequestAck(RDC_FORWARD,1);
			jumping = 1;
			jump_target = -1;
			mystream.request_read = mystream.queued - mystream.out_position;
			close(output_pipe);
			mystream.queued = 0;
			media_send_read(&mystream);
			output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
			jump_target = 0;
			/* ack when loaded */
			return True;
			break;
		}
	case RDC_VOLUP:
		{
			PRINTF("RDC_VOLUP\n");
			break;
		}
	case RDC_VOLDOWN:
		{
			PRINTF("RDC_VOLDOWN\n");
			break;
		}
	case RDC_MENU:
		{
			PRINTF("RDC_MENU\n");
			buf[7] = 0;
			char display[2];
			if ( !ReadExact(sock,display,2) ) {
				return False;
			}
			PRINTF("Display is state %d %x\n",display[0],buf[7]);
			SetDisplayState(display[0]);
			break;
		}
	case RDC_MUTE:
		{
			PRINTF("RDC_MUTE\n");
			break;
		}
	case RDC_SETTINGS:
		{
			char   *settings;
			PRINTF("RDC_SETTINGS ");
			settings=malloc(buf[7]);
			memset(settings,0,buf[7]);
			if ( !ReadExact(sock,settings,buf[7]) ) {
				free(settings);
				return False;
			}
			PRINTF("Settings command %d\n",settings[0]);
			buf[2]=1;
			if ( !WriteExact(sock,buf,34) ) {
				free(settings);
				return False;
			}
			if (si.rows==480) {
				settings[1] = 0;
			} else {
				settings[1] = 1;
			}
			switch (av_get_output()) {
			case AV_OUTPUT_SCART:
				settings[2] = 0;
				break;
			case AV_OUTPUT_SVIDEO:
				settings[2] = 1;
				break;
			case AV_OUTPUT_COMPOSITE:
				settings[2] = 2;
				break;
			default:
				settings[2] = 3;
				break;                    
			}

			settings[3] = 0 ;  // no flicker

			if ( av_get_tv_aspect() == AV_TV_ASPECT_16x9 ) {
				settings[4] = 1;
			} else {
				settings[4] = 0;
			}
			/* gbpvr setting 14 - ea-60-01-00  20-0a  24-01 */
			ptr = settings + 12;
			BUF_TO_INT32(value,ptr);
			printf("%x ss timeout %u mode %x un1 %x un2 %x\n",buf[7],value,settings[16],settings[20],settings[24]);

			if ( !WriteExact(sock,settings,buf[7]) ) {
				free(settings);
				return False;
			}
			free(settings);
			return True;
			break;
		}
	case RDC_SCALE:
		{
			char   *scale;
			int factor;
			PRINTF("RDC_SCALE\n");
			scale=malloc(buf[8]);
			memset(scale,0,buf[8]);
			if ( !ReadExact(sock,scale,buf[8]) ) {
				free(scale);
				return False;
			}
			gb_scale.mode = scale[1];
			if (gb_scale.mode) {
				if ( osd_visible == 0 ) {
					osd_visible = -2;
				}
				if (gb_scale.mode==2 ) {
					factor = 0;
				} else {
					factor = 80;
				}
				gb_scale.mode++;
				gb_scale.x = (720 - factor + scale[3])/scale[1];
				if (surface_y == 480 || gb_scale.mode == 4) {
					gb_scale.y = (surface_y + scale[2])/scale[1];
				} else {
					gb_scale.y = (surface_y -76 + scale[2])/scale[1];
				}
				PRINTF("%d %d %d %d %d %d\n",scale[1],scale[2],scale[3],gb_scale.x, gb_scale.y, gb_scale.mode);
			} else {
				av_wss_redraw();
				gb_scale.x = gb_scale.y = gb_scale.mode = 0;
			}
			if ( mystream.mediatype==TYPE_VIDEO  ) {
				av_move(gb_scale.x, gb_scale.y, gb_scale.mode);
			}
			break;
		}
	default:
		{
			PRINTF("RDC_UNHANDLED %d\n",buf[1]);
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
	media_send_eight(&mystream);
	if (mystream.last_key==MVPW_KEY_CHAN_UP || mystream.last_key==MVPW_KEY_CHAN_DOWN ) {
		PRINTF("CHANNEL\n");
	}
}

void client_stop(void)
{
	media_send_stop(&mystream);
	free(current);
	current=NULL;
}

void client_pause(void)
{
	/* native pause for now */
	if (av_pause()) {
		paused = 1;
		pauseFileAck(0x04);
	}
}

void pauseFileAck(int type)
{
	char buf[261];
	if ( mystream.mediatype==TYPE_VIDEO ) {
		memset(buf,0,261);
		buf[0]=0x10;
		buf[1]=type;
		strcpy(&buf[3],current);
		WriteExact(rfbsock,buf,261);
	}
}

void client_rewind(void)
{
	media_send_seek(&mystream,media_seek(0));
}

void client_forward(void)
{
	media_send_seek(&mystream,media_seek(0));
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

	PRINTF("%s %d\n",hostname,port);

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
	char *buf;
	u_int16_t len = strlen(uri);
	char *ptr;


	PRINTF("Requesting");
	PRINTF(" %s\n",uri);
	buf = CALLOC(40 + len + 1, sizeof(char));

	buf[0] = MEDIA_REQUEST;

	ptr = buf + 36;
	INT16_TO_BUF(len + 1,ptr);
	strcpy(buf+40,uri);

	if (!WriteExact(stream->sock,buf,40) ) {
		FREE(buf);
		printf("8 error 1\n");
		return False;
	}
	if (!WriteExact(stream->sock,uri,len + 1) ) {
		FREE(buf);
		printf("8 error 2\n");
		return False;
	}
	FREE(buf);

	stream->last_command = MEDIA_REQUEST;

	return True;
}

Bool media_send_read(stream_t *stream)
{
	char    buf[40];
	char   *ptr;

	if ( mvp_state == EMU_STOP_PENDING ) {
		return False;
	}
	memset(buf,0,sizeof(buf));
	memcpy(buf+6,stream->fileid,2);


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
	char    buf[40];

	memset(buf,0,sizeof(buf));
	memcpy(buf+6,stream->fileid,2);

	buf[0] = MEDIA_STEP;
	buf[9] = forward ? 1 : 0;

	if (!WriteExact(stream->sock,buf,40) ) {
		return False;
	}

	stream->last_command = MEDIA_STEP;

	return True;
}


Bool media_send_seek(stream_t *stream, int32_t offset)
{
	char   buf[40];
	char  *ptr;

	memset(buf,0,sizeof(buf));
	buf[0] = MEDIA_SEEK;
	memcpy(buf+6,stream->fileid,2);


	ptr = buf + 8;
	INT32_TO_BUF(offset,ptr);

	if (!WriteExact(stream->sock,buf,40) ) {
		return False;
	}

	stream->last_command = MEDIA_SEEK;

	return True;
}

Bool media_send_eight(stream_t *stream)
{
	char   buf[40];

	memset(buf,0,sizeof(buf));
	buf[0] = MEDIA_8;

	if (!WriteExact(stream->sock,buf,40) ) {
		return False;
	}

	stream->last_command = MEDIA_8;

	return True;
}


Bool media_send_stop(stream_t *stream)
{
	char  buf[40];

	memset(buf,0,sizeof(buf));
	buf[0] = MEDIA_STOP;
	memcpy(buf+6,stream->fileid,2);


	if (!WriteExact(stream->sock,buf,40) ) {
		return False;
	}

	stream->last_command = MEDIA_STOP;

	return True;
}

Bool media_read_message(stream_t *stream)
{
	char   buf[40];
	char  *ptr;
	if (!ReadExact(stream->sock,buf,40) ) {
		return False;
	}

	switch ( buf[0] ) {
	case MEDIA_REQUEST:
		{
			PRINTF("Media Request ");
			if ( buf[4] == 0 ) {
				PRINTF("Aborted reading\n");
			} else {
				numRead = 0;
				paused=0;
				stream->mediatype = buf[4];
				if ( buf[4] == TYPE_VIDEO ) {
					PRINTF("Video\n");
					stream->blocklen = 200000;
				} else {
					PRINTF("Audio\n");
					stream->blocklen = 20000;
				}
				// 8 w 
				// 10 h
				is_stopping=EMU_RUNNING;
				ptr = buf + 14;
				BUF_TO_INT16(stream->bps,ptr);
				ptr = buf + 16;
				BUF_TO_INT32(stream->length,ptr);
				printf("bps %d length %lld\n",stream->bps,stream->length);
				memcpy(stream->fileid,buf+34,2);
				if (av_mute()==1) {
					av_mute();
				}
				switch ( buf[4] ) {
				case 0x01:	     /* MPEG */   
					close(output_pipe);

					mystream.queued = 0;
					video_functions = &mvp_functions;
					if (gb_scale.mode==0 ) {
						osd_fill_rect(surface,0,0,720,surface_y,MVPW_TRANSPARENT);
					}
					audio_clear();            
					video_clear();
					mvpw_set_bg(root, MVPW_TRANSPARENT);
//                av_move(0, 0, 0);
					video_play(NULL);
					av_move(gb_scale.x, gb_scale.y, gb_scale.mode);
					mvpw_set_bg(root, MVPW_TRANSPARENT);
					output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
//                output_file = open("/music/mympeg.mpg", O_CREAT|O_TRUNC|O_WRONLY);
					demux_set_iframe(handle,1);
					break;                                        
				case 0x02: /* MP3 */
					av_reset();
					audio_play(NULL);
					output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
					break;
				}
				media_send_seek(stream,0);
				if ( mystream.mediatype == TYPE_VIDEO ) {
					RDCSendRequestAck(RDC_MENU,1);
				}
//            media_send_read(stream);

			}
			break;
		}
	case MEDIA_STEP:
		{
//        PRINTF("Media Step\n");
			ptr = buf + 16;
			BUF_TO_INT32(stream->current_position,ptr);
			if (mvp_state== EMU_STEP_SEEK) {
				PRINTF("Step received1\n");
				mvp_state=EMU_STEP;
//			pthread_kill(video_write_thread, SIGURG);
//			pthread_kill(audio_write_thread, SIGURG);
				/*
				av_stop();
				av_reset();
				av_play();
				*/
				if (stream->direction) {
					osd_drawtext(surface, 100, 100, "Fast Forward",osd_rgba(255,255, 0, 255),MVPW_TRANSPARENT, 1, &font_CaslonRoman_1_25);
					pauseFileAck(0x02);
					RDCSendRequestAck(RDC_FORWARD,1);
				} else {
					osd_drawtext(surface, 100, 100, "Rewind",osd_rgba(255,255, 255, 255),MVPW_TRANSPARENT, 1, &font_CaslonRoman_1_25);
					pauseFileAck(0x03);
					RDCSendRequestAck(RDC_REWIND,1);
				}
//			pthread_cond_broadcast(&video_cond);  
			} else if (mvp_state==EMU_SEEK_TO_PLAY) {
				mvp_state=EMU_RUNNING;
				numRead=0;
//            mvp_state=EMU_RUNNING;
			}
			// Fall through
		}
	case MEDIA_BLOCK:
		{
			int32_t    blocklen;
			if (mvp_state==EMU_SKIP_REPLAY_OK || mvp_state==EMU_PERCENT) {
				mvp_state=EMU_RUNNING;
				numRead=5;
				RDCSendRequestAck(MEDIA_SEEK,1);
				pauseFileAck(0x01);
			}
			if (mvp_state==EMU_PERCENT_RUNNING) {
				mvp_state=EMU_PERCENT;
			}
			if (buf[0]==MEDIA_BLOCK) {
				ptr = buf + 8;
			} else {
				ptr = buf + 12;
			}
			BUF_TO_INT32(blocklen,ptr);
			is_live = 0;
//        PRINTF("Media Block %d",blocklen);

			if ( blocklen != 0 ) {

				if (buf[0]==MEDIA_BLOCK) {
					ptr = buf + 12;
					BUF_TO_INT32(stream->current_position,ptr);
					if ( stream->current_position > stream->length && is_live==0) {
						is_live = 1;
					}
				};

//            PRINTF(" Current %lld\n",stream->current_position);

				stream->inbuf = MALLOC(blocklen);
				if (!ReadExact(stream->sock,stream->inbuf,blocklen) ) {
					return False;
				}
				stream->inbuflen = blocklen;
				stream->inbufpos = 0;
				if (is_stopping == EMU_STOPPED ) {
					PRINTF("Ignore this because Media Block\n");
					break;
				}
				media_queue_data(&mystream);
			} else {
				PRINTF(" %lld %lld\n",stream->current_position,stream->length);
				if ( stream->current_position < stream->length ) {
					if (buf[0]==MEDIA_BLOCK) {
						ptr = buf + 12;
					} else {
						ptr = buf + 16;
					}
					BUF_TO_INT32(stream->inbuflen,ptr);
					printf("%d ",stream->inbuflen);
					stream->inbuflen = stream->length - stream->current_position;
					PRINTF("%d\n",stream->inbuflen);
					printf("Remaining %d\n",stream->inbuflen);
					stream->inbuflen = 0;
					stream->current_position = stream->length;
					/*
					stream->inbuf = MALLOC(stream->inbuflen);
					if (!ReadExact(stream->sock,stream->inbuf,stream->inbuflen) ) {
					    return False;
					}
					stream->inbufpos = 0;
					media_queue_data(&mystream);
					*/
				} else {
					PRINTF("\n");
				}
				PRINTF("Media Done %d\n",mvp_state);
				RDCSendProgress(stream);
				if (mvp_state == EMU_RUNNING || mvp_state == EMU_STEP ) {
					client_stop();
					mvp_state = EMU_STOP_PENDING;
				}
			}
			break;
		}

	case MEDIA_SEEK:
		{
			PRINTF("Media Seek\n");

//        media_send_read(stream);                  
			if (mvp_state== EMU_STEP_PENDING||mvp_state==EMU_STEP_SEEK ||mvp_state==EMU_STEP) {
				ptr = buf + 8;
				INT32_TO_BUF(stream->current_position,ptr);
				PRINTF("First Step\n");
				media_send_step(stream,stream->direction);
				mvp_state=EMU_STEP_SEEK;
			} else if (mvp_state==EMU_SEEK_TO_PLAY) {
				ptr = buf + 8;
				INT32_TO_BUF(stream->current_position,ptr);
				mvp_state = EMU_RUNNING;
				numRead = 0;
				media_send_read(stream);
			} else if (mvp_state == EMU_SKIP_REPLAY_OK || mvp_state == EMU_PERCENT_RUNNING) {
				PRINTF("SKIPPED\n");
				jumping = 1;
				jump_target = -1;
				mystream.request_read = mystream.queued - mystream.out_position;
				close(output_pipe);
				mystream.queued = 0;
				media_send_read(stream);
				output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
				jump_target = 0;
				mvp_state = EMU_RUNNING;
			} else {
//            stream->current_position = 0;
				media_send_read(stream);
			}
			break;
		}
	case MEDIA_STOP:
		{
			mvpw_set_timer(vnc_widget, NULL, 0);
			usleep(10000);
			osd_visible = -1;
			if (mystream.last_key==MVPW_KEY_CHAN_UP || mystream.last_key==MVPW_KEY_CHAN_DOWN ) {
				PRINTF("CHANNEL\n");
//            break;
			}
			close(output_pipe);
//        close(output_file);
			stream->inbuflen = 0;
			PRINTF("Media Stop %d %d\n",is_stopping,mystream.last_command);
			while ( is_stopping==EMU_RUNNING && av_empty() == 0 ) {
				usleep(100000);
			}
			PRINTF("Media Stopped %d %d\n",mvp_state,is_stopping);
			if (is_stopping != EMU_STOPPED ) {
				RDCSendRequestAck(RDC_STOP,1);
			} else {
				RDCSendRequestAck(RDC_STOP,1);
			}
			osd_visible = -2;
//        RDCSendStop();
			if (mystream.mediatype==TYPE_VIDEO /*&& surface==NULL*/) {
				mvpw_set_timer(vnc_widget, NULL, 0);
				video_clear();
				demux_set_iframe(handle,0);
//            av_move(0, 0, 0);
				mvpw_set_bg(root, MVPW_TRANSPARENT);
			} else if (mystream.mediatype==TYPE_AUDIO ) {
				audio_stop = 1;
				audio_clear();
				av_stop();
			}

			mvp_state = EMU_RUNNING;
			osd_visible = 0;
			if (mystream.mediatype == TYPE_VIDEO) {
//            RDCSendRequestAck(RDC_MENU,1);
				usleep(100000);
				RestoreSurface();
			}
			mvpw_set_timer(vnc_widget, mvp_timer_callback, 5000);
			mystream.mediatype= 0;
			if (is_stopping == EMU_STOPPED_AGAIN ) {
				PRINTF("Stopping with stopped again\n");
			}
			is_stopping = EMU_RUNNING;
			SendIncrementalFramebufferUpdateRequest();
			break;
		}
	case MEDIA_8:
		{
			PRINTF("Media 8\n");
			media_send_request(&mystream,current);
			RDCSendRequestAck(MEDIA_8,1);
			break;
		}
	default:
		{
			printf("Media Unhandled %d\n",buf[0]);
			exit(1);
			break;
		}
	}
	return True;
}

void media_queue_data(stream_t *stream)
{
	int      n;
	n = write(output_pipe,stream->inbuf + stream->inbufpos, stream->inbuflen - stream->inbufpos);
	if ( n > 0 ) {
		mystream.queued +=n;
		stream->inbufpos += n;
		if ( stream->inbufpos == stream->inbuflen || is_stopping!=EMU_RUNNING) {
			if (++numRead == 2) {
				/* We've opened the file, send an OSD update*/
				pauseFileAck(0x05);
			} else if (numRead == 5) {
				RDCSendRequestAck(RDC_PLAY,1);
			}
			usleep(10000);
			RDCSendProgress(stream);
			stream->inbufpos = 0;
			stream->inbuflen = 0;
			FREENULL(stream->inbuf);
			if (is_stopping==EMU_RUNNING) {
				switch (mvp_state) {
				case EMU_RUNNING:
//                        PRINTF("Read Block\n");
					media_send_read(stream);
					break;
				case EMU_STEP_PENDING:
					mvp_state=EMU_STEP_SEEK;
					PRINTF("End of stream\n");
					break;
				case EMU_STEP_SEEK:
				case EMU_SEEK_TO_PLAY:
					PRINTF("Seeking\n");
					break;
				case EMU_STEP:
					media_send_step(stream,stream->direction);
					break;
				default:
					break;
				}
			}
		} else {
			PRINTF("What if %d\n",n);
		}
	} else {
		perror("Write QueueData");
	}

	return ;
}


/*
*   mvp.c
*   Native MVP viewer
*/


void RectangleUpdateYUV(int x0, int y0, int w, int h,  unsigned char *buf)
{
	int   x,y;
	unsigned char   y1,y2,u,v;
	unsigned char *ptr = buf;

//    printf("YUV update %d %d %d %d\n",x0,y0,w,h);

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

	if ( mystream.mediatype==TYPE_VIDEO  ) {
		printf("YUV2 update %d %d %d %d\n",x0,y0,w,h);
		return;
	}
	PRINTF("YUV2 update %d %d %d %d\n",x0,y0,w,h);

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

void UpdateFinished(void)
{
	if (mvp_state==2) {
		return;
	}
	pthread_mutex_lock(&mymut);
	PRINTF("Update Finished %d %d\n",osd_visible,mvp_state);
	if ( mystream.mediatype!=TYPE_VIDEO || osd_visible >= 0 ) {
		osd_blit(surface,0,0,surface2,0,0,720,surface_y);
	} else {
		if (osd_visible!=-1) {
			osd_blit(surface,0,0,surface2,0,0,720,surface_y);
		} else {
//            osd_fill_rect(surface,0,0,720,surface_y,MVPW_TRANSPARENT);
//            osd_blit(surface3,0,0,surface2,0,0,720,surface_y);
		}
//        osd_blit(surface,0,0,surface_blank,0,0,720,surface_y);
	}
	pthread_mutex_unlock(&mymut);
}

/** \brief Set the display to be osd_visible/not
 */
void SetDisplayState(int state)
{
	pthread_mutex_lock(&mymut);
	PRINTF("Set Display %d %d\n",osd_visible,state);
	if (state==1) {
		root_color = mvpw_color_alpha(MVPW_TRANSPARENT, 0);
	} else {
		root_color = mvpw_color_alpha(MVPW_BLACK, state);
	}
	switch (state) {
	case 0:
		mvpw_set_timer(vnc_widget, mvp_timer_callback, 5000);
		// restore last screen
		if (osd_visible==-2) {
			osd_visible = 0;
		} else if (osd_visible==-1) {
			osd_fill_rect(surface,0,0,720,surface_y,MVPW_TRANSPARENT);
		}
		break;
	case 1:
		// clear screen
		mvpw_set_timer(vnc_widget, mvp_timer_callback, 1000);
		osd_fill_rect(surface,0,0,720,surface_y,MVPW_TRANSPARENT);
		osd_fill_rect(surface2,0,0,720,surface_y,MVPW_TRANSPARENT);
		break;
	case 2:
		mvpw_set_timer(vnc_widget, mvp_timer_callback, 5000);
		break;
	case 170:
	case 255:
	default:
		osd_fill_rect(surface,0,0,720,surface_y,root_color);
		osd_fill_rect(surface2,0,0,720,surface_y,root_color);
		mvpw_set_timer(vnc_widget, mvp_timer_callback, 1000);
		break;

	}
	osd_visible = state;
	pthread_mutex_unlock(&mymut);

}


void SetSurface(void)
{
	pthread_mutex_lock(&mymut);
	osd_blit(surface2,0,0,surface,0,0,720,surface_y);
	osd_blit(surface,0,0,surface_blank,0,0,720,surface_y);      
	pthread_mutex_unlock(&mymut);
}


void RestoreSurface(void)
{
	pthread_mutex_lock(&mymut);
	osd_blit(surface,0,0,surface3,0,0,720,surface_y);
	osd_blit(surface2,0,0,surface3,0,0,720,surface_y);
	pthread_mutex_unlock(&mymut);
}


void ClearSurface(osd_surface_t *sfc)
{
	int      x,y;

	for ( x = 0; x < 720; x++ ) {
		for ( y = 0; y < surface_y; y++ ) {
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


void PauseDisplayState(void)
{
	osd_blit(surface2,0,0,surface,0,0,720,surface_y);
}

long long mvp_file_size(void)
{
	PRINTF("Video size %lld\n",mystream.current_position);
	return mystream.current_position;
}


int mvp_file_read(char *buf, int len)
{
	int data, tslen = 0;
	tslen = 0;
	/*
	if (mvp_state==EMU_STEP && len > mystream.queued) {
	    len = mystream.queued;
	    if (len==0) {
		return 0;
	    }
	}
	*/
	do {
		data = read(fd, buf+tslen, len-tslen);
		if (data <= 0) {
			if (data == -1 && (errno==EAGAIN)) {
				continue;
			}
			if (tslen==0) {
				tslen = data;
			}
			break;
		} else {
			tslen+=data;
			if (mvp_state==EMU_STEP) {
				if ( mystream.queued == (mystream.out_position+tslen) ) {
					break;
				}

			}
		}
	} while (tslen < len && jumping==0 );
	if (tslen>0) {
		mystream.out_position+=tslen;
	}
	//printf("%lx %lx %x %x %x %x %x\n",mystream.queued,mystream.out_position,tslen,buf[0],buf[1],buf[2],buf[3]);
	return tslen;
}

void mvp_timer_callback(mvp_widget_t *widget)
{
	static int i=0;
	if ( mystream.mediatype==TYPE_VIDEO  ) {
		if (is_stopping==EMU_RUNNING || osd_visible == 0) {
			RDCSendPing();
		} else {
			SendIncrementalFramebufferUpdateRequest();
		}
		i = 0;
	} else {
		if (i==1) {
			SendIncrementalFramebufferUpdateRequest();
			i=0;
		} else {
			i++;
//            SendIncrementalFramebufferUpdateRequest();
			RDCSendPing();
			needPing = 1;
		}
	}
}

void demux_state(demux_handle_t *handle, unsigned int value);

long long mvp_seek(long long how_much, int whence)
{
	/*
	av_ffwd();
	av_stop();
	av_reset();
	av_play();
	printf("how much %lld\n",how_much);
	int data;
	int len;
	char tsbuf_static[100000]; 
	long long rc;    
	av_mute();
	do {
	    if (how_much > 100000) {
		len = 100000;
	    } else {
		len = how_much;
	    }
	    data = read(fd, tsbuf_static, len);
	    printf("data %d %lld\n",data,how_much);
	    if (data <= 0) {
		if (data == -1 && (errno==EAGAIN ) ) {
		    continue;
		}
		if (data==0) {                
		    //mystream.out_position = ;
		}
		break;
	    } else {
		how_much-=data;
	    }
	} while ( how_much > 0);
	printf("out queued %ld %ld\n",mystream.out_position,mystream.queued);
	rc = mystream.out_position;
	*/
	close(fd);
	mystream.out_position = 0;
	fd = open(MVP_NAMED_PIPE, O_RDONLY);

//    demux_state(handle,1);
//    av_reset();
//    mvpw_show(ffwd_widget);
//    timed_osd(seek_osd_timeout*1000);

//    tsmode = TS_MODE_UNKNOWN;
	return 0;
}

void RectangleUpdateHauppaugeAYVU(int x0, int y0, int w, int h,  unsigned char *buf,unsigned char *buf1)
{
	int   x,y;
	unsigned char   a1,a2,y1,y2,u,v;
	unsigned char *ptr = buf;
	unsigned char *ptr1 = buf1;

	if (osd_visible==0) {
//        osd_fill_rect(surface2,0,0,720,surface_y,MVPW_TRANSPARENT);
		return;
	}
	PRINTF("AYUVMode8 update %d %d %d %d\n",x0,y0,w,h);

	if (useHauppageExtentions == 1 ) {
		ptr1+=x0 + w*(y0*2);
	}
	for ( y = y0; y < y0 + h; y++ ) {
		for ( x = x0; x < x0 +w ; x+= 2 ) {
			a1 = *ptr1++;
			a2 = *ptr1++;
			y1 = *ptr++;
			u = *ptr++;
			v = *ptr++;
			y2 = *ptr++;
			osd_draw_pixel_ayuv(surface2,x,y,a1,y1,u,v);
			osd_draw_pixel_ayuv(surface2,x+1,y,a2,y2,u,v);
		}
	}
}

void mvp_fdinput_callback(mvp_widget_t *widget, int fd)
{
	if (!HandleRFBServerMessage()) {
		printf("Error updating screen\n");
		mvp_key_callback(widget, MVPW_KEY_POWER);       
	}
}

int media_seek(int value)
{
	int rc;
	long offset=0,offset1;

	if (mystream.queued) {
		offset = mystream.queued-mystream.out_position;
	}

	if ( offset < 0 ) {
		offset = mystream.blocklen;
	} else if (offset > mystream.length) {
		offset = 0;
	}
	offset1 = (value * mystream.bps * 100);
	if ( offset1 < 0 && value > 0 ) {
		offset1 = 0;
	} else if (offset1 > mystream.length) {
		offset1 = mystream.current_position;
	}

	PRINTF("%lld %ld ",mystream.current_position,offset);
	rc = mystream.current_position - offset + offset1;
	if (rc < 0) {
		rc = 0;
	}
	PRINTF("%d\n",rc);
	/*
	    demux_attr_t *attr;
	    av_stc_t stc;
	    attr = demux_get_attr(handle);
	av_current_stc(&stc);
	int rc1 = attr->bps;
	printf("Go %d %d %d\n",value,rc1,mystream.bps*100);
	*/
	return rc;
}

Bool HandlerfbHauppaugeOsd(void)
{
	char rfbOsdHeader[20];
	if (!ReadExact(rfbsock,rfbOsdHeader,20))
		return False;
	char *osd;
	osd = &rfbOsdHeader[6];
	int value;
	BUF_TO_INT16(value,osd);
	printf("Hauppauge OSD.BIN %d %x\n",value,rfbOsdHeader[0]);
	osd = (char *) malloc(value*sizeof(char));
	ReadExact(rfbsock,osd,value);
	free(osd);
	return True;
}

int displayOSDFile(char *filename)
{
	struct osdHeader {
		short x;
		short y;
		short w;
		short h;
		int yuv_len;
		int alpha_len;
	}osdHeader;

	// function to read and display osd files passed in osd.bin

	char *buf;
	char *buf1;
	FILE *infile;
	infile = fopen(filename,"rb");
	printf("%x\n",fread((char*)&osdHeader,1,sizeof(struct osdHeader),infile));
	buf = (char *)malloc(osdHeader.yuv_len*sizeof(char));
	printf("%x\n",fread(buf,sizeof(char),osdHeader.yuv_len,infile));
	buf1 = (char *)malloc(osdHeader.alpha_len*sizeof(char));
	printf("%x\n",fread(buf1,sizeof(char),osdHeader.alpha_len,infile));
	fclose(infile);
	printf("%x %x %x %x\n",osdHeader.x,osdHeader.y,osdHeader.w,osdHeader.h);
	RectangleUpdateHauppaugeAYVU(osdHeader.x,osdHeader.y,osdHeader.w,osdHeader.h,(unsigned char *)buf,(unsigned char *)buf1);
	free(buf);
	free(buf1);
	osd_blit(surface,0,0,surface2,0,0,720,surface_y);
	return 0;
}

Bool HandlePing(void)
{
	char msg;
	pthread_mutex_lock(&mymut);
	if (!ReadExact(rfbsock, (char *)&msg, 1)) {
		pthread_mutex_unlock(&mymut);
		return False;
	}
	if (msg!=0) {
		printf("Ping error %x\n",msg);
	} else {
		if ( mystream.mediatype!=TYPE_VIDEO  && needPing == 0 ) {
			printf("Received Ping\n");
			RDCSendPing();
		}
		needPing = 0;
	}
	pthread_mutex_unlock(&mymut);
	return True;
}
