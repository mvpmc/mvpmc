/*
 *  Copyright (C) 2007, Jon Gettler
 *  http://www.mvpmc.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

typedef enum {
	HTTP_FILE_CLOSED=0,
	HTTP_FILE_ERROR,
	HTTP_FILE_DONE,
	HTTP_FILE_UNKNOWN,
	HTTP_AUDIO_FILE_OGG,
	HTTP_VIDEO_FILE_MPG,
	HTTP_AUDIO_FILE_FLAC,
	HTTP_IMAGE_FILE_JPEG,
} http_content_t;

// note using Winamp now for testing

#define  GET_STRING     "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: WinampMPEG/5.1\r\nAccept: */*\r\nIcy-MetaData:1\r\nConnection: close\r\n\r\n"
#define  GET_OGG_STRING "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: WinampMPEG/5.1\r\nAccept: */*\r\nConnection: close\r\n\r\n"
#define  GET_M3U "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: WinampMPEG/5.1\r\nAccept: */*\r\n\r\n"
#define  GET_LIVE365 "GET /cgi-bin/api_login.cgi?action=login&org=live365&remember=Y&member_name=%s HTTP/1.0\r\nUser-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8) Gecko/20051111 Firefox/1.5 \r\nHost: www.live365.com\r\nAccept: */*\r\nConnection: Keep-Alive\r\n\r\n"

#define  GET_ASF_STRING "GET %s HTTP/1.0\r\nAccept: */*\r\nUser-Agent: NSPlayer/10.0.0.3802\r\nHost: %s\r\n%sPragma: no-cache,rate=1.000,stream-time=0,stream-offset=0:0,max-duration=0\r\nPragma: xClientGUID={3300AD50-2C39-46c0-AE0A-da6f4029b4c09b70}\r\nConnection: Close\r\n\r\n"

#define SHOUTCAST_DOWNLOAD "http://www.shoutcast.com/sbin/newxml.phtml?"

#define SHOUTCAST_TUNEIN "http://www.shoutcast.com/sbin/tunein-station.pls?id=%s"

#define  MAX_URL_LEN 275
#define  MAX_PLAYLIST 5
#define  MAX_META_LEN 256
#define  STREAM_PACKET_SIZE  1448

extern int bufferFull;
extern char bitRate[];
extern unsigned long contentLength;
extern unsigned long bytesRead;
extern int using_helper;
extern http_content_t http_playing;

extern int fd;

extern int mplayer_disable;

int mplayer_helper_connect(FILE *outlog,char *url,int stopme);

void content_osd_update(mvp_widget_t *widget);

int is_streaming(char *url);

http_content_t http_main(void);
