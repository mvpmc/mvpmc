/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *  
 *   Based on MediaMVP VNC Viewer
 *   http://www.rst38.org.uk/vdr/mediamvp
 *
 *   Copyright (C) 2005, Dominic Morris
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

#ifndef __EMULATE_H__
#define __EMULATE_H__


/* Some sub-commands of RFB_MEDIA and RFB_MEDIA_ACK - these are used
 by the ack functions
*/
#define RDC_PLAY            0x01
#define RDC_PAUSE           0x02
#define RDC_STOP            0x03
#define RDC_REWIND          0x04
#define RDC_FORWARD         0x05
#define RDC_VOLUP           0x06
#define RDC_VOLDOWN         0x07
#define RDC_MENU            0x08          /* Used by displayon/off calls */
#define RDC_MUTE            0x09
#define RDC_SETTINGS        0x0a
#define RDC_DISPLAY_ON      0x0b          /* Fake */
#define RDC_DISPLAY_OFF     0x0c          /* Fake */

#define RDC_SKIP            0x0d          /* Fake */
#define RDC_BACK            0x0e          /* Fake */
#define RDC_SEEK_PERCENT    0x0f          /* Fake */

#define RFB_PONG            0x10          /* Fake - internal */
#define RDC_MESSAGE_MAX     0x11          /* End marker */

#define RDC_SETTINGS_GET    0x00
#define RDC_SETTINGS_TEST   0x01
#define RDC_SETTINGS_CANCEL 0x02
#define RDC_SETTINGS_SAVE   0x03

#endif


/* Some handy memory macros */

#define MALLOC(x)               malloc(x)
#define REALLOC(x,y)            realloc(x,y)
#define CALLOC(x,y)             calloc(x,y)
#define FREE(x)                 free(x)
#define FREENULL(x)             if(x) { FREE(x); (x) = NULL; }
#define STRDUP(x)               strdup(x)


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

#define INT64_TO_BUF(src,dest) \
(dest)[0] = (((src) >> 56) & 0xff); \
(dest)[1] = (((src) >> 48) & 0xff); \
(dest)[2] = (((src) >> 40) & 0xff); \
(dest)[3] = (((src) >> 32) & 0xff); \
(dest)[4] = (((src) >> 24) & 0xff); \
(dest)[5] = (((src) >> 16) & 0xff); \
(dest)[6] = (((src) >> 8) & 0xff); \
(dest)[7] = ((src) & 0xff); \
(dest) += 8;

#define BUF_TO_INT16(dest,src) \
(dest) = (((unsigned char)(src)[0] << 8) | (unsigned char)(src)[1]); \
(src) += 2;

#define BUF_TO_INT32(dest,src) \
(dest) = ( ((unsigned char)(src)[0] << 24) | ((unsigned char)(src)[1] << 16) |((unsigned char)(src)[2] << 8) | (unsigned char)(src)[3]); \
(src) += 4;

#define BUF_TO_INT64(dest,src) \
(dest) = ( ((unsigned char)(src)[0] << 56) | ((unsigned char)(src)[1] << 48) |((unsigned char)(src)[2] << 40) | ((unsigned char)(src)[3] << 32) | ((unsigned char)(src)[4] << 24) | ((unsigned char)(src)[5] << 16) |((unsigned char)(src)[6] << 8) | (unsigned char)(src)[7]); \
(src) += 8;


#include <sys/types.h>

#define MEDIA_REQUEST    2
#define MEDIA_STOP       3
#define MEDIA_BLOCK      4
#define MEDIA_STEP       5
#define MEDIA_SEEK       7
#define MEDIA_8          8


#define TYPE_VIDEO     0x01
#define TYPE_AUDIO     0x02


typedef struct {
    int       sock;
    int       rfbsock;
    int       directsock;
    char     *uri;
    int64_t   length;
    int64_t   current_position;
    u_int8_t   last_command;
    int32_t   blocklen;
    int       outfd;
    u_int8_t   mediatype;
    int        socks[2];
    char      *inbuf;
    int        inbuflen;
    int        inbufpos;
    char       fileid[2];
} stream_t;

extern stream_t mystream;

