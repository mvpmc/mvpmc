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
#include "mythtv.h"
#include "replaytv.h"
#include "config.h"

/*
 * vlc_control.c
 *
 * Maintained by Robin Rawson-Tetley <robin@rawsontetley.org>
 * This file contains code from Martin Vallevand and Robin Rawson-Tetley
 */


static long long vlc_stream_size(void);
static long long vlc_stream_seek(long long, int);
static int vlc_key(char);
char* vlc_get_video_transcode();
char* vlc_get_audio_transcode();

extern int errno;
extern char *vlc_server;	// main.c
extern int http_main(void); 	// audio.c

/* VLC command for audio transcoding to mp3 */
#define VLC_MP3_TRANSCODE "setup mvpmc output #transcode{acodec=mp3,ab=%d,channels=2}:duplicate{dst=std{access=http,mux=raw,url=:%s}}\r\n"

/* VLC command for audio transcoding to flac */
#define VLC_FLAC_TRANSCODE "setup mvpmc output #transcode{acodec=flac,channels=2}:duplicate{dst=std{access=http,mux=raw,url=:%s}}\r\n"

/* VLC command for video/audio transcode to mpeg 2 */
#define VLC_VIDEO_TRANSCODE "setup mvpmc output #transcode{vcodec=mp2v,vb=%d,venc=ffmpeg{keyint=3},scale=1,audio-sync,soverlay,deinterlace,width=%s,height=%s,canvas-width=%s,canvas-height=%s,canvas-aspect=%s,fps=%s,acodec=mpga,ab=%d,channels=2}:duplicate{dst=std{access=http,mux=ts,dst=:%s}}\r\n"

/* VLC command video/audio transcode to mpeg 2 without scaling */
#define VLC_VIDEO_NOSCALE_TRANSCODE "setup mvpmc output #transcode{vcodec=mp2v,vb=%d,scale=1,fps=%s,acodec=mpga,ab=%d,channels=2}:duplicate{dst=std{access=http,mux=ts,dst=:%s}}\r\n"

/*
 * Buffer for the transcoding request to be sent to VLC
 */
char vlc_transcode_message[512];

/* 
 * Whether or not VLC_BROADCAST messages are enabled (disabling
 * them allows us to go through the http_main cycle again and
 * connect to an already running VLC stream)
 */
int vlc_broadcast_enabled = 1;

/*
 * The last seen position in the vlc stream. We only
 * go to the network once during pause and OK/BLANK OSD enabling. 
 * This is also set by all seek functions so that when the osd asks 
 * for our current position we don't have to go to the network again.
 * If the vlc_get_pct_pos() routine finds -1 in this variable, it will
 * populate it by interrogating the stream using the VLC telnet 
 * interface. Pausing and bringing up the OSD will reset this value to
 * -1 to ensure fresh info. 
 */
int vlc_cachedstreampos = -1;

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
 * Main routine to connect to VLC telnet interface. 
 * 
 * outlog:		The log file to print messages to
 * url:			The URL to the file to play on the VLC box
 * ContentType:		The type of content - >=100 is video
 * VlcCommandType:	One of:
 *
 * 	VLC_BROADCAST   - Create the transcoding stream
 * 	VLC_CONTROL     - control mvpmc <command>
 * 	VLC_DESTROY     - issues del mvpmc to destroy the stream
 *
 * 	VLC_SEEK_PCT    - receive stream info and seek based on a percentage
 *                        offset of current percentage position.
 * 	    	          return value is new stream position percentage
 *
 * 	VLC_SEEK_SEC	- interrogates the stream for the current percentage
 * 			  position, calcaultes one second in percent and 
 * 			  moves backwards or forwards by the offset.
 * 			  return value is the new stream position percentage.
 *
 * 	VLC_PCTPOS   	- interrogates the stream for the current percentage
 * 	                  position.return value is stream position percentage.
 *
 * VlcCommandArgs:	A VLC telnet command for control mvpmc %s (but
 * 			only if VlcCommandType == VLC_CONTROL)
 *
 * offset:		An offset percentage value if
 * 		        VlcCommandType == VLC_SEEK_PCT, or number
 * 		        of seconds for VLC_SEEK_SEC
 *
 */
int vlc_connect(FILE *outlog,char *url,int ContentType, int VlcCommandType, char *VlcCommandArgs, int offset)
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
    char *vpos;
    int mpos;
    int newpos = 0;
    char *vlength;
    double dpos = 0;
    double dlength = 0;
    double donesec = 0;
    double doffsetpct = 0;
    double dseekpos = 0;

    // BROADCAST commands
    char *vlc_connects[]= {
        NULL,
        "del mvpmc\r\n",
        "new mvpmc broadcast enabled\r\n",
        "setup mvpmc input %s\r\n",
        NULL,
        "setup mvpmc option sout-http-mime=%s/%s\r\n",
        "control mvpmc play\r\n"
    };

    // CONTROL commands
    char *vlc_controls[]= {
        NULL,
        "control mvpmc %s\r\n"
    };

    // SEEK_PCT commands
    char *vlc_pct[]= {
        NULL,
	"show mvpmc\r\n",
	NULL,
        "control mvpmc seek %d\r\n"
    };

    // SEEK_SEC commands
    char *vlc_sec[]= {
        NULL,
	"show mvpmc\r\n",
	NULL,
        "control mvpmc seek %f\r\n"
    };



    if (VlcCommandType == VLC_CREATE_BROADCAST) {
        vlc_commands_size = 7;
    } else if (VlcCommandType == VLC_CONTROL) {
        vlc_commands_size = 2;
    } else if (VlcCommandType == VLC_DESTROY) {
        vlc_commands_size = 2;
    } else if (VlcCommandType == VLC_SEEK_PCT) {
        vlc_commands_size = 4;
    } else if (VlcCommandType == VLC_SEEK_SEC) {
	vlc_commands_size = 4;
    } else if (VlcCommandType == VLC_PCTPOS) {
        vlc_commands_size = 4;
    }

    // If broadcast messages are disabled and this is one, bail out now.
    fprintf(outlog, "broadcast messages enabled == %d\n", vlc_broadcast_enabled);
    if ((VlcCommandType == VLC_CREATE_BROADCAST) && (vlc_broadcast_enabled == 0)) {	
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
		    	// New stream means we reset our cached position
			vlc_cachedstreampos = -1;
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
                                    fprintf(instream,vlc_get_video_transcode());
                                    fprintf(outlog,vlc_get_video_transcode());
                                } else {
                                    fprintf(instream,vlc_get_audio_transcode());
                                    fprintf(outlog,vlc_get_audio_transcode());
                                }
                                break;
                            case 5:
                                if (ContentType == 100) {
                                    fprintf(instream,vlc_connects[i],"video", "mpeg");
                                    fprintf(outlog,vlc_connects[i],"video", "mpeg");
                                } else {
				    if (strcmp(config->vlc_aopts, "flac") == 0) {
				        fprintf(instream,vlc_connects[i],"audio", "flac");
                                        fprintf(outlog,vlc_connects[i],"audio", "flac");
				    }
                                    else {
				        fprintf(instream,vlc_connects[i],"audio", "mpeg");
                                        fprintf(outlog,vlc_connects[i],"audio", "mpeg");
				    }
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
                    } else if (VlcCommandType == VLC_DESTROY) {
                        switch(i) {
                            case 0:
                                fprintf(instream,"admin\r\n");
                                fprintf(outlog,"admin\n");
                                break; 
                            case 1:
                                fprintf(instream,vlc_connects[i]);
                                fprintf(outlog,vlc_connects[i]);
                                break;
                            default:
                                fprintf(instream, vlc_controls[i]);
                                fprintf(outlog, vlc_controls[i]);
                                break;
                        }
                    } else if (VlcCommandType == VLC_SEEK_PCT) {
		        switch (i) {
				case 0:
				    // Send authentication
				    fprintf(instream,"admin\r\n");
				    fprintf(outlog,"admin\n");
				    break;
				case 1:
				    // SHOW MVPMC command
				    fprintf(instream,vlc_pct[i]);
				    fprintf(outlog,vlc_pct[i]);
				    break;
				case 2:
				    // Parse the show mvpmc response and
				    // extract the 'position : p' value
				    // One we have that, apply an offset and seek
				    vpos = strstr(line_data, "position : ");
				    if (vpos == NULL) {
					fprintf(outlog, "VLC: couln't find 'position : '");
					return -1;
			 	    }
				    // Adjust offset of string pointer to beginning of
				    // numeric value for position
				    vpos += 11;
				    // Parse the current % position
				    mpos = (int) (strtod(vpos, NULL) * (double) 100);
				    // Calculate new position
				    newpos = mpos + offset;
				    fprintf(outlog, "\nPosition: %d%%, Offset: %d%%, NewPosition: %d%%\n", mpos, offset, newpos);
				    // Is the new position out of range?
				    // Bail if it is and return the current pos
				    if (newpos > 99 || newpos < 0) {
  			                shutdown(vlc_sock,SHUT_RDWR);
			                close(vlc_sock);
				    	return mpos;
				    }
				    // Send seek command to new position
				    i++;
				    fprintf(instream,vlc_pct[i],newpos);
				    fprintf(outlog,vlc_pct[i],newpos);
				    // Update position for osd
				    vlc_cachedstreampos = newpos;
				    // Cleanup
			            shutdown(vlc_sock,SHUT_RDWR);
			            close(vlc_sock);
				    return newpos;
				default:
				    fprintf(instream,vlc_pct[i]);
				    fprintf(outlog,vlc_pct[i]);
				    break;
			}
                    } else if (VlcCommandType == VLC_SEEK_SEC) {
			switch (i) {
				case 0:
				    // Send authentication
				    fprintf(instream,"admin\r\n");
				    fprintf(outlog,"admin\n");
				    break;
				case 1:
				    // SHOW MVPMC command
				    fprintf(instream,vlc_sec[i]);
				    fprintf(outlog,vlc_sec[i]);
				    break;
				case 2:
				    // Parse the show mvpmc response and
				    // extract the 'position : p' value and
				    // length : l value.
				    // One we have that, calculate percentage for
				    // one second, apply an offset and seek
				    vpos = strstr(line_data, "position : ");
				    if (vpos == NULL) {
					fprintf(outlog, "VLC: couln't find 'position : '");
					return -1;
				    }
				    vlength = strstr(line_data, "length : ");
				    if (vpos == NULL) {
					fprintf(outlog, "VLC: couln't find 'length : '");
					return -1;
				    }
				    // Adjust offset of string pointers to beginning of
				    // numeric value for position and length
				    vpos += 11;
				    vlength += 9;
				    // Parse the current % position
				    dpos = strtod(vpos, NULL) * (double) 100;
				    // Parse the current length
				    dlength = strtod(vlength, NULL);
				    // If length is < 0 then we have an older VLC with the
				    // overflow bug - log and error and do nothing
				    if (dlength < 0) {
					fprintf(outlog, "Detected VLC with overflow length bug - cannot do second seek!");
					shutdown(vlc_sock,SHUT_RDWR);
					close(vlc_sock);
					return (int) dpos;
				    }
				    // Calculate one second percent (VLC reports length
				    // and time values in microseconds - 10^-6)
				    donesec = (1000000 / dlength) * (double) 100;
				    // Calculate offset percentage
				    doffsetpct = donesec * (double) offset;
				    // Calculate new seek position
				    dseekpos = doffsetpct + dpos;
				    fprintf(outlog, "Pos: %f%%, Length: %f, Onesec: %f%%, NewPos: %f%% \n", 
				    	dpos, dlength, donesec, dseekpos);
				    // Is the new position out of range?
				    // Bail if it is and return the current pos
				    if (dseekpos > 99 || dseekpos < 0) {
					shutdown(vlc_sock,SHUT_RDWR);
					close(vlc_sock);
					return (int) dpos;
				    }
				    // Send seek command to new position
				    i++;
				    fprintf(instream,vlc_sec[i],dseekpos);
				    fprintf(outlog,vlc_sec[i],dseekpos);
				    // Update position for osd
				    vlc_cachedstreampos = (int) dseekpos;
				    // Cleanup
				    shutdown(vlc_sock,SHUT_RDWR);
				    close(vlc_sock);
				    return newpos;
				default:
				    fprintf(instream,vlc_sec[i]);
				    fprintf(outlog,vlc_sec[i]);
				    break;
			}

                    } else if (VlcCommandType == VLC_PCTPOS) {
		        switch (i) {
				case 0:
				    // Send authentication
				    fprintf(instream,"admin\r\n");
				    break;
				case 1:
				    // SHOW MVPMC command
				    fprintf(instream,vlc_pct[i]);
				    fprintf(outlog,vlc_pct[i]);
				    break;
				case 2:
				    // Parse the show mvpmc response and
				    // extract the 'position : p' value
				    vpos = strstr(line_data, "position : ");
				    if (vpos == NULL) {
					fprintf(outlog, "VLC: couln't find 'position : '");
					return -1;
			 	    }
				    // Adjust offset of string pointer to beginning of
				    // numeric value for position
				    vpos += 11;
				    // Parse the current % position
				    mpos = (int) (strtod(vpos, NULL) * (double) 100);
				    fprintf(outlog, "Position: %d%%\n", mpos);
			            shutdown(vlc_sock,SHUT_RDWR);
			            close(vlc_sock);            
				    return mpos;
				default:
				    fprintf(instream,vlc_pct[i]);
				    fprintf(outlog,vlc_pct[i]);
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
 * Reads the MVPMC config items for TV aspect and
 * mode. It uses these to construct a VLC transcode request
 * for appropriate quality video. Resizes the output
 * stream to scale to the canvas if dvd/svcd/vcd is selected.
 */
char* vlc_get_video_transcode()
{
	char* aspect;
	char* canvas_width;
	char* canvas_height;
	char* fps;

	/** Figure out height and FPS */
	if (config->av_tv_aspect == AV_TV_ASPECT_16x9)
		aspect = "16:9";
	else
		aspect = "4:3";

	if (config->av_mode == AV_MODE_PAL) {
		canvas_height = "576";
		fps = "25.0000";
	} else {
		canvas_height = "480";
		fps = "29.9700";
	}


	/* bitrate settings */
	int ab = config->vlc_ab;
	if (ab == 0) ab = 192;
	int vb = config->vlc_vb;

	/** DVD scaling settings */
	if (*config->vlc_vopts == 0 || strcmp(config->vlc_vopts, "dvd") == 0) {
		if (vb == 0) vb = 4192;
		canvas_width = "720";
		sprintf(vlc_transcode_message, VLC_VIDEO_TRANSCODE, 
			vb, canvas_width, canvas_height, canvas_width, 
			canvas_height, aspect, fps, ab, VLC_HTTP_PORT);
		return vlc_transcode_message;
	}

	/** SVCD scaling settings */
	if (strcmp(config->vlc_vopts, "svcd") == 0) {
		if (vb == 0) vb = 2778;
		canvas_width = "480";
		sprintf(vlc_transcode_message, VLC_VIDEO_TRANSCODE, 
			vb, canvas_width, canvas_height, canvas_width, 
			canvas_height, aspect, fps, ab, VLC_HTTP_PORT);
		return vlc_transcode_message;
	}

	/** VCD scaling settings */
	if (strcmp(config->vlc_vopts, "vcd") == 0) {
		if (vb == 0) vb = 1152;
		canvas_width = "352";
		if (config->av_mode == AV_MODE_PAL)
			canvas_height = "288";
		else
			canvas_height = "240";
		sprintf(vlc_transcode_message, VLC_VIDEO_TRANSCODE, 
			vb, canvas_width, canvas_height, canvas_width, 
			canvas_height, aspect, fps, ab, VLC_HTTP_PORT);
		return vlc_transcode_message;
	}


	/** No scaling settings */
	if (vb == 0) vb = 2048;
	sprintf(vlc_transcode_message, VLC_VIDEO_NOSCALE_TRANSCODE, 
		vb, fps, ab, VLC_HTTP_PORT);
	return vlc_transcode_message;
}

/*
 * Returns the VLC transcode request for mp3
 * audio of the target.
 */
char* vlc_get_audio_transcode()
{

	/* Audio bitrate settings */
	int ab = config->vlc_ab;
	if (ab == 0) ab = 192;

	if ((*config->vlc_aopts == 0) || (strcmp(config->vlc_aopts, "flac") != 0))
		sprintf(vlc_transcode_message, VLC_MP3_TRANSCODE, ab, VLC_HTTP_PORT);
	else
		sprintf(vlc_transcode_message, VLC_FLAC_TRANSCODE, VLC_HTTP_PORT);
	return vlc_transcode_message;
}


/*
 * Tells VLC to seek to a given percentage
 * of the playing stream. 
 */
int vlc_seek_pct(int pos)
{
    char cmd[10];
    sprintf(cmd, "seek %d", pos);
    vlc_cachedstreampos = pos;
    return vlc_cmd(cmd);
}

/*
 * Tells VLC to pause playback
 */
int vlc_pause()
{
    return vlc_cmd("pause");
}

/*
 * Tells VLC to stop playback
 */
int vlc_stop()
{
    return vlc_cmd("stop");
}

/*
 * Stops and destroys the mvpmc vlc stream
 */
int vlc_destroy()
{
    FILE *outlog = fopen("/usr/share/mvpmc/connect.log", "a");
    int rv = vlc_connect(outlog, NULL, 100, VLC_DESTROY, NULL, 0);
    fclose(outlog);
    return rv;
}

/*
 * Sends a command to the VLC telnet interface.
 * This is a wrapper around vlc_connect with a VLC_CONTROL message
 * to save passing lots of args and having to open the log every 
 * time. The command sent is "control mvpmc <cmd>"
 */
int vlc_cmd(char *cmd)
{
    FILE *outlog = fopen("/usr/share/mvpmc/connect.log", "a");
    int rv = vlc_connect(outlog, NULL, 100, VLC_CONTROL, cmd, 0);
    fclose(outlog);
    return rv;
}

/*
 * Moves backwards and forwards a percentage
 * of the stream in VLC by calculating the current position
 * as a percentage and adding the offset.
 * Uses vlc_connect with VLC_SEEK_PCT
 */
int vlc_seek_pct_relative(int offset)
{
    FILE *outlog = fopen("/usr/share/mvpmc/connect.log", "a");
    int rv = vlc_connect(outlog, NULL, 100, VLC_SEEK_PCT, NULL, offset);
    fclose(outlog);
    return rv;
}

/*
 * Moves backwards and forwards a number of seconds
 * in the stream in VLC by calculating the current position
 * as an exact percentage, figuring out how much of a pct
 * represents one second, and then adding the offset.
 * Uses vlc_connect with VLC_SEEK_SEC
 */
int vlc_seek_sec_relative(int offset)
{
    FILE *outlog = fopen("/usr/share/mvpmc/connect.log", "a");
    int rv = vlc_connect(outlog, NULL, 100, VLC_SEEK_SEC, NULL, offset);
    fclose(outlog);
    return rv;
}

/*
 * Returns the current percentage position of the playing
 * VLC stream.
 */
int vlc_get_pct_pos()
{

    FILE *outlog = fopen("/usr/share/mvpmc/connect.log", "a");
    int rv = vlc_cachedstreampos;
    
    // If we have an already stored pause position, return
    // that instead of going to the network again
    if (vlc_cachedstreampos == -1) {
        rv = vlc_connect(outlog, NULL, 100, VLC_PCTPOS, NULL, 0);
	vlc_cachedstreampos = rv;
    }
    fprintf(outlog, "VLC_PCT_POS: %d%%\n", rv);
    fclose(outlog);
    return rv;
}


/* Video pause function for vlc */
void vlc_key_pause(void)
{
	av_pause();
	mvpw_show(pause_widget);
	mvpw_hide(ffwd_widget);
	paused = 1;
	// Reset our stored pause position so we can look it up for the osd
	vlc_cachedstreampos = -1;   
	if (pause_osd && !display_on && (display_on_alt < 2)) {
		display_on_alt = 2;
		enable_osd();
	}
	screensaver_enable();
	vlc_pause();
}

 /* Video unpause function for vlc */
void vlc_key_unpause(void)
{
	// Stop broadcast messages being sent
 	vlc_broadcast_enabled = 0;

	// Stop the a/v stream altogether	
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
	screensaver_disable();
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
		timed_osd(seek_osd_timeout*1000);
		vlc_seek_pct(10 * key);
		break;
	
	case MVPW_KEY_LEFT:
		// Rewind VLC stream by 2%
		timed_osd(seek_osd_timeout*1000);
		vlc_seek_pct_relative(-2);
		break;

	case MVPW_KEY_RIGHT:
		// Fast forward VLC stream by 2%
		timed_osd(seek_osd_timeout*1000);
		vlc_seek_pct_relative(2);
		break;

	case MVPW_KEY_CHAN_DOWN:
	case MVPW_KEY_DOWN:
	case MVPW_KEY_REWIND:
		// Rewind VLC stream by 5%
		timed_osd(seek_osd_timeout*1000);
		vlc_seek_pct_relative(-5);
		break;

	case MVPW_KEY_CHAN_UP:
	case MVPW_KEY_UP:
	case MVPW_KEY_FFWD:
		// Fast forward VLC stream by 5%
		timed_osd(seek_osd_timeout*1000);
		vlc_seek_pct_relative(5);
		break;

	case MVPW_KEY_SKIP:
		// FFWD VLC stream by 30 seconds
		timed_osd(seek_osd_timeout*1000);
		vlc_seek_sec_relative(30);
		break;

	case MVPW_KEY_REPLAY:
		// Rewind VLC stream by 10 seconds
		timed_osd(seek_osd_timeout*1000);
		vlc_seek_sec_relative(-10);
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
			vlc_key_pause();
		else 
			vlc_key_unpause();
		break;

	case MVPW_KEY_PLAY:
		// Unpause the av and reconnect to the stream
		if ( paused ) 
			vlc_key_unpause();
		break;

	case MVPW_KEY_BLANK:
	case MVPW_KEY_OK:
		// Reset our stored stream position so that
		// the osd shows where we are correctly.
		vlc_cachedstreampos = -1;   
		if (display_on || display_on_alt) {
			disable_osd();
			mvpw_expose(root);
			display_on = 1;
			display_on_alt = 0;
		} else {
			enable_osd();
		}
		display_on = !display_on;
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
 * We deal in percentages for stream sizes. */
static long long
vlc_stream_size(void)
{
	return (long long) 100000;
}

/* Stream seek callback from video_callback_t
 * Returns a seekable percentage.
 * This function is primarily used for making sure
 * the OSD widget position is correct.
 */
static long long
vlc_stream_seek(long long offset, int whence)
{
	return (long long) (vlc_get_pct_pos() * 1000);
}


