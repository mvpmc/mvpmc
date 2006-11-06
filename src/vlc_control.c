/*
 *  Copyright (C) 2004,2005,2006, Jon Gettler
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
#include <ctype.h>
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

static long long vlc_stream_size(void);
static long long vlc_stream_seek(long long, int);
static int vlc_key(char);

extern int errno;
extern char *vlc_server;	//config.c
extern int http_main(void); 	// audio.c


// Whether or not VLC_BROADCAST messages are enabled (disabling
// them allows us to go through the http_main cycle again and
// connect to an already running stream)
int vlc_reconnect = 0;

void vlc_setreconnect(int reconnecting) 
{
    vlc_reconnect = reconnecting;
}

/*
 * Version of atoll that converts signed to 
 * to unsigned correctly and does long long.
 * It's used when calculating our current vlc stream 
 * position for a context seek.
 */
unsigned long long atollc(const char *nptr)
{
    int c;
    unsigned long long total;
    int sign;
    while (isspace((int)(unsigned char) *nptr)) ++nptr;
    c = (int)(unsigned char) *nptr++;
    sign = c;
    if (c == '-' || c == '+') c = (int)(unsigned char) *nptr++;
    total = 0;
    while (isdigit(c)) 
    {
        total = 10 * total + (c - '0');
        c = (int)(unsigned char) *nptr++;
    }
    //if (sign == '-')
      // Set hi-bit if negative
      //total = total | (0x01 << 31);
    return total;
}

/* video_callback_t for vlc functions */
video_callback_t vlc_functions = {
	.open      = file_open,
	.read      = file_read,
	.read_dynb = NULL,
	.seek      = vlc_stream_seek,
	.size      = vlc_stream_size,
	.notify    = NULL,
	.key       = vlc_key,
	.halt_stream = NULL,
};


/*
 * Main routine to connect to VLC telnet interface. Is capable of sending:
 * VLC_BROADCAST   - transpose/start streaming message
 * VLC_CONTROL     - any other VLC command
 * VLC_CONTEXTSEEK - receive stream info and seek based on context
 */
int vlc_connect(FILE *outlog,char *url,int ContentType, int VlcCommandType, char *VlcCommandArgs)
{
    struct sockaddr_in server_addr; 
    struct hostent* remoteHost;
    char vlc_port[5];
    FILE *instream;
    char line_data[LINE_SIZE];
    int vlc_sock=-1;
    int i;
    char *ptr;
    int vlc_commands_size = -1;
    char *vtime;
    char *vlength;
    int newpos = 0;

    // BROADCAST commands
    char *vlc_connects[]= {
        NULL,
        "del mvpmc\r\n",
        "new mvpmc broadcast enabled\r\n",
        "setup mvpmc input %s\r\n",
        NULL,
        "setup mvpmc option sout-http-mime=%s/mpeg\r\n",
        "control mvpmc play\r\n"
    };

    // CONTROL commands
    char *vlc_controls[]= {
        NULL,
        "control mvpmc %s\r\n"
    };

    // CONTEXTSEEK commands
    char *vlc_cts[]= {
        NULL,
	"show mvpmc\r\n",
	NULL,
        "control mvpmc seek %d\r\n"
    };


    if (VlcCommandType == VLC_CREATE_BROADCAST) {
        vlc_commands_size = 7;
    } else if (VlcCommandType == VLC_CONTROL) {
        vlc_commands_size = 2;
    } else if (VlcCommandType == VLC_CONTEXTSEEK) {
        vlc_commands_size = 4;
    }

    fprintf(outlog, "dont send broadcast == %d\n", vlc_reconnect);
    if ((VlcCommandType == VLC_CREATE_BROADCAST) && (vlc_reconnect == 1)) {	
        return 0;
    }

    int rc;
    int retcode=-1;

    remoteHost = gethostbyname(vlc_server);

    if (remoteHost!=NULL) {
     
        vlc_sock = socket(AF_INET, SOCK_STREAM, 0);

        server_addr.sin_family = AF_INET;
        strcpy(vlc_port,VLC_VLM_PORT);
        server_addr.sin_port = htons(atoi(vlc_port));

        memcpy ((char *) &server_addr.sin_addr, (char *) remoteHost->h_addr, remoteHost->h_length);
        
        struct timeval stream_tv;
        memset((char *)&stream_tv,0,sizeof(struct timeval));
        stream_tv.tv_sec = 10;
        int optionsize = sizeof(stream_tv);

        setsockopt(vlc_sock, SOL_SOCKET, SO_SNDTIMEO, &stream_tv, optionsize);
        mvpw_set_text_str(fb_name, "Connecting to vlc");
    
        retcode = connect(vlc_sock, (struct sockaddr *)&server_addr,sizeof(server_addr));

        if (retcode == 0) {
            char *newurl;
            if (url!=NULL) {
                if (*url!='"' && strchr(url,' ') ){
                    newurl = malloc(strlen(url)+3);
                    snprintf(newurl,strlen(url)+3,"\"%s\"",url);
                } else {
                    newurl = strdup(url);
                }
            } else {
                newurl = NULL;
            }

            instream = fdopen(vlc_sock,"r+b");
            setbuf(instream,NULL);

            for (i=0;i < vlc_commands_size ;i++) {
                rc=recv(vlc_sock, line_data, LINE_SIZE-1, 0);
                if ( feof(instream) || ferror(instream) ) break;
                if (rc != -1 ) {
                    line_data[rc]=0;
                    ptr=strchr(line_data,0xff);
                    if (ptr!=0) {
                        *ptr=0;
                    }
                    fprintf(outlog,"%s",line_data);
                    if (ContentType==2) {
                        break;
                    }
                    if (VlcCommandType == VLC_CREATE_BROADCAST) {
                        switch (i) {
                            case 0:
                                fprintf(instream,"admin\r\n");
                                fprintf(outlog,"admin\n");
                                if (ContentType==0 || ContentType==100) {
                                    i++;
                                }
                                break;
                            case 1:
                                fprintf(instream,vlc_connects[i]);
                                fprintf(outlog,vlc_connects[i],newurl);
                                ContentType = 2;
                                break;
                            case 3:
                                fprintf(instream,vlc_connects[i],newurl);
                                fprintf(outlog,vlc_connects[i],newurl);
                                break;
                            case 4:
                                if ( ContentType == 100 ) {
                                    fprintf(instream,VLC_DIVX_TRANSCODE,VLC_HTTP_PORT);
                                    fprintf(outlog,VLC_DIVX_TRANSCODE,VLC_HTTP_PORT);
                                } else {
                                    fprintf(instream,VLC_MP3_TRANSCODE,VLC_HTTP_PORT);
                                    fprintf(outlog,VLC_MP3_TRANSCODE,VLC_HTTP_PORT);
                                }
                                break;
                            case 5:
                                if (ContentType == 100) {
                                    fprintf(instream,vlc_connects[i],"video");
                                    fprintf(outlog,vlc_connects[i],"video");
                                } else {
                                    fprintf(instream,vlc_connects[i],"audio");
                                    fprintf(outlog,vlc_connects[i],"audio");
                                }
                                break;
                            case 2:
                                if (strncmp(current,"vlc://",6) == 0 ) {
                                    fprintf(instream,"load %s",&current[6]);
                                    fprintf(outlog,"load %s",&current[6]);
                                    ContentType = 2;
                                    break;
                                }
                            default:
                                fprintf(instream,vlc_connects[i]);
                                fprintf(outlog,vlc_connects[i]);
                                break;
                        }
                    } else if (VlcCommandType == VLC_CONTROL) {
                        switch(i) {
                            case 0:
                                fprintf(instream,"admin\r\n");
                                fprintf(outlog,"admin\n");
                                break; 
                            case 1:
                                fprintf(instream,vlc_controls[i],VlcCommandArgs);
                                fprintf(outlog,vlc_controls[i],VlcCommandArgs);
                                break;
                            default:
                                fprintf(instream, vlc_controls[i]);
                                fprintf(outlog, vlc_controls[i]);
                                break;
                        }
                    } else if (VlcCommandType == VLC_CONTEXTSEEK) {
		        switch (i) {
				case 0:
				    // Send authentication
				    fprintf(instream,"admin\r\n");
				    fprintf(outlog,"admin\n");
				    break;
				case 1:
				    // SHOW MVPMC command
				    fprintf(instream,vlc_cts[i]);
				    fprintf(outlog,vlc_cts[i]);
				    break;
				case 2:
				    // Parse the show mvpmc response and
				    // extract the 'time : t' and 'length : l' values
				    // One we have those, calculate our current percentage
				    // position in the stream and add the offset
				    vtime = strstr(line_data, "time : ");
				    if (vtime == NULL) {
					mvpw_set_text_str(fb_name, "VLC: couldn't find 'time : '");
					fprintf(outlog, "VLC: couln't find 'time : '");
					return -1;
				    }
				    vlength = strstr(line_data, "length : ");
				    if (vlength == NULL) {
					mvpw_set_text_str(fb_name, "VLC: couldn't find 'length : '");
					fprintf(outlog, "VLC: couln't find 'length : '");
					return -1;
				    }
				    // Adjust offsets of string pointers to beginning of
				    // numeric values for time/length
				    vtime += 7;
				    vlength += 9;
				    // This code does not work. If a debug atollc I can see that
				    // the correct values are parsed and returned, but a runtime
				    // error occurs after that.
				    unsigned long long ltime = atollc(vtime);
				    fprintf(outlog, "Parsed Time : %llu\n", ltime);
				    unsigned long long llength = atollc(vlength);
				    fprintf(outlog, "Parsed Length : %llu\n", llength);
				    // Calculate our current percentage position
				    // TODO: Are floats allowed? Performance hit?
				    int cpos = ((float) ltime / (float) llength) * (float) 100;
				    fprintf(outlog, "Cur pct pos: %d\n", cpos);
				    // Calculate new position
				    newpos = cpos + atoi(VlcCommandArgs);
				    fprintf(outlog, "Calculated new pos: %d", newpos);
				    break;
				case 3:
				    // SEEK command
				    fprintf(instream,vlc_cts[i],newpos);
				    fprintf(outlog,vlc_cts[i],newpos);
				    break;
				default:
				    fprintf(instream,vlc_cts[i]);
				    fprintf(outlog,vlc_cts[i]);
				    break;
			}
		    }
                } else {
                    break;
                }
            }
            if (url!=NULL) {
                free(newurl);
            }
            fflush(outlog);
            shutdown(vlc_sock,SHUT_RDWR);
            close(vlc_sock);            
        } else {
            mvpw_set_text_str(fb_name, "VLC connection timeout");
            fprintf(outlog,"VLC connection timeout\nCannot connect to %s:%s\n",vlc_server,vlc_port);
            retcode = -1;
        }
    } else {
        mvpw_set_text_str(fb_name, "VLC/VLM setup error");
        fprintf(outlog,"VLC/VLM setup error\nCannot find %s\n",vlc_server);
        retcode = -1;
    }
    return retcode;
}

/*
 * VLC last seeked to position for context
 */
int lastpos = 0;

/*
 * Tells VLC to seek to a given position (percentage)
 * in the stream. 
 */
int vlc_seek(int pos)
{
    char cmd[10];
    sprintf(cmd, "seek %d", pos);
    lastpos = pos;
    return vlc_cmd(cmd);
}

/*
 * Tells VLC to seek to a given offset from the last
 * seeked position (as long as it's not outside 0-99).
 */
int vlc_ctxseek(int offset)
{
    int newpos = lastpos + offset;
    if (newpos < 0 || newpos > 99) return 0;
    lastpos += offset;
    return vlc_seek(lastpos);
}

/*
 * Tells VLC to pause playback
 */
int vlc_pause()
{
    return vlc_cmd("pause");
}

/*
 * Sends a command to the VLC telnet interface.
 * Just a wrapper around vlc_connect to save passing lots of args
 * and having to open the log every time.
 */
int vlc_cmd(char *cmd)
{
    FILE *outlog = fopen("/usr/share/mvpmc/connect.log", "a");
    int rv = vlc_connect(outlog, NULL, 100, VLC_CONTROL, cmd);
    fclose(outlog);
    return rv;
}

/*
 * Uses context to go backwards and forwards a percentage
 * of the stream in VLC by calculating the current position
 * as a percentage.
 * Uses vlc_connect with VLC_CONTEXTSEEK
 */
int vlc_ctxffrew(int offset)
{
    FILE *outlog = fopen("/usr/share/mvpmc/connect.log", "a");
    char soffset[10];
    sprintf(soffset, "%d", offset);
    int rv = vlc_connect(outlog, NULL, 100, VLC_CONTEXTSEEK, soffset);
    fclose(outlog);
    return rv;
}

/* Video pause function for vlc */
void vlc_ctl_pause(void)
{
	av_pause();
	mvpw_show(pause_widget);
	mvpw_hide(ffwd_widget);
	paused = 1;
	if (pause_osd && !display_on && (display_on_alt < 2)) {
		display_on_alt = 2;
		enable_osd();
	}
	screensaver_enable();
	vlc_pause();
}

 /* Video unpause function for vlc */
void vlc_ctl_unpause(void)
{
	// Stop broadcast messages being sent
 	vlc_setreconnect(1);

	// Stop the a/v stream altogether	
	audio_clear();
	video_clear();

	// Resume playing the VLC stream 
	vlc_pause();

	// Start a/v again. audio_play is used since it has
	// to figure out the content type from the stream before
	// starting video
	audio_play(NULL);

	// Hide the pause widget
	if (pause_osd && !display_on &&
	    (display_on_alt == 2)) {
		display_on_alt = 0;
		disable_osd();
		mvpw_expose(root);
	}
	mvpw_hide(pause_widget);
	mvpw_hide(mute_widget);
	paused = 0;

}

/* Callback for remote button presses during vlc playback */
int 
vlc_key(char key)
{
	int rtnval = 1;
	long long offset, size;

	FILE *outlog;
	outlog = fopen("/usr/share/mvpmc/connect.log","a");

	switch(key) {

	case MVPW_KEY_ZERO ... MVPW_KEY_NINE:
		vlc_seek(10 * key);
		break;

	case MVPW_KEY_LEFT:
		// Rewind VLC stream by 2%
		vlc_ctxseek(-2);
		break;

	case MVPW_KEY_RIGHT:
		// Fast forward VLC stream by 2%
		vlc_ctxseek(2);
		break;

	case MVPW_KEY_RECORD:
		// Resyncs the video stream during http playback if the
		// audio gets out of sync
		size = video_functions->size();
		jump_target = -1;
		jumping = 1;
		pthread_kill(video_write_thread, SIGURG);
		pthread_kill(audio_write_thread, SIGURG);
		offset = video_functions->seek(0, SEEK_CUR);
		jump_target = ((size / 100.0) + offset);
		pthread_cond_broadcast(&video_cond);
		break;

	case MVPW_KEY_PAUSE:
		// Pause or Unpause the av and stream
		if ( !paused )
			vlc_ctl_pause();
		else 
			vlc_ctl_unpause();
		break;

	case MVPW_KEY_PLAY:
		// Unpause the av and reconnect to the stream
		if ( paused ) 
			vlc_ctl_unpause();
		break;

	default:
		printf("No http key defined %d \n", key);
		rtnval = -1;
		break;
	}

	fclose(outlog);

	return rtnval;
}

/* Stream size callback from video_callback_t
 * FIXME: Could maybe use length value from mvpmc show */
static long long
vlc_stream_size(void)
{
	struct stat64 sb;
    
	fstat64(fd_http, &sb);
	printf("http_size: %lld\n", sb.st_size);
	return sb.st_size;    
}

/* Stream seek callback from video_callback_t
 * FIXME: Could use VLC_CONTEXTSEEK message */
static long long
vlc_stream_seek(long long offset, int whence)
{
	return lseek(fd_http, offset, whence);
}


