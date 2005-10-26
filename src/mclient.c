/*
 *  $Id$
 *
 *  Copyright (C) 2005, Paul Warren <pdw@ex-parrot.com>
 *  http://mvpmc.sourceforge.net/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundtion; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "mvpmc.h"

#include <sys/un.h>

#include <unistd.h>
#include <sys/utsname.h>

#include <fcntl.h>
#include <errno.h>

#include "mclient.h"


/******************************
 * Music Client
 ******************************/

#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <locale.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

#include "display.h"

/*
 * Added to obtain MAC address.
 */
#include <net/if.h>
#include <sys/ioctl.h>

#define SERVER_PORT    3483
#define CLIENT_PORT    34443

#define STATE_STARTUP 0

#define RECV_BUF_SIZE 65536
#define OUT_BUF_SIZE  131072
#define DISPLAY_SIZE 128
#define LINE_LENGTH 40
#define LINE_COUNT 2

int mclient_type;

int mclient_socket;

static pthread_t mclient_loop_thread_handle;
pthread_cond_t mclient_cond = PTHREAD_COND_INITIALIZER;

typedef struct {
  char type;
  char reserved1;
  unsigned short wptr;
  unsigned short rptr;
  char reserved2[12];
} request_data_struct;

typedef struct {
  char type;
  char zero;
  /* optimizer wants to put two bytes here */
  unsigned long time;/* since startup, in 625kHz ticks = 0.625 * microsecs */
  char codeset; /* 0xff for JVC */
  char bits; /* 16 for JVC */
  /* optimizer wants to put two bytes here */
  unsigned long code;
  char mac_addr[6];
} __attribute__((packed)) send_ir_struct; /* be smarter than the optimizer */

typedef struct {
  char type;
  char control;
  char reserved1[4];
  unsigned short wptr;
  char reserved2[2];
  unsigned short seq;
  char reserved3[6];
} receive_mpeg_header;

typedef struct {
  char type;
  char reserved1[5];
  unsigned short wptr;
  unsigned short rptr;
  unsigned short seq;
  char mac_addr [6];
} packet_ack;

typedef struct {
  void * buf;
  int head;
  int tail;
  int size;
} ring_buf;

/* lookup table to convert the VFD charset to Latin-1 */
/* (Sorry, other character sets not supported) */
char vfd2latin1[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 00 - 07 */
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, /* 08 - 0f */
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, /* 10 - 17 */
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, /* 18 - 1f */
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, /* 20 - 27 */
  0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, /* 28 - 2f */
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, /* 30 - 37 */
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, /* 38 - 3f */
  0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* 40 - 47 */
  0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* 48 - 4f */
  0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* 50 - 57 */
  0x58, 0x59, 0x5a, 0x5b, 0xa5, 0x5d, 0x5e, 0x5f, /* 58 - 5f */
  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, /* 60 - 67 */
  0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, /* 68 - 6f */
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, /* 70 - 77 */
  0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0xbb, 0xab, /* 78 - 7f */
  0xc4, 0xc3, 0xc5, 0xe1, 0xe5, 0x85, 0xd6, 0xf6, /* 80 - 87 */
  0xd8, 0xf8, 0xdc, 0x8b, 0x5c, 0x8d, 0x7e, 0xa7, /* 88 - 8f */
  0xc6, 0xe6, 0xa3, 0x93, 0xb7, 0x6f, 0x96, 0x97, /* 90 - 97 */
  0xa6, 0xc7, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, /* 98 - 9f */
  0xa0, 0xa1, 0xa2, 0xac, 0xa4, 0xb7, 0xa6, 0xa7, /* a0 - a7 */
  0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, /* a8 - af */
  0x2d, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb1, /* b0 - b7 */
  0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, /* b8 - bf */
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, /* c0 - c7 */
  0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, /* c8 - cf */
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, /* d0 - d7 */
  0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xa8, 0xb0, /* d8 - df */
  0xe0, 0xe4, 0xdf, 0xe3, 0xb5, 0xe5, 0x70, 0x67, /* e0 - e7 */
  0xe8, 0xe9, 0x6a, 0xa4, 0xa2, 0xed, 0xf1, 0xf6, /* e8 - ef */
  0x70, 0x71, 0xf2, 0xf3, 0xf4, 0xfc, 0xf6, 0xf7, /* f0 - f7 */
  0xf8, 0x79, 0xfa, 0xfb, 0xfc, 0xf7, 0xfe, 0xff  /* f8 - ff */
};

/*
 * Global pointer to structure containning alloc'd memory for
 * local buffer and other buffer related info.
 */
ring_buf * outbuf;
/*
 * Global pointer to alloc'd memory for data received from 
 * the client.
 */
void * recvbuf;

int playmode = 3;

static int debug = 0;

/*
 * Default is to enable the display.
 */
int display = 1;

char *server_name;

struct in_addr *server_addr_mclient = NULL;

char slimp3_display[DISPLAY_SIZE];

/* Mutex for sending on socket */
pthread_mutex_t mclient_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 
 * Local flag to keep track of paused state.
 */
int local_paused;

struct timeval uptime; /* time we started */

void mclient_audio_play(mvp_widget_t *widget);

/*
 * Need the MAC to uniquely identify this mvpmc box to
 * mclient server.
 */
unsigned char *mac_address_ptr;
struct ifreq ifr;

ring_buf* ring_buf_create(int size)
{
  ring_buf * b;
  b = malloc(sizeof(ring_buf));
  b->buf = (void*)calloc(size, 1); 
  b->size = size;
  b->head = b->tail = 0;
  return b;
}


void send_packet(int s, char *b, int l) {
  struct sockaddr_in ina;

  ina.sin_family = AF_INET;
  ina.sin_port = htons(SERVER_PORT);
  ina.sin_addr = *server_addr_mclient; 

  /* Protect from multiple sends */
  pthread_mutex_lock(&mclient_mutex);

  if(sendto(s, b, l, 0, (const struct sockaddr*)&ina, sizeof(ina)) == -1) {
    if(debug) printf("mclient:Could not send packet\n");
  };
  pthread_mutex_unlock(&mclient_mutex);
}


void send_discovery(int s) {
  char pkt[18];

  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 'd';
  pkt[2] = 1;
  pkt[3] = 0x11;

  memcpy(&pkt[12], mac_address_ptr, 6);

  if(debug) printf("mclient: Sending discovery request.\n");

  send_packet(s, pkt, sizeof(pkt));
}


void send_ack(int s, unsigned short seq)
{
  packet_ack pkt;

  memset(&pkt, 0, sizeof(pkt));

  pkt.type = 'a';

  pkt.wptr = htons(outbuf->head >> 1);
  pkt.rptr = htons(outbuf->tail >> 1);

  pkt.seq = htons(seq);

  memcpy(pkt.mac_addr, mac_address_ptr, 6);

  if(debug)
    {
      printf("\nmclient:pkt.wptr:%8.8d pkt.rptr:%8.8d handle:%d\n", pkt.wptr, pkt.rptr, s);
      printf("=> sending ack for %d\n", seq); 
    }

  send_packet(s, (void*)&pkt, sizeof(request_data_struct));
}


void say_hello(int s) {
  char pkt[18];

  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 'h';
  pkt[1] = 1;
  pkt[2] = 0x11;

  memcpy(&pkt[12], mac_address_ptr, 6);

  send_packet(s, pkt, sizeof(pkt));
}


void send_ir(int s, char codeset, unsigned long code, int bits) {
  send_ir_struct pkt;
  struct timeval now;
  struct timezone tz;
  struct timeval diff;
  unsigned long usecs;
  unsigned long ticks;

  gettimeofday(&now, &tz);
  now.tv_sec -= 60 * tz.tz_minuteswest; /* canonicalize to GMT/UTC */
  if (now.tv_usec < uptime.tv_usec) {
    /* borrowing */
    now.tv_usec += 1000;
    now.tv_sec -= 1;
  }
  diff.tv_usec = now.tv_usec - uptime.tv_usec;
  diff.tv_sec = now.tv_sec - uptime.tv_sec;
  usecs = diff.tv_sec * 1000000L + diff.tv_usec;
  ticks = (unsigned int)(0.625 * (double)usecs);

  memset(&pkt, 0, sizeof(send_ir_struct));
  pkt.type = 'i';
  pkt.zero = 0;
  pkt.time = htonl(ticks);
  pkt.codeset = codeset;
  pkt.bits = (char)bits;
  pkt.code = htonl(code);
  memcpy(pkt.mac_addr, mac_address_ptr, 6);

  if(debug) printf("=> sending IR code %lu at tick %lu (%lu usec)\n",
		   code, ticks, usecs);

  send_packet(s, (void*)&pkt, sizeof(pkt));
}


unsigned long curses2ir(int key) {
  unsigned long ir = 0;

  switch(key)
    {
    case MVPW_KEY_ZERO: ir = 0x76899867; break;
    case MVPW_KEY_ONE: ir = 0x7689f00f; break;
    case MVPW_KEY_TWO: ir = 0x768908f7; break;
    case MVPW_KEY_THREE: ir = 0x76898877; break;
    case MVPW_KEY_FOUR: ir = 0x768948b7; break;
    case MVPW_KEY_FIVE: ir = 0x7689c837; break;
    case MVPW_KEY_SIX: ir = 0x768928d7; break;
    case MVPW_KEY_SEVEN: ir = 0x7689a857; break;
    case MVPW_KEY_EIGHT: ir = 0x76896897; break;
    case MVPW_KEY_NINE: ir = 0x7689e817; break;
    case MVPW_KEY_DOWN: ir = 0x7689b04f; break; /* arrow_down */
    case MVPW_KEY_LEFT: ir = 0x7689906f; break; /* arrow_left */
    case MVPW_KEY_RIGHT: ir = 0x7689d02f; break; /* arrow_right */
    case MVPW_KEY_UP: ir = 0x7689e01f; break; /* arrow_up */
#if 0
    case MVPW_KEY_VOL_DOWN: ir = 0x768900ff; break; /* voldown */
    case MVPW_KEY_VOL_UP: ir = 0x7689807f; break; /* volup */
#endif
    case MVPW_KEY_REWIND: ir = 0x7689c03f; break; /* rew */
    case MVPW_KEY_PAUSE: ir = 0x768920df; break; /* pause */
    case MVPW_KEY_SKIP: ir = 0x7689a05f; break; /* fwd */
    case MVPW_KEY_FFWD: ir = 0x7689a05f; break; /* fwd */
    case MVPW_KEY_OK: ir = 0x768910ef; break; /* play */
    case MVPW_KEY_PLAY: ir = 0x768910ef; break; /* play */
    case MVPW_KEY_MENU: ir = 0x76897887; break; /* jump to now playing menu */
    case MVPW_KEY_REPLAY: ir = 0x768938c7; break; /* cycle through repeat modes */

    case MVPW_KEY_RED: ir = 0x768940bf; break; /* power */
    case MVPW_KEY_GREEN: ir = 0x7689d827; break; /* cycle through shuffle modes */
    case MVPW_KEY_BLUE: ir = 0x7689807f; break; /* volup */
    case MVPW_KEY_YELLOW: ir = 0x768900ff; break; /* voldown */

   /*
    * JVC remote control codes.
    */
    case MVPW_KEY_STOP: ir = 0x0000f7c2; break; /* stop */

   /*
    * Special keys we can process here.
    */
    case MVPW_KEY_MUTE:
       if (av_mute() == 1)
          mvpw_show(mute_widget);
       else
          mvpw_hide(mute_widget);
       break;

      /*
       * Uncomment if want to toggle mclient debug printfs (lots of them!).
       */
      ///    case MVPW_KEY_RECORD: debug = !debug; break; /* toggle debug mode */

      /*
       * Keys that may not make sense for mvpmc.
       */
      ///    case '!': ir = 0x768940bf; break; /* power */
      ///    case 's': ir = 0x7689b847; break; /* sleep */
      ///    case '+': ir = 0x7689f807; break; /* size */
      ///    case '*': ir = 0x768904fb; break; /* brightness */
      ///    case    : ir = 0x768958a7; break; /* jump to search menu */
      ///    case    : ir = 0x7689609f; break; /* add, NOTE: if held = zap */
      ///    case    : ir = 0x7689d827; break; /* cycle through shuffle modes */
    case MVPW_KEY_VOL_UP:
    case MVPW_KEY_VOL_DOWN:
	    volume_key_callback(volume_dialog, key);
	    mvpw_show(volume_dialog);
	    mvpw_set_timer(volume_dialog, mvpw_hide, 3000);
	    break;
    }
    if (ir != 0) send_ir(mclient_socket, 0xff , ir, 16);

    return ir;
}


void receive_mpeg_data(int s, receive_mpeg_header* data, int bytes_read)
{
  static int seq_history = 0;
  int message_length = 0;

  if(debug) 
    {
      printf("mclient:     Address:%8.8d Cntr:%8.8d     Seq:%8.8d  BytesIn:%8.8d\n", ntohs(data->wptr), data->control, ntohs(data->seq), bytes_read);

      if(seq_history == ntohs(data->seq))
	{
	  printf("mclient:Sequence says - NO  data\n");
	}
      else
	{
	  printf("mclient:Sequence says - NEW data\n");
	}

      if(outbuf->head == (ntohs(data->wptr) << 1))
	{
	  printf("mclient:Server and Client pntrs - AGREE\n");
	}
      else
	{
	  printf("mclient:Server and Client pntrs - DIFFERENT (by:%d)\n",outbuf->head - ((ntohs(data->wptr)) << 1));
	}
    }

  /*
   * Set tail pointer to value received from server.
   */
  outbuf->head = ntohs((data->wptr) << 1 );

  /*
   * Store play mode into global variable.
   */
  playmode = data->control;

  /*
   * Must be some header bytes we need to get rid of.
   */
  message_length = bytes_read - 18;

  /*
   * If this is a new sequence (new data) then
   * add it to the input buffer.
   */
  if(seq_history != ntohs(data->seq))
    {
      /*
       * Keep history of seq so next time
       * we are here we can tell if
       * we are getting new data that should be 
       * processed.
       */
      seq_history = ntohs(data->seq);

      /*
       * Check if there is room at the end of the buffer for the new data.
       */
      if((outbuf->head + message_length) <= OUT_BUF_SIZE)
	{
	  /*
	   * Put data into the rec buf.
	   */
	  memcpy((outbuf->buf + outbuf->head), (recvbuf + 18),                    message_length);

	  /*
	   * Move head by number of bytes read.
	   */
	  outbuf->head += message_length;
	}
      else
	{
	  /*
	   * If not, then split data between the end and beginning of
	   * the buffer.
	   */
	  memcpy((outbuf->buf + outbuf->head), (recvbuf + 18), (OUT_BUF_SIZE - outbuf->head));
	  memcpy(outbuf->buf,(recvbuf + 18 + (OUT_BUF_SIZE - outbuf->head)), (message_length - (OUT_BUF_SIZE - outbuf->head)));

	  /*
	   * Move head by number of bytes written from the beginning of the buffer.
	   */
	  outbuf->head = (message_length - (OUT_BUF_SIZE - outbuf->head));
	}
    }
  /*
   * Send ack back to server.
   */
  send_ack(s, ntohs(data->seq));
}


void send_mpeg_data(int s, receive_mpeg_header* data)
{
  int amount_written = 0;
  int afd;

  /*
   * Play control (play, pause & stop).
   */
  switch(data->control)
    {
    case 0:
      /*
       * PLAY: Decode data.
       */

      /*
       * Un-Pause the mvpmc so it will start playing anything that is or
       * will be copied into the mvpmc buffer.
       */
      if(local_paused == 1)
	{
	  local_paused = av_pause();
	  if(debug)
	    {
	      printf("mclient:UN-PAUSE returned:%d\n",local_paused);
	    }
	}

      /*
       * Get file descriptor of hardware.
       */
      afd = av_audio_fd();

      /*
       * Problems opening the audio hardware?
       */
      if(afd < 0)
	{
	  outbuf->head = 0;
	  outbuf->tail = 0;
	  if(debug) printf("mclient:Problems opening the audio hardware.\n");
	}
      else
	{
	  /*
	   * If there is room, wirte data in the input buffer to
	   * the audio device.
	   *
	   * Careful, if the head pointer (in) has wrapped and the tail (out) pointer
	   * has not, the data is split between the end and then the beginning of the 
	   * ring buffer.
	   */
	  if(outbuf->head < outbuf->tail)
	    {
	      /*
	       * Data is split, use the end of the buffer before going back to the 
	       * beginning.
	       */
	      if((amount_written=write(afd, outbuf->buf + outbuf->tail, OUT_BUF_SIZE - outbuf->tail)) == 0)
		{
		  /*
		   * Couldn't use the end of the buffer.  We know the buffer is not
		   * empty from above (outbuf->head < (or !=) outbuf->tail), so the hardware
		   * must be full. Go into an idle mode.
		   */
		  if(debug) 
		    {
		      printf("mclient:The audio output device is full or there is nothing to write (1).\n");
		    }
		}
	      else
		{
		  /// Just in case...
		  if(amount_written < 0)
		    {
		      printf("mclinet:WARNING (1), the write returned a negative value:%d\n",amount_written);
		    }
		  /*
		   * Data is split and we were able to use some or all of
		   * the data at the end of the ring buffer.  Find out which
		   * the case might be.
		   */
		  if(amount_written == (OUT_BUF_SIZE - outbuf->tail))
		    {
		      /*
		       * All the data at the end of the buffer was written.  Now,
		       * adjust the pointers and start writing from the beginning
		       * of the buffer.
		       */
		      outbuf->tail = 0;

		      if((amount_written = write(afd, outbuf->buf, outbuf->head)) == 0)
			{
			  /*
			   * Couldn't use the start of the buffer.  The hardware is
			   * full of data.
			   */
			  if(debug) 
			    {
			      printf("mclient:The audio output device is full or there is nothing to write (2).\n");
			    }
			}
		      else
			/// Just in case...
			if(amount_written < 0)
			  {
			    printf("mclinet:WARNING (2), the write returned a negative value:%d\n",amount_written);
			  }
		      /*
		       * We were able to use some or all of
		       * the data.  Find out which the case might be.
		       */
		      if(amount_written == outbuf->head)
			{
			  /*
			   * We wrote all of it.
			   */
			  outbuf->tail = outbuf->head;
			}
		      else
			{
			  /*
			   * The amount written was not zero and it was not up to the 
			   * head pointer.  Adjust the tail to only account for the 
			   * portion of the buffer consumed.
			   */
			  outbuf->tail = amount_written;
			}
		    }
		  else
		    {
		      /*
		       * The amount written was not zero and it was not up to the 
		       * end of the buffer.  Adjust the tail to only account for the 
		       * portion of the buffer consumed.
		       */
		      outbuf->tail += amount_written;
		    }
		}
	    }
	  else
	    {
	      if((amount_written = write(afd, outbuf->buf + outbuf->tail, outbuf->head - outbuf->tail)) == 0)
		{
		  if(debug) 
		    {
		      printf("mclient:The audio output device is full or there is nothing to write (3).\n");
		    }
		}
	      else
		{
		  /// Just in case...
		  if(amount_written < 0)
		    {
		      printf("mclinet:WARNING (3), the write returned a negative value:%d\n",amount_written);
		    }
		  /*
		   * We wrote something:
		   * Move the tail of the ring buffer up to show the server
		   * we are actually using data.
		   */
		  outbuf->tail += amount_written;
		}
	    }
	}
      break;

    case 1:
      /*
       * PAUSE: Do not play, and do not reset buffer.
       */

      /*
       * Pause the mvpmc so as not to continue to play what has already
       * been copied into the mvpmc buffer.
       */
      if(local_paused == 0)
	{
	  local_paused = av_pause();
	  if(debug)
	    {
	      printf("mclient:UN-PAUSE returned:%d\n",local_paused);
	    }
	}
      break;

    case 3:
      /*
       * STOP: Do not play, and reset head to beginning of buffer.
       */
      av_reset();
      outbuf->tail = 0;
      break;

    default:
      break;
    }
}


void 
mclient_idle_callback(mvp_widget_t *widget)
{
  static int doubletime = 100;
  static char oldstring[140] = "OriginalString";
  static int send_display_data_state = 0;
  char	newstring[140];


  pthread_mutex_lock(&mclient_mutex);

  /*
   * Only use the first 40 characters.  Looks like the server
   * leaves old characters laying round past the 40th character.
   */
  sprintf(newstring,"%40.40s\n%40.40s\n",slimp3_display,&slimp3_display[64]);
  pthread_mutex_unlock(&mclient_mutex);

  /*
   * Set the call back for the slower 100ms interval.
   */
  doubletime = 100;

  if(strcmp(newstring,oldstring) != 0)
    {
      /*
       * If we are looking a new title data, reduce the 
       * call back interval to a faster 10ms (in anticipation
       * of the need to handle scrolling data).
       *
       * Send text to OSD.
       */
      doubletime = 10;
      mvpw_set_dialog_text(mclient,newstring);
    }

  /*
   * Send text to VFD.
   *
   * Only send data when Line1 has changed.  We don't want to send
   * text when the server is scrolling text on Line2.
   */
  if((strncmp(newstring ,oldstring, 40) != 0) && (send_display_data_state == 0))
    {
      if(debug) printf("mclient:TEST:new&old are diff new:%s old:%s state:%d\n",newstring,oldstring,send_display_data_state);
      /*
       * But, wait until server animation has stopped before deciding
       * to send data.
       */
      send_display_data_state = 1;
    }

  if((strncmp(newstring, oldstring, 40) == 0) && (send_display_data_state == 1))
    {
      if(debug) printf("mclient:TEST:new&old are same new:%s old:%s state:%d\n",newstring,oldstring,send_display_data_state);
      snprintf(display_message, sizeof(display_message),"Line1:%40.40s\n", &slimp3_display[0]);
      display_send(display_message); 

      snprintf(display_message, sizeof(display_message),"Line2:%40.40s\n", &slimp3_display[64]);
      display_send(display_message); 

      send_display_data_state = 0;
    }
  /*
   * Make copy of string to compair with next time 
   * through.
   */
  strncpy(oldstring,newstring,135);

  if(debug) if(doubletime==10) printf("mclient:Test:Double Time Activated.\n");
  mvpw_set_timer(mclient, mclient_idle_callback, doubletime);
}


void receive_display_data(char * ddram, unsigned short *data, int bytes_read) {
  unsigned short *display_data;
  int n;
  int addr = 0; /* counter */

  pthread_mutex_lock(&mclient_mutex);
  if (bytes_read % 2) bytes_read--; /* even number of bytes */
  display_data = &(data[9]); /* display data starts at byte 18 */

  for (n=0; n<(bytes_read/2); n++) {
    unsigned short d; /* data element */
    unsigned char t, c;

    d = ntohs(display_data[n]);
    t = (d & 0x00ff00) >> 8; /* type of display data */
    c = (d & 0x0000ff); /* character/command */
    switch (t) {
    case 0x03: /* character */
      c = vfd2latin1[c];
      if (!isprint(c)) c = ' ';
      if (addr <= DISPLAY_SIZE)
	ddram[addr++] = c;
      break;
    case 0x02: /* command */
      switch (c) {
      case 0x01: /* display clear */
	memset(ddram, ' ', DISPLAY_SIZE);
	addr = 0;
	break;
      case 0x02: /* cursor home */
      case 0x03: /* cursor home */
	addr = 0;
	break;
      case 0x10: /* cursor left */
      case 0x11: /* cursor left */
      case 0x12: /* cursor left */
      case 0x13: /* cursor left */
	addr--;
	if (addr < 0) addr = 0;
	break;
      case 0x14: /* cursor right */
      case 0x15: /* cursor right */
      case 0x16: /* cursor right */
      case 0x17: /* cursor right */
	addr++;
	if (addr > DISPLAY_SIZE) addr = DISPLAY_SIZE;
	break;
      default:
	if ((c & 0x80) == 0x80) {addr = (c & 0x7f);}
	break;
      }
    case 0x00: /* delay */
    default:
      break;
    }
  }
  pthread_mutex_unlock(&mclient_mutex);
}


void read_packet(int s) {
  struct sockaddr ina;
  struct sockaddr_in *ina_in = NULL;
  socklen_t slen = sizeof(struct sockaddr);
  int bytes_read;

  bytes_read = recvfrom(s, recvbuf, RECV_BUF_SIZE, 0, &ina, &slen);
  if (ina.sa_family == AF_INET) {
    ina_in = (struct sockaddr_in *)(&ina);
  }
  if(bytes_read < 1) {
    if (bytes_read < 0) {
      if (errno != EINTR)
	if(debug) printf("mclient:recvfrom\n");
    } else {
      printf("Peer closed connection\n");
    }
  }
  else if(bytes_read < 18) {
    printf("<= short packet\n");
  }
  else {
    switch(((char*)recvbuf)[0]) {
    case 'D':
      if(debug) printf("<= discovery response\n"); 
      say_hello(s);
      break;
    case 'h':
      if(debug) printf("<= hello\n");
      say_hello(s);
      break;
    case 'l':
      if(debug) printf("<= LCD data\n");
      if(display) 
	receive_display_data(slimp3_display, recvbuf, bytes_read);
      break;
    case 's':
      if(debug) printf("<= stream control\n");
      break;
    case 'm':
      if(debug) printf("<= mpeg data\n"); 
      receive_mpeg_data(s, recvbuf, bytes_read);
      break;
    case '2':
      if(debug) printf("<= i2c data\n"); 
      break;
    }
  }

}

void *
mclient_loop_thread(void *arg)
{
  int s;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  pthread_mutex_lock(&mutex);

  for(;;)
    {
      /*
       * Check when we get "turned on" (grab the GUI).
       */
      if(gui_state == MVPMC_STATE_MCLIENT)
	{

	  /*
	   * Let's try initializing mclient here.
	   */
	  if(debug) printf("mclient:Initializing mclient\n");

	  /*
	   * Grab the audio hardware.
	   */
	  switch_hw_state(MVPMC_STATE_MCLIENT);

	  mclient_local_init();

	  s = mclient_server_connect();

	  mclient_socket = s;
	  send_discovery(s);

          /*
	   * Set up the hardware to pass mp3 data.
	   * (Should only do once?...)
	   */
	  av_set_audio_output(AV_AUDIO_MPEG);
	  av_set_audio_type(0);
	  av_play();

         /*
          * Stay in loop processing server's audio data
          * until we give up the audio hardware.
          */
           while(hw_state == MVPMC_STATE_MCLIENT)
	    {

	      struct timeval mclient_tv;

	      int n = 0;

	      FD_ZERO(&read_fds);
	      FD_SET(s, &read_fds);
	      if(s > n)  n = s ;

	      /*
	       * Wait until we receive data from server or up to 100ms
	       * (1/10 of a second).
	       */
	      mclient_tv.tv_usec = 1000;

	      if(select(n + 1, &read_fds, NULL, NULL, &mclient_tv) == -1)
		{
		  if (errno != EINTR)
		    {
		      if(debug) printf("mclient:select error\n");
		      abort();
		    }
		}
	      /*
	       * Check if the "select" event could have been caused because data
	       * has been sent by the server.
	       */
	      if(FD_ISSET(s, &read_fds))
		{
		  read_packet(s);
		}

	      /*
	       * Regardless if we got here because of the "select" time out or receiving
	       * a message from the server (again a "select" event), check to see if we
	       * can send more data to the hardware, if there has been a remote control
	       * key press or if we have exited out of the music client.
	       */
	      send_mpeg_data(s, recvbuf);
	    }

	  /*
	   * Done, we got "turned off", so close the connection.
	   */
	  close(s);

          /*
           * Free up the alloc'd memory.
           */
           free(outbuf);
           free(recvbuf);
	} else {
		pthread_cond_wait(&mclient_cond, &mutex);
	}
    }
}

void 
mclient_local_init(void)
{
  struct hostent *h;
  struct timezone tz;
  static struct in_addr hostname_mclient;

  /*
   * Create the buffer and associated info to store data before
   * sending it out to the hardware.
   */
  outbuf = ring_buf_create(OUT_BUF_SIZE);

  /*
   * Create the buffer to store data from the server.
   */
  recvbuf = (void*)calloc(1, RECV_BUF_SIZE);

  h = gethostbyname((const char *)server_name);
  if(h == NULL) {
    printf("mclient:Unable to get address for %s\n", server_name);
    exit(1);
  }
  else
    {
      printf("mclient:Was able to get an address for:%s\n", server_name);
    }

  /*
   * Save address from gethostbyname structure to memory for 
   * later use.
   */
  memcpy(&hostname_mclient, h->h_addr_list[0], sizeof(hostname_mclient));
  server_addr_mclient = &hostname_mclient;

  memset(slimp3_display, ' ', DISPLAY_SIZE);
  slimp3_display[DISPLAY_SIZE] = '\0';

  setlocale(LC_ALL, ""); /* so that isprint() works properly */

  /* save start time for sending IR packet */
  gettimeofday(&uptime, &tz);
  uptime.tv_sec -= 60 * tz.tz_minuteswest; /* canonicalize to GMT/UTC */

  /*
   * Initialize pause state
   */
  local_paused = av_pause();
}

int 
mclient_server_connect(void) 
{
  int s;
  struct sockaddr_in my_addr;

  s = socket(AF_INET, SOCK_DGRAM, 0);

 /*
  * Get the MAC address for the first ethernet port.
  */
  strcpy(ifr.ifr_name,"eth0");
  ioctl(s,SIOCGIFHWADDR, &ifr);
  mac_address_ptr = (unsigned char*)ifr.ifr_hwaddr.sa_data;

  if(s == -1)
    {
      if(debug) printf("mclient:Could not get descriptor\n");
    }
  else
    {
      if(debug) printf("mclient:Was able get descriptor:%d\n",s);
    }

  my_addr.sin_family = AF_INET;         
  my_addr.sin_port = htons(CLIENT_PORT); 
  my_addr.sin_addr.s_addr = INADDR_ANY;
  memset(&(my_addr.sin_zero), '\0', 8); 
  if(bind(s, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)))
    {
      if(debug) printf("mclient:Unable to connect to descriptor endpoint:%d\n",s);
    }
  else
    {
      if(debug) printf("mclient:Was able to connect to descriptor endpoint:%d\n",s);
    }

  return s;
}

/*
 * Main routine for mclient.
 */
int music_client(void)
{
  server_name = mclient_server;

  printf("mclient:Starting mclient pthread.\n");
  pthread_create(&mclient_loop_thread_handle, &thread_attr_small, mclient_loop_thread, NULL);

  mvpw_set_dialog_text(mclient, "Update text lines....................... \n line 2................................... \n line 3................................... \n");

  return 0;
}

void
mclient_exit(void)
{
        audio_clear();
        av_stop();
}

