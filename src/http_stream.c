/*
*  Copyright (C) 2004,2005,2006,2007, Jon Gettler
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>   
#include <sys/socket.h>   
#include <fcntl.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>

#include "mvpmc.h"
#include "mclient.h"
#include "http_stream.h"

FILE *shoutOut;
char shoutcastDisplay[41];
char *stristr(char *str, char *substr);

typedef enum {
	CONTENT_MP3,
	CONTENT_PLAYLIST,
	CONTENT_OGG,
	CONTENT_MPG,
	CONTENT_PODCAST,
	CONTENT_UNKNOWN,
	CONTENT_REDIRECT,
	CONTENT_200,
	CONTENT_UNSUPPORTED,
	CONTENT_ERROR,
	CONTENT_TRYHOST,
	CONTENT_NEXTURL,
	CONTENT_GET_SHOUTCAST,
	CONTENT_AAC,
	CONTENT_DIVX,
	CONTENT_FLAC,
	CONTENT_JPEG,
} content_type_t;

typedef enum {
	PLAYLIST_SHOUT,
	PLAYLIST_M3U,
	PLAYLIST_PODCAST,
	PLAYLIST_ASX,
	PLAYLIST_ASX_REFERENCE,
	PLAYLIST_NONE,
	PLAYLIST_RA,
} playlist_type_t;

typedef enum {
	HTTP_INIT,
	HTTP_HEADER,
	HTTP_CONTENT,
	HTTP_RETRY,
	HTTP_RETRY_LATER,
	HTTP_UNKNOWN,
} http_state_type_t;

//=================================
ring_buf* ring_buf_create(int size);

void send_mpeg_data(void);

#define RECV_BUF_SIZE 65536
#define OUT_BUF_SIZE  131072

//=================================

// globals from mclient.c

extern int debug;
extern ring_buf * outbuf;
extern void * recvbuf;
extern int local_paused;

int http_read_stream(unsigned int socket,int metaInt,int offset);

void strencode( char* to, size_t tosize, const char* from );

void content_osd_update(mvp_widget_t *widget);
void http_buffer(int message_length,int offset);
int http_metadata(char *metaString,int metaWork,int metaData);
int create_shoutcast_playlist(int limit);


/* globals */

int bufferFull;
char bitRate[10];
unsigned long contentLength;
unsigned long bytesRead;
http_content_t http_playing;

void content_osd_update(mvp_widget_t *widget)
{
	av_stc_t stc;
	char buf[256];

	int percent;

	av_current_stc(&stc);
	snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d    %s",
		 stc.hour, stc.minute, stc.second,bitRate);
	mvpw_set_text_str(fb_time, buf);

	if (contentLength == 0) {
		snprintf(buf, sizeof(buf), "Bytes: %lu", bytesRead);
		mvpw_set_text_str(fb_size, buf);
		percent = (bufferFull* 100) /  OUT_BUF_SIZE;
		snprintf(buf, sizeof(buf), "Buf:");
	} else {
		snprintf(buf, sizeof(buf), "Bytes: %lu", contentLength);
		mvpw_set_text_str(fb_size, buf);
		percent = bytesRead /  (contentLength/100);
		snprintf(buf, sizeof(buf), "%d%%", percent);
	}
	mvpw_set_text_str(fb_offset_widget, buf);
	mvpw_set_graph_current(fb_offset_bar, percent);
	mvpw_expose(fb_offset_bar);

}


http_content_t http_main(void)
{

	struct sockaddr_in server_addr;
	char get_buf[1024];
	int  retcode;
	content_type_t ContentType;
	http_content_t http_retcode = HTTP_FILE_ERROR;

	int httpsock=-1;
	int flags = 0;

	char url[MAX_PLAYLIST][MAX_URL_LEN];

	char scname[MAX_URL_LEN],scport[MAX_URL_LEN],scpage[MAX_URL_LEN];
	char host_name[MAX_URL_LEN];

	int counter=0;
	int  NumberOfEntries=0;
	int  curEntry=0;
	int live365Login = 0;
	static char cookie[MAX_URL_LEN]={""};
	char user[MAX_URL_LEN],password[MAX_URL_LEN];

	char line_data[LINE_SIZE];
	char pod_data[LINE_SIZE];
	char *ptr,*ptr1;
	int i;

	int metaInt;
	int option;
	bytesRead = 0;

	int statusGet;

	struct hostent* remoteHost;
	int result;
	playlist_type_t playlistType = PLAYLIST_NONE;
	static char * ContentPlaylist[] = {
		"audio/scpls","audio/x-scpls",
		"audio/x-pn-realaudio",
		"audio/mpegurl","audio/mpeg-url","audio/x-mpegurl",
		"audio/x-mpeg-url","audio/m3u","audio/x-m3u", NULL
	};


	static char * ContentAudio[] = {"audio/aacp","audio/flac",
		"audio/mpeg","audio/x-mpeg", "audio/mpg",NULL};

	FILE *instream,*outlog,*outcast;
	char *rcs;

	using_helper = 0;

	retcode = 1;


	if ( MAX_URL_LEN < strlen(current) ) {
		sprintf(line_data,"URL length exceeds 256 characters");
		mvpw_set_text_str(fb_name, line_data);
		retcode = -1;
		return -1;
	}

	http_state_type_t stateGet = HTTP_UNKNOWN;

	int streamType=0;

	if (strncmp(current,SHOUTCAST_DOWNLOAD,sizeof(SHOUTCAST_DOWNLOAD)-1)==0 ) {
		ContentType = CONTENT_GET_SHOUTCAST;
	} else {
		ContentType = CONTENT_UNKNOWN;
	}

	snprintf(url[0],MAX_URL_LEN,"%s",current);

	curEntry = 1;
	NumberOfEntries=1;
	shoutcastDisplay[0]=0;

	outlog = fopen("/usr/share/mvpmc/connect.log","w");
	outcast = NULL;

	if (outlog == NULL) outlog = stdout;

	strcpy(cookie,"");
	while (retcode == 1 && ++counter < 5 ) {

		if ( curEntry <= 0 ) {
			mvpw_set_text_str(fb_name, "Empty playlist");
			// no valid http in playlist
			retcode = -1;
			break;
		}

		fprintf(outlog,"Connecting to %s\n",url[curEntry-1]);

		streamType = is_streaming(url[curEntry-1]);

		if ( streamType > 0 || ContentType == CONTENT_AAC || ContentType == CONTENT_DIVX ) {
			if (ContentType==CONTENT_AAC) {
				using_helper = mplayer_helper_connect(outlog,url[curEntry-1],3);
			} else {
				using_helper = mplayer_helper_connect(outlog,url[curEntry-1],0);
			}
			if ( using_helper < 0 ) {
				retcode = -1;
				using_vlc = 0;
				break;
			} else if ( using_helper == 1) {
				printf("Now playing\n");
				snprintf(url[curEntry-1],MAX_URL_LEN,"/tmp/mvpmcdump.wav");
			} else {
				using_helper = 0;
			}
			int vlcType = 0;
			if (ContentType==CONTENT_DIVX || streamType > 101) {
				vlcType = 100;
			}
			if (vlc_connect(outlog,url[curEntry-1],vlcType,VLC_CREATE_BROADCAST,NULL,0) < 0 ) {
				retcode = -1;
				using_vlc = 0;
				break;
			}


			snprintf(url[curEntry-1],MAX_URL_LEN,"http://%s:%s",vlc_server,VLC_HTTP_PORT);
			using_vlc = 1;
			if (ContentType==CONTENT_DIVX || streamType > 101) {
				ContentType = CONTENT_DIVX;
			} else {
				ContentType = CONTENT_MP3;
			}
			fprintf(outlog,"Using VLC on %s\n",url[curEntry-1]);
		}

		live365Login = 0;

		if ( strstr(url[curEntry-1],".live365.")!=NULL &&  strstr(url[curEntry-1],"sessionid=")==NULL) {
			if (cookie[0] == 0 ) {
				if (getenv("LIVE365DATA")!=NULL) {
					live365Login = 1;
				}
			} else {
				ptr = strdup(url[curEntry-1]);
				snprintf(url[curEntry-1],MAX_URL_LEN,"%s?%s",ptr,cookie);
				free(ptr);
				fprintf(outlog,"Live365 Connect to %s\n",url[curEntry-1]);
			}
		}

		retcode = 0;

		if (live365Login == 0 ) {
			scpage[0]=0;

			// no size checks use same len for all
			result = sscanf(url[curEntry-1],"http://%[^:/]:%[^//]%s",scname,scport,scpage);
			if (result==1) {
				result = sscanf(url[curEntry-1],"http://%[^/]%s",scname,scpage);
				strcpy(scport,"80");
			}
			if (scpage[0]==0) {
				strcpy(scpage,"/");
			}
		} else {
			strcpy(scname,"www.live365.com");
			strcpy(scport,"80");
		}


		mvpw_set_text_str(fb_name, scname);
		strcpy(host_name,scname);
		remoteHost = gethostbyname(host_name);
//        printf("%s\n",remoteHost->h_name);
		if (remoteHost!=NULL) {
			if (live365Login == 0 ) {
				if (NumberOfEntries==1 && strcmp(remoteHost->h_name,scname)) {
					snprintf(url[1],MAX_URL_LEN,"http://%s:%s%s",remoteHost->h_name,scport,scpage);
					NumberOfEntries=2;
				}
				if (strstr(scpage,".m3u")==NULL ) {
					if (strstr(scpage,".asf")==NULL && strstr(scpage,".asx")==NULL && cookie[0]==0 ) {
						snprintf(get_buf,MAX_URL_LEN,GET_STRING,scpage,scname);
					} else {
						snprintf(get_buf,1024,GET_ASF_STRING,scpage,scname,cookie);
					}
				} else {
					snprintf(get_buf,MAX_URL_LEN,GET_M3U,scpage,scname);
				}
			} else {
				if (snprintf(get_buf,MAX_URL_LEN,GET_LIVE365,getenv("LIVE365DATA")) >= MAX_URL_LEN-1) {
					retcode = -1;
					continue;
				}
			}
//            printf("%s\n%s\n",scname,get_buf);


			httpsock = socket(AF_INET, SOCK_STREAM, 0);

			server_addr.sin_family = AF_INET;    
			server_addr.sin_port = htons(atoi(scport));           
			memcpy ((char *) &server_addr.sin_addr, (char *) remoteHost->h_addr, remoteHost->h_length);
			struct timeval stream_tv;

			stream_tv.tv_sec = 10;
			stream_tv.tv_usec = 0;
			int optionsize = sizeof(stream_tv);
			setsockopt(httpsock, SOL_SOCKET, SO_SNDTIMEO, &stream_tv, optionsize);

			mvpw_set_text_str(fb_name, "Connecting to server");

			retcode = 0;
			do {
				if ( retcode < 0 ) {
					usleep(50000);
					mvpw_set_text_str(fb_name, "Connecting to vlc. Press stop to exit");
				}
				retcode = connect(httpsock, (struct sockaddr *)&server_addr,sizeof(server_addr));
			} while ( retcode < 0 && using_vlc == 1 && errno == ECONNREFUSED && audio_stop == 0 );

			if (retcode != 0) {
				fprintf(outlog,"connect() failed %d error %d\n",retcode,errno);
				mvpw_set_text_str(fb_name, "Connection Error");
				instream = NULL;
				rcs = NULL;
				if ( NumberOfEntries > curEntry ) {
					stateGet=HTTP_RETRY;
					ContentType = CONTENT_NEXTURL;
				} else {
					retcode = -2;
					ContentType = CONTENT_ERROR;
					audio_stop = 1;
				}
			} else {
				// Send a GET to the Web server
				mvpw_set_text_str(fb_name, "Sending GET request");
				if (send(httpsock, get_buf, strlen(get_buf), 0) != strlen(get_buf) ) {
					fprintf(outlog,"send() failed \n");
					retcode = -2;
					break;
				}
				stateGet = HTTP_INIT;
				statusGet = 0;
				playlistType = PLAYLIST_NONE;
				contentLength = 0;
				metaInt = 0;
				bitRate[0]=0;
				retcode = -2;

				instream = fdopen(httpsock,"rb");
				setbuf(instream,NULL);
				rcs = fgets(line_data,LINE_SIZE-1,instream);
			}
			while ( rcs != NULL ) {

				if (stateGet == HTTP_INIT ) {
					ptr = strrchr (line_data,'\r');
					if (ptr!=NULL) {
						*ptr =0;
					}
					fprintf(outlog,"%s\n",line_data);
					if (line_data[0]==0) {
						ContentType = CONTENT_UNKNOWN;
						retcode = -2;
						break;
					} else {
						ptr = line_data;
						while (*ptr!=0 && *ptr!=0x20) {
							ptr++;
						}
						if (*ptr==0x20) {
							sscanf(ptr,"%d",&statusGet);
						}
						if (statusGet != 200 && statusGet != 301 && statusGet != 302 && statusGet != 307) {
							if ( ContentType != CONTENT_TRYHOST && NumberOfEntries > 1 && NumberOfEntries > curEntry ) {
								ContentType = CONTENT_TRYHOST;
							} else {
								mvpw_set_text_str(fb_name, line_data);
								ContentType = CONTENT_ERROR;
								retcode = -2;
							}
							break;
						}
					}
					if (strcmp(line_data,"ICY 200 OK")==0) {
						// default shoutcast to mp3 when none found
						ContentType = CONTENT_MP3;
					} else if (strcmp(line_data,"200")==0) {
						// default shoutcast to mp3 when none found
						ContentType = CONTENT_200;
					} else if (ContentType == CONTENT_PLAYLIST) {
						ContentType = CONTENT_UNKNOWN;
					} else {
//                        ContentType = CONTENT_UNKNOWN;
					}
					stateGet = HTTP_HEADER;
					if ( shoutcastDisplay[0] ) {
						mvpw_set_text_str(fb_name,shoutcastDisplay);
					} else {
						mvpw_set_text_str(fb_name,scname);
					}
				}

				rcs = fgets(line_data,LINE_SIZE-1,instream);
				if (rcs==NULL && errno != 0) {
					if (stateGet != HTTP_RETRY) {
						printf("fget() failed %d\n",errno);
						switch (errno) {
						case EBADF:
							mvpw_set_text_str(fb_name, "Error opening stream");
							break;
						default:
							mvpw_set_text_str(fb_name, "General stream error");
							break;
						}
					}
					continue;
				}
				ptr = strpbrk (line_data,"\r\n");
				if (ptr!=NULL) {
					*ptr =0;
				}

				fprintf(outlog,"%s\n",line_data);

				if ( line_data[0]==0x0a || line_data[0]==0x0d || line_data[0]==0) {
					if (ContentType == CONTENT_UNSUPPORTED || 
					    ContentType==CONTENT_MP3 || ContentType==CONTENT_OGG || 
					    ContentType==CONTENT_FLAC || ContentType==CONTENT_MPG || 
					    ContentType==CONTENT_JPEG || stateGet==HTTP_RETRY ||
					    ContentType==CONTENT_GET_SHOUTCAST || ContentType==CONTENT_AAC) {
						// stream the following audio data or redirect
						break;
					} else if (ContentType == CONTENT_200) {
						// expect real data next time
						stateGet = HTTP_INIT;
						rcs = fgets(line_data,LINE_SIZE-1,instream);
						if (rcs==NULL) {
							printf("fget() #2 failed %d\n",errno);
							mvpw_set_text_str(fb_name, "Unexpected end of data");
						}
						continue;
					} else {
						if (stateGet!=HTTP_RETRY_LATER) {
							stateGet = HTTP_CONTENT;
						}
						continue;
					}

				} else {
					if (ContentType == CONTENT_200) {
						ContentType = CONTENT_UNKNOWN;
					}
				}
				if (stateGet == HTTP_HEADER ) {
					// parse response

					if (strncasecmp(line_data,"Content-Length:",15)==0) {
						sscanf(&line_data[15],"%lu",&contentLength);
					} else if (strncasecmp(line_data,"Content-Type",12)==0) {
						if ( strstr(line_data,"audio/") != NULL ) {
							i = 0;
							// have to do this one first some audio would match playlists
							while (ContentPlaylist[i]!=NULL) {
								ptr = strstr(line_data,ContentPlaylist[i]);
								if (ptr!=NULL) {
									switch (i) {
									case 0:
									case 1:
										playlistType = PLAYLIST_SHOUT;
										break;
									case 2:
										playlistType = PLAYLIST_RA;
										break;
									default:
										playlistType = PLAYLIST_M3U;
										break;
									}
									ContentType = CONTENT_PLAYLIST;
									curEntry = -1;	 // start again
									break;
								} else {
									i++;
								}
							}
							if (ContentType!=CONTENT_PLAYLIST) {
								i = 0;
								ContentType = CONTENT_UNSUPPORTED;
								while (ContentAudio[i]!=NULL) {
									ptr = strstr(line_data,ContentAudio[i]);
									if (ptr!=NULL) {
										switch (i) {
										case 0:
											stateGet=HTTP_RETRY;
											ContentType = CONTENT_AAC;
											break;
										case 1:
											ContentType = CONTENT_FLAC;
											break;
										default:
											ContentType = CONTENT_MP3;
											break;                                  
										}
										break;
									} else {
										i++;
									}
								}                            
							}
						} else if ( strstr(line_data,"application/ogg") != NULL ) {
							ContentType = CONTENT_OGG;
						} else if ( strstr(line_data,"video/mpeg") != NULL ) {
							ContentType = CONTENT_MPG;
						} else if ( strstr(line_data,"image/jpeg") != NULL ) {
							ContentType = CONTENT_JPEG;
						} else if ( strstr(line_data,"video/divx") != NULL  || strstr(line_data,"video/divx") != NULL ) {
							ContentType = CONTENT_DIVX;
							stateGet=HTTP_RETRY;
						} else if ( strstr(line_data,"video/x-ms-asf") != NULL || strstr(line_data,"application/asx") != NULL ) {
							playlistType = PLAYLIST_ASX;
							ContentType = CONTENT_PLAYLIST;
							curEntry = -1;	 // start again
						} else if ( strstr(line_data,"application/vnd.ms.wms-hdr.asfv1") != NULL || strstr(line_data,"application/x-mms-framed") != NULL ) {
							if (strncmp(url[curEntry-1],"http://",7)==0) {
								snprintf(url[0],MAX_URL_LEN,"mms%s",&url[curEntry-1][4]);
								ContentType = CONTENT_REDIRECT;
								stateGet=HTTP_RETRY;
							}
						} else if ( playlistType == PLAYLIST_NONE && (strstr(line_data,"text/xml") != NULL || strstr(line_data,"application/xml") != NULL ) ) {
							// try and find podcast data
							ContentType = CONTENT_PODCAST;
							playlistType = PLAYLIST_PODCAST ;
							curEntry = -1;	 // start again
						} else if (ContentType != CONTENT_GET_SHOUTCAST) {
							ContentType = CONTENT_UNKNOWN;
						}

					} else if (strncasecmp (line_data, "icy-metaint:", 12)==0) {
						sscanf(&line_data[12],"%d",&metaInt);
					} else if (strncasecmp (line_data, "Set-Cookie:",11)==0) {
						if (live365Login==1 ) {
							ptr = strstr(line_data,"sessionid=");
							if (ptr!=NULL) {
								ContentType = CONTENT_REDIRECT;
								sscanf(ptr,"%[^%]%[^;]",user,password);
								snprintf(cookie,MAX_URL_LEN,"%s:%s",user,&password[3]);
								printf("%s\n",cookie);
								stateGet=HTTP_RETRY;
								live365Login = 2;
								break;
							}
						} else {
							ptr = &line_data[11];
							while (*ptr == ' ') ptr++;
							char *p1;
							p1 = strchr(ptr,';');
							if (p1!=NULL ) *p1 = 0;
							snprintf(cookie,MAX_URL_LEN,"Cookie: %s\r\n",ptr);
							printf("%s",cookie);
						}
					} else if (strncasecmp (line_data, "icy-name:",9)==0) {
						line_data[70]=0;
						char *icyn;
						icyn = &line_data[9];
						while (*icyn==' ') {
							icyn++;
						}
						mvpw_set_text_str(fb_name,icyn );
						mvpw_menu_change_item(playlist_widget,playlist->key, icyn);
						snprintf(shoutcastDisplay,40,icyn);
					} else if (strncasecmp (line_data,"x-audiocast-name:",17)==0 ) {
						line_data[70]=0;
						mvpw_set_text_str(fb_name, &line_data[17]);
						mvpw_menu_change_item(playlist_widget,playlist->key, &line_data[17]);
						snprintf(shoutcastDisplay,40,&line_data[17]);
					} else if (strncasecmp (line_data, "icy-br:",6)==0) {
						snprintf(bitRate,10,"kbps%s",&line_data[6]);
					} else if (strncasecmp (line_data, "x-audiocast-bitrate:",20)==0) {
						snprintf(bitRate,10,"kbps%s",&line_data[20]);
					} else if ( statusGet==301 || statusGet==302 || statusGet==307 ) {
						if (strncasecmp (line_data,"Location:" ,9 )==0) {
							ptr = strstr(line_data,"http://");
							if (ptr!=NULL &&  (strlen(ptr) < MAX_URL_LEN) ) {
								snprintf(url[0],MAX_URL_LEN,"%s",ptr);
								stateGet=HTTP_RETRY;
								ContentType = CONTENT_REDIRECT;
							} else {
								mvpw_set_text_str(fb_name, "No redirection");
								ContentType = CONTENT_ERROR;
								break;
							}
						}
					}
				} else {
					// parse non-audio content
					switch (playlistType ) {
					case PLAYLIST_NONE:
						ptr = strstr(line_data,"[playlist]");
						if (ptr!=NULL) {
							playlistType = PLAYLIST_SHOUT;
							ContentType = CONTENT_PLAYLIST;
							curEntry = -1;	 // start again
							NumberOfEntries = 1; // in case NumberOfEntries= not found
						} else if (strncmp(line_data,"Too many requests.",18) == 0 ) {
							mvpw_set_text_str(fb_name, line_data);
							ContentType = CONTENT_ERROR;
						} else if ( strncasecmp(line_data,"<ASX VERSION",12) == 0 ) {
							// probably video/x-ms-asf
							playlistType = PLAYLIST_ASX;
							ContentType = CONTENT_PLAYLIST;
							curEntry = -1;	 // start again
							NumberOfEntries = 1; // in case NumberOfEntries= not found
						}

						break;
					case PLAYLIST_SHOUT:
						if (strncmp(line_data,"File",4)==0 && line_data[5]=='=') {
							if ( is_streaming(&line_data[6])>=0 && strlen(&line_data[6]) < MAX_URL_LEN ) {
								if (curEntry <= MAX_PLAYLIST ) {
									if (curEntry == -1 ) {
										// assume 1 before number of entries
										curEntry = 0;
										NumberOfEntries = 1;
									}
									snprintf(url[curEntry],MAX_URL_LEN,"%s",&line_data[6]);
									curEntry++;
									stateGet = HTTP_RETRY;
								}
							}
						} else if (strncasecmp(line_data,"NumberOfEntries=",16)==0) {
							if (curEntry == -1 ) {
								// only read the first NumberOfEntries
								NumberOfEntries= atoi(&line_data[16]);
								if (NumberOfEntries==0) {
									mvpw_set_text_str(fb_name, "Empty playlist");
									ContentType = CONTENT_ERROR;
								} else {
									curEntry = 0;
								}
							}
						}
						break;
					case PLAYLIST_M3U:
						if ( strncmp(line_data,"http://",7) == 0 && strlen(line_data) < MAX_URL_LEN ) {
							if (curEntry <= MAX_PLAYLIST ) {
								if (curEntry == -1 ) {
									// assume 1 before number of entries
									curEntry = 0;
									NumberOfEntries = 0;
								}
							}
							snprintf(url[curEntry],MAX_URL_LEN,"%s",line_data);
							curEntry++;
							NumberOfEntries++;
							stateGet = HTTP_RETRY;
						}
						break;
					case PLAYLIST_RA:
						if ( strncmp(line_data,"rtsp://",7) == 0 && strlen(line_data) < MAX_URL_LEN ) {
							if (curEntry <= MAX_PLAYLIST ) {
								if (curEntry == -1 ) {
									// assume 1 before number of entries
									curEntry = 0;
									NumberOfEntries = 0;
								}
							}
							snprintf(url[curEntry],MAX_URL_LEN,"%s",line_data);
							curEntry++;
							NumberOfEntries++;
							stateGet = HTTP_RETRY;
						}
						break;
					case PLAYLIST_PODCAST:
						if ( strstr(line_data,"</rss>")!=NULL ) {
							stateGet = HTTP_RETRY;
						}
						if ( (ptr=strstr(line_data,".mp3")) != NULL && strstr(line_data,"enclosure url") != NULL && (ptr1=strstr(line_data,"http://")) != NULL ) {
							ptr+=4;
							*ptr=0;
							if (ptr1 < ptr && ((ptr1-ptr) < MAX_URL_LEN) ) {
								if (curEntry <= 0 ) {
									if (curEntry == -1 ) {
										// assume 1 before number of entries
										curEntry = 0;
										NumberOfEntries = 0;
										outcast = fopen("/usr/playlist/outcast.m3u","w");
									}
									snprintf(url[curEntry],MAX_URL_LEN,"%s",ptr1);
									curEntry++;
									NumberOfEntries++;
									if (stateGet != HTTP_RETRY) {
										stateGet = HTTP_RETRY_LATER;
									}
								}
								fprintf(outcast,"#EXTINF:-1,%s\n%s\n",pod_data,ptr1);
							}
						} else if ((ptr=strstr(line_data,"<title>")) != NULL  && (ptr1=strstr(line_data,"</title>")) != NULL ) {
							if ( ptr < ptr1 ) {
								ptr+= 7;
								*ptr1 = 0;
								if (curEntry <= 0 ) {
									snprintf(shoutcastDisplay,40,"%s",ptr);
								}
								snprintf(pod_data,LINE_SIZE,"%s",ptr);
							}
						}
						break;
					case PLAYLIST_ASX:
						if (strstr(line_data,"[Reference]") !=NULL ) {
							playlistType = PLAYLIST_ASX_REFERENCE;
						} else {
							ptr = stristr(line_data,"<REF HREF");
							if (ptr!=NULL) {
								printf("%s\n",ptr);
								ptr+=9;
								while (*ptr!=0) {
									if (*ptr =='"') {
										ptr++;
										break;
									}
									ptr++;
								}
								if (*ptr!=0) {
									char *z;
									z = strchr(ptr,'"');
									if (z!=NULL) {
										*z = 0;
									}
									if (is_streaming(ptr) >= 0 ) {
										if (curEntry <= MAX_PLAYLIST ) {
											if (curEntry == -1 ) {
												// assume 1 before number of entries
												curEntry = 0;
												NumberOfEntries = 1;
											}
											snprintf(url[curEntry],MAX_URL_LEN,"%s",ptr);
											curEntry++;
											stateGet = HTTP_RETRY;
										}
									}
								}
							}

						}
						break;
					case PLAYLIST_ASX_REFERENCE:
						if (strncmp(line_data,"Ref",3)==0 && line_data[4]=='=') {
							if ( is_streaming(&line_data[5])>=0 && strlen(&line_data[5]) < MAX_URL_LEN ) {
								if (curEntry <= MAX_PLAYLIST ) {
									if (curEntry == -1 ) {
										// assume 1 before number of entries
										curEntry = 0;
										NumberOfEntries = 1;
									}
									snprintf(url[curEntry],MAX_URL_LEN,"%s",&line_data[5]);
									curEntry++;
									stateGet = HTTP_RETRY;
								}
							}
						}
						break;
					default:
						break;
					}
					if ( ContentType == CONTENT_ERROR || curEntry == MAX_PLAYLIST ) {
						// no need for any more or an error
						break;
					}
				}

			}

//			printf("%d %d %d %d %d %d\n",line_data[0],ContentType,stateGet,retcode,statusGet,curEntry);

			switch (ContentType) {
			case CONTENT_MP3:
				// good streaming mp3 data
				fclose(outlog);
				outlog = NULL;
				shoutOut = fopen("/usr/share/mvpmc/played.log","w");
				fprintf(shoutOut,"MediaMVP Media Center Song History\n%s\n%s\n\n%s\n\n",shoutcastDisplay,url[curEntry-1],"Played @ Song Title");
				fflush(shoutOut);

				if (fcntl(httpsock, F_SETFL, flags | O_NONBLOCK) != 0) {
					printf("nonblock fcntl failed \n");
				} else {
					outbuf = ring_buf_create(OUT_BUF_SIZE);
					recvbuf = (void*)calloc(1, RECV_BUF_SIZE);
					http_read_stream(httpsock,metaInt,0);
					free(recvbuf);
					free(outbuf->buf);
					free(outbuf);
				}
				fclose(shoutOut);
				retcode = -2;
				http_retcode = HTTP_FILE_DONE;
				break;
			case CONTENT_OGG:
				fclose(outlog);
				outlog = NULL;
				fd = httpsock;
				http_retcode = HTTP_AUDIO_FILE_OGG;
				retcode = 3;
				break;
			case CONTENT_FLAC:
				fclose(outlog);
				outlog = NULL;
				fd = httpsock;
				http_retcode = HTTP_AUDIO_FILE_FLAC;
				retcode = 3;
				break;
			case CONTENT_MPG:
				// this data doesn't make it to mpg or ogg streams change to use fdopen etc.
				if (fcntl(httpsock, F_SETFL, flags | O_NONBLOCK) != 0) {
					printf("nonblock fcntl failed \n");
				}

				fd = httpsock;
				option = 65534; 
				setsockopt(httpsock, SOL_SOCKET, SO_RCVBUF, &option, sizeof(option));
//                    option = 0;
//                    int optionsize = sizeof(int);
//                    getsockopt(httpsock, SOL_SOCKET, SO_RCVBUF, &option, &optionsize);
				http_retcode = HTTP_VIDEO_FILE_MPG;
				retcode = 3;
				break;
			case CONTENT_JPEG:
				// this data doesn't make it to mpg or ogg streams change to use fdopen etc.
				if (fcntl(httpsock, F_SETFL, flags | O_NONBLOCK) != 0) {
					printf("nonblock fcntl failed \n");
				}

				fd = httpsock;
				option = 65534; 
				setsockopt(httpsock, SOL_SOCKET, SO_RCVBUF, &option, sizeof(option));
				http_retcode = HTTP_IMAGE_FILE_JPEG;
				retcode = 3;
				break;
			case CONTENT_GET_SHOUTCAST:
				fclose(outlog);
				outlog = NULL;
				fd = httpsock;
				shoutOut = fdopen(fd, "rb");
				create_shoutcast_playlist(25);
				fclose(shoutOut);
				retcode = -2;
				http_retcode = HTTP_FILE_DONE;
				break;
			case CONTENT_PLAYLIST:
			case CONTENT_PODCAST:
			case CONTENT_REDIRECT:
			case CONTENT_AAC:
			case CONTENT_DIVX:
				if (stateGet == HTTP_RETRY) {
					// try again
					curEntry = 1;
					fprintf(outlog,"Redirect %d %s\n",curEntry,url[curEntry-1]);
					close(httpsock);
					retcode = 1;
				} else {
					retcode = -2;
					http_retcode = HTTP_FILE_DONE;
				}
				break;
			case CONTENT_UNKNOWN:
			case CONTENT_UNSUPPORTED:
				mvpw_set_text_str(fb_name, "No supported content found");
				retcode = -2;
				audio_stop = 1;
				http_retcode = HTTP_FILE_UNKNOWN;
				break;
			case CONTENT_TRYHOST:
				close(httpsock);
			case CONTENT_NEXTURL:
				if (curEntry < NumberOfEntries) {
					// try next
					curEntry++;
					fprintf(outlog,"Retry %d %s\n",curEntry,url[curEntry-1]);
					retcode = 1;
				} else {
					fclose(outlog);
					retcode = -2;
					http_retcode = HTTP_FILE_DONE;
				}
				break;
			case CONTENT_ERROR:
			default:
				// allow message above;
				retcode = -2;
				audio_stop = 1;
				http_retcode = HTTP_FILE_ERROR;
				break;
			}
		} else {
			mvpw_set_text_str(fb_name, "DNS Trouble Check /etc/resolv.conf");
			retcode = -1;
		}
	}

	if (retcode == -2 || retcode == 2) {
		close(httpsock);
	}
	if (using_helper==1) {
		if (outlog==NULL) {
			outlog = fopen("/usr/share/mvpmc/connect.log","a");
		}
		mplayer_helper_connect(outlog,NULL,1);
	}

	if (using_vlc==1) {
		if (http_retcode != HTTP_VIDEO_FILE_MPG) {
			if (outlog==NULL) {
				outlog = fopen("/usr/share/mvpmc/connect.log","a");
			}
			vlc_connect(outlog,NULL,1, VLC_DESTROY,NULL,0);
			using_vlc = 0;
		} else {
			using_helper = 2;
		}
	}
	if (using_helper==1) {
		// delete tmp file
		mplayer_helper_connect(outlog,NULL,2);
		using_helper = 0;
	}

	if (outlog!=NULL) {
		fclose(outlog);
	}
	if (outcast!=NULL) {
		fclose(outcast);
	}

	if (contentLength==0 && audio_stop == 0 ) {
		audio_stop = 1;
	}

	if (gui_state == MVPMC_STATE_HTTP ) {
		mvpw_hide(mclient);
		gui_state = MVPMC_STATE_FILEBROWSER;
	}
	return http_retcode;
}


void http_buffer(int message_length, int offset)
{
	if (message_length != 0 ) {
		bytesRead+=message_length;
		/*
		 * Check if there is room at the end of the buffer for the new data.
		 */
		if ((outbuf->head + message_length) <= OUT_BUF_SIZE ) {
			/*
			 * Put data into the rec buf.
			 */
			memcpy((outbuf->buf + outbuf->head),recvbuf+offset,message_length);

			/*
			 * Move head by number of bytes read.
			 */
			outbuf->head += message_length;
			if (outbuf->head == OUT_BUF_SIZE ) {
				outbuf->head = 0;
			}
		} else {
			/*
			 * If not, then split data between the end and beginning of
			 * the buffer.
			 */
			memcpy((outbuf->buf + outbuf->head), recvbuf+offset, (OUT_BUF_SIZE - outbuf->head));
			memcpy(outbuf->buf,recvbuf + offset + (OUT_BUF_SIZE - outbuf->head), (message_length - (OUT_BUF_SIZE - outbuf->head)));

			/*
			 * Move head by number of bytes written from the beginning of the buffer.
			 */
			outbuf->head = (message_length - (OUT_BUF_SIZE - outbuf->head));
		}
	}
}
int http_metadata(char *metaString,int metaWork,int metaData)
{
	int retcode;
	char buffer[MAX_META_LEN];
	char *ptr;

	if (metaData > MAX_META_LEN-1 ) {
		metaData = MAX_META_LEN-1;
	}
	memcpy(buffer,recvbuf+metaWork,metaData);
	buffer[metaData]=0;

	if (strncmp(buffer,"StreamTitle=",12)==0) {
		char * fc;
		fc = strstr (buffer,"';StreamUrl='");
		if (fc!=NULL) {
			*fc = 0;
		} else if ((fc = strstr(buffer,"';"))!=NULL) {
			*fc= 0;
		}
		if (strcmp(metaString,&buffer[13])) {
			ptr = &buffer[13];
//            printf("md |%s|\n",ptr);
			while (*ptr!=0)	ptr++;
			do {
				ptr--;
			} while (*ptr==' ' && ptr != &buffer[13]);
			if (ptr != &buffer[13]) {
				snprintf(metaString,MAX_META_LEN,"%s",&buffer[13]);
				time_t tm;
				tm = time(NULL);
				strftime(buffer,MAX_META_LEN,"%H:%M:%S", localtime(&tm));
				fprintf(shoutOut,"%s %s\n",buffer,metaString);
				fflush(shoutOut);
				retcode = 0;
			} else {
				retcode = 3;
			}
		} else {
			retcode = 3;
		}
	} else if (strncmp(buffer,"StreamUrl=",10)==0) {
		retcode = 1;
	} else {
		printf("%x|%s|\n",buffer[0],buffer);
		retcode = 2;
	}
	return retcode;

}

int http_read_stream(unsigned int httpsock,int metaInt,int offset)
{
	int retcode=0;

	int metaRead = metaInt;
	int metaStart = 0;
	int metaWork=0;
	char cmetaData;
	int metaData=0;
	int metaIgnored=0;
	char peekBuffer[STREAM_PACKET_SIZE];

	char metaString[4081];
	char metaTemp[4081];

	char mclientDisplay[140];

	int message_len=offset;

	metaRead -= message_len;
	outbuf->playmode = 4;

	struct timeval stream_tv;
	fd_set read_fds;
	FD_ZERO(&read_fds);
	int dataFlag = 0;

	do {

		int n = 0;

		FD_ZERO(&read_fds);
		FD_SET(httpsock, &read_fds);
		if (httpsock > n)  n = httpsock ;

		/*
		 * Wait until we receive data from server or up to 2s
		 */
		stream_tv.tv_sec =  2;
		stream_tv.tv_usec = 0;

		if (select(n + 1, &read_fds, NULL, NULL, &stream_tv) == -1) {
			if (errno != EINTR && errno != EAGAIN) {
				printf("select error\n");
				abort();
			}
		}
		if (outbuf->head >= outbuf->tail ) {
			bufferFull = outbuf->head - outbuf->tail;
		} else {
			bufferFull = outbuf->head + (OUT_BUF_SIZE - outbuf->tail);
		}
		if (outbuf->playmode == 4 && bufferFull > (OUT_BUF_SIZE / 2) ) {
			outbuf->playmode = 0;
			outbuf->tail = 0;
		} else if (outbuf->tail==0 && dataFlag > 20) {
			outbuf->playmode = 0;
		}

		/*
		 * Check if the "select" event could have been caused because data
		 * has been sent by the server.
		 */

		if (FD_ISSET(httpsock, &read_fds) ) {
//            printf("%d %d %d %d %d %d\n",outbuf->playmode,dataFlag,bufferFull,outbuf->head,outbuf->tail,message_len);

			if (bufferFull < (OUT_BUF_SIZE - 2 * STREAM_PACKET_SIZE) ) {
				message_len = recv (httpsock, (char *)recvbuf, STREAM_PACKET_SIZE,0);
				if (message_len<=0) {
					dataFlag++;
				} else {
					dataFlag=0;
				}
				if (message_len < 0) {
					if (errno!=EAGAIN) {
						printf("recv() error %d\n",errno);
					}
				} else if (metaInt==0) {
					metaRead -= message_len;
					http_buffer(message_len,0);
				} else {

					if (metaRead < message_len) {
						cmetaData =  ((char*)recvbuf)[metaRead];
						metaData = (int) cmetaData;
						metaWork = metaRead;
						if ( metaData==0 ) {
							http_buffer(metaRead,0);
							metaStart = metaRead+1;
							metaRead = metaInt;
							if (metaStart < message_len) {
								http_buffer(message_len-metaStart,metaStart);
								metaRead = metaRead - (message_len-metaStart);
							} else {
//                                    printf("meta int 0\n");
							}


						} else {
							http_buffer(metaRead,0);
							metaData*=16;
//                          printf("meta %d\n",metaData);
							metaStart = metaRead + metaData + 1;
							if (metaStart <= message_len ) {
								metaRead = metaInt;
								if (http_metadata(metaString,metaWork+1,metaData)==0 ) {
									printf("%s\n",metaString);
									mvpw_set_text_str(fb_name, metaString);
									snprintf (mclientDisplay,80, "%-40.40s\n%-40.40s\n", shoutcastDisplay,metaString);
									if (gui_state != MVPMC_STATE_HTTP ) {
										switch_gui_state(MVPMC_STATE_HTTP);
//                                        mvpw_show(mclient);
										mvpw_focus(playlist_widget);
									}
									mvpw_set_dialog_text (mclient,mclientDisplay);
								}
								if ( metaStart < message_len ) {
									http_buffer(message_len-metaStart,metaStart);
									metaRead = metaRead - (message_len-metaStart);
								} else {
									/* all read */
								}
							} else {
								/* skip rest */
								memcpy(metaTemp,recvbuf+metaRead+1,message_len-metaRead);
								metaIgnored = message_len-metaRead;
								metaTemp[metaIgnored-1]=0;
								printf("mt 1 %s\n",metaTemp);
								metaStart -= message_len;
//                                printf("ignore %s %d %d\n",metaString,metaStart,metaIgnored);
								metaRead = metaInt + metaStart;
							}
						}
					} else {
						// check this for optimizing
//                        printf ("%d %d\n",metaRead,message_len );

						if (metaRead > metaInt) {
							// meta data spans recvs - does not update screen

							if ((metaStart+metaIgnored) > 4000) {
								// probably because of metaint error probably insane
								abort();
							}

							if ((metaRead-message_len) > metaInt ) {
								/* all recv was meta data skip it all */
								metaStart = metaRead - metaInt;                                
								memcpy(metaTemp+metaIgnored-1,recvbuf,metaStart);
								metaRead-=message_len;
								metaIgnored += metaStart;
								printf("ignore 2 %d\n",metaRead);
							} else {
								metaStart = metaRead - metaInt;
								memcpy(metaTemp+metaIgnored-1,recvbuf,metaStart);
								metaRead-=metaStart;
								http_buffer(message_len-metaStart,metaStart);
								metaRead -= (message_len - metaStart);

							}
						} else {
							metaRead -= message_len;
							http_buffer(message_len,0);
						}
					}
				}
			} else {
				dataFlag++;
				message_len = recv (httpsock, peekBuffer, STREAM_PACKET_SIZE,MSG_PEEK);
				usleep(10000);
			}
		}
		send_mpeg_data();
		usleep(1000);
	} while ( (message_len > 0 || bufferFull > 1) && audio_stop == 0 );
	if (audio_stop == 0) {
		// empty audio buffer
		usleep(1000000);
	}
	bufferFull = 0;

//    printf("message %d buff %d stop %d\n",message_len,bufferFull,audio_stop);

	return  retcode;
}

#include <ctype.h>

int is_streaming(char *url)
{
	char *types[]= {
		"http://",
		"vlc://",
		"mms://",
		"mmsh://",
		"rtsp://",
		NULL
	};
	static char *vlcTypes[] = {
		".wma",
		".WMA",
		".divx",
		".flv",
		".wmv",
		".avi",
		".DIVX",
		".FLV",
		".WMV",
		".AVI",
		".mp4",
		".MP4",
		".rm",
		".RM",
		".ogm",
		".OGM",
		NULL
	};
	int i=0;
	int retcode = -1;
	char *ptr;
	ptr = strstr(url,"://");
	if ( ptr!=NULL ) {
		while (types[i]!=NULL) {
			if (strncmp(url,types[i],strlen(types[i]) )==0) {
				retcode = i;
				break;
			}
			i++;
		}
	}
	if (retcode == -1) {
		i = 0;
		while (vlcTypes[i]!=NULL) {
			if ( strstr(url,vlcTypes[i]) !=NULL ) {
				retcode = 100+i;
				break;
			}
			i++;
		}

	}
	return retcode;
}

int fixTitle(char *title,char *src);

int create_shoutcast_playlist(int limit)
{
	char title[256];
	char url[256];
	char *p,*ptr;
	char in_buffer[2048];
	int j = 0;
	FILE *outfile;

	ptr = strrchr(current,'?');
	ptr++;
	if (*ptr=='g') {
		p = strrchr(ptr,'=');
		ptr = ++p;
	}
	snprintf(title,256,"%s",ptr);
	p = strchr(title,'=');
	if (p!=NULL) *p = 0;

	snprintf(url,256,"/usr/playlist/%s.m3u",title);
	outfile = fopen(url,"w");

	fprintf(outfile,"#EXTM3U\n");
	while (fgets(in_buffer,2048,shoutOut) != NULL ) {
		if (strstr(in_buffer,"mt=\"audio/mpeg")!=NULL) {
			p = strstr(in_buffer,"name=\"");
			if (p!=NULL) {
				p+=6;
				ptr = strchr(p,'"');
				if (ptr!=NULL) {
					*ptr = 0;
					ptr++;
					fixTitle(title,p);
					title[59]=0;
					p = strstr(ptr,"id=\"");
					p+=4;
					if (p!=NULL) {
						ptr = strchr(p,'"');
						if (ptr!=NULL) {
							*ptr=0;
							snprintf(url,256,SHOUTCAST_TUNEIN,p);
							fprintf(outfile,"#EXTINF:-1,%s\n%s\n",title,url);
							if (++j==limit)	break;
						}
					}
				}
			}
		}

	}
	fclose(outfile);
	return 0;
}

int fixTitle(char *title,char *src)
{
	char *ptr;
	ptr = src;
	while (*ptr!=0) {
		if (*ptr=='&') {
			if (strncmp(ptr,"&amp;",5)) {
				ptr +=5;
				*title = '&';
				title++;
				continue;
			} else if (strncmp(ptr,"&#42",4)) {
				ptr +=4;
				*title = '*';
				continue;
			} else if (strncmp(ptr,"&#124",5)) {
				ptr +=4;
				*title = '|';
				continue;
			}
		}
		*title = *ptr;
		ptr++;
		title++;

	}
	*title = 0;
	return 0;
}


int mplayer_helper_connect(FILE *outlog,char *url,int stopme)
{
	struct sockaddr_in server_addr; 
	struct hostent* remoteHost;
	char mvpmc_helper_port[5];
	FILE *instream;
	char line_data[LINE_SIZE];
	int mvpmc_helper_sock=-1;

	int size;


	int retcode=-1;

	if (stopme == 0 && strncmp(url,"rtsp:",5) != 0) {
		return 0;
	}

	if (mplayer_disable==1) {
		mvpw_set_text_str(fb_name, "Mplayer support not enabled");
		fputs("Mplayer support not enabled\n",outlog);
		return -1;
	}

	remoteHost = gethostbyname(vlc_server);

	if (remoteHost!=NULL) {

		mvpmc_helper_sock = socket(AF_INET, SOCK_STREAM, 0);

		server_addr.sin_family = AF_INET;
		strcpy(mvpmc_helper_port,VLC_VLM_PORT);
		server_addr.sin_port = htons(atoi(mvpmc_helper_port)+1);

		memcpy ((char *) &server_addr.sin_addr, (char *) remoteHost->h_addr, remoteHost->h_length);

		struct timeval stream_tv;
		memset((char *)&stream_tv,0,sizeof(struct timeval));
		stream_tv.tv_sec = 6;
		int optionsize = sizeof(stream_tv);

		setsockopt(mvpmc_helper_sock, SOL_SOCKET, SO_SNDTIMEO, &stream_tv, optionsize);
		mvpw_set_text_str(fb_name, "Connecting to mvpmc helper");

		retcode = connect(mvpmc_helper_sock, (struct sockaddr *)&server_addr,sizeof(server_addr));

		if (retcode == 0) {
			instream = fdopen(mvpmc_helper_sock,"r+b");
			setbuf(instream,NULL);
			switch (stopme) {
			case 0:
			case 3:
				mvpw_set_text_str(fb_name, "Waiting for mplayer");
				size = snprintf(line_data,LINE_SIZE-1,"loadfile %s",url);
				break;
			case 1:
				size = snprintf(line_data,LINE_SIZE-1,"quit");
				break;
			case 2:
				size = snprintf(line_data,LINE_SIZE-1,"cleanup");
				break;
			}
			size=send(mvpmc_helper_sock, line_data, strlen(line_data), 0);

			while (1) {
				size=recv(mvpmc_helper_sock, line_data, LINE_SIZE-1, 0);
				if (size > 0 ) {
					line_data[size]=0;
					if ( strncmp(line_data,"Buffering",9)==0 ) {
						line_data[40]=0;
						mvpw_set_text_str(fb_name,line_data );
					} else {
						fprintf(outlog,"helper: %s %d\n",line_data,size);
						if (strncasecmp(line_data,"Error",5)==0 ) {
							mvpw_set_text_str(fb_name, "mplayer error");
							retcode = -1;
						} else {
							retcode = 1;
							sleep(1);
						}
						break;
					}
				} else if (size < 0 ) {
					mvpw_set_text_str(fb_name, "mplayer unhandled error");
					retcode = size;
					break;
				}
			}
			close(mvpmc_helper_sock);
		} else {
			mvpw_set_text_str(fb_name, "mplayer connection timeout");
			printf("Cannot connect to %s:%s\n",vlc_server,mvpmc_helper_port);
			fprintf(outlog,"mplayer connection timeout\nCannot connect to %s:%s\n",vlc_server,mvpmc_helper_port);
			retcode = -1;
		}
	} else {
		mvpw_set_text_str(fb_name, "mplayer setup error");
		fprintf(outlog,"mplayer setup error\nCannot find %s\n",vlc_server);
		retcode = -1;
	}
	return retcode;
}

char *stristr(char *str, char *substr)
{
	long lensub,remains;
	char *ptr;
	lensub = strlen(substr);
	remains = strlen(str);
	ptr = str;
	while (remains >= lensub) {
		if (strncasecmp(ptr, substr, lensub)==0) {
			return(ptr);
		}
		remains--;
		ptr++;
	}
	return(NULL);
}
