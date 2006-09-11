/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
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

/*! \mainpage MediaMVP Media Center
 *
 * The MediaMVP Media Center is an open source replacement for the Hauppauge
 * firmware that runs on the Hauppauge MediaMVP.
 *
 * \section projectweb Project website
 * http://www.mvpmc.org/
 *
 * \section repos Source repositories
 * http://git.mvpmc.org/
 *
 * \section libraries Libraries
 * \li \link mvp_av.h libav \endlink
 * \li \link cmyth.h libcmyth \endlink
 * \li \link mvp_demux.h libdemux \endlink
 * \li \link mvp_osd.h libosd \endlink
 * \li \link mvp_widget.h libwidget \endlink
 *
 * \section application Application Headers
 * \li \link mvpmc.h mvpmc.h \endlink
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/select.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <getopt.h>
#include <ctype.h>
#include <sys/stat.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>
#include <ts_demux.h>

#include "mvpmc.h"
#include "mythtv.h"
#include "replaytv.h"
#include "config.h"

#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

#include "display.h"
#include "mclient.h"

char *version = NULL;

static struct option opts[] = {
	{ "aspect", required_argument, 0, 'a' },
	{ "config", required_argument, 0, 'F' },
	{ "iee", required_argument, 0, 'd' },
	{ "mclient", required_argument, 0, 'c' },
	{ "mode", required_argument, 0, 'm' },
	{ "mythtv", required_argument, 0, 's' },
	{ "mythtv-debug", no_argument, 0, 'M' },
	{ "no-wss", no_argument, 0, 0 },
	{ "no-filebrowser", no_argument, 0, 0 },
	{ "no-reboot", no_argument, 0, 0 },
	{ "no-settings", no_argument, 0, 0 },
	{ "theme", required_argument, 0, 't' },
	{ "startup", required_argument, 0, 0},
	{ "web-port", required_argument, 0, 0},
	{ "version", no_argument, 0, 0},
	{ "vlc", required_argument, 0, 0},
	{ "use-mplayer", no_argument, 0, 0 },
	{ "emulate", required_argument, 0, 0},
	{ "rfb-mode", required_argument, 0, 0},
	{ "flicker", no_argument, 0, 0},
	{ "mythtv-db", required_argument, 0, 'y'},
	{ "mythtv-username", required_argument, 0, 'u'},
	{ "mythtv-password", required_argument, 0, 'p'},
	{ "mythtv-database", required_argument, 0, 'T'},
	{ 0, 0, 0, 0 }
};


int settings_disable = 0;
int reboot_disable = 0;
int filebrowser_disable = 0;
int mplayer_disable = 1;
int rfb_mode = 3;
int flicker = 0;

/* this controls the default web server set to zero to disable */
#ifdef MVPMC_HOST
int web_port = 0;
#else
int web_port = -1;
#endif

int web_server;

void reset_web_config(void);
void load_web_config(char *font);

extern char cwd[];

/*
 * Let's use the "exit" option for "no startup
 * option selected" as no one would choose this.
 */
int startup_this_feature = MM_EXIT;

#define VNC_SERVERPORT (5900)    /* Offset to VNC server for regular connections */

char *mythtv_server = NULL;
char *replaytv_server = NULL;
char *mclient_server = NULL;
char *vlc_server = NULL;
char *mvp_server = NULL;

char vnc_server[256];
int vnc_port = 0;

int fontid;
extern demux_handle_t *handle;
extern ts_demux_handle_t *tshandle;

char *mythtv_recdir = NULL;
char *mythtv_ringbuf = NULL;
char *rtv_init_str  = NULL;

char *saved_argv[32];
int saved_argc = 0;
char *imagedir = "/usr/share/mvpmc";

extern a52_state_t *a52_state;

static pthread_t video_read_thread;
pthread_t video_write_thread;
pthread_t audio_write_thread;
pthread_t audio_thread;
pthread_attr_t thread_attr, thread_attr_small;
pthread_t web_server_thread;
static pthread_t video_events_thread;
void *www_mvpmc_start(void *arg);

pthread_mutex_t web_server_mutex = PTHREAD_MUTEX_INITIALIZER;

static pid_t child;

av_demux_mode_t demux_mode;

int (*DEMUX_PUT)(demux_handle_t*, void*, int);
int (*DEMUX_WRITE_VIDEO)(demux_handle_t*, int);
int (*DEMUX_WRITE_AUDIO)(demux_handle_t*, int);

#define DATA_SIZE (1024*1024)
static char *data = NULL;
static int data_len = 0;

mvpmc_state_t hw_state = MVPMC_STATE_NONE;
mvpmc_state_t gui_state = MVPMC_STATE_NONE;

config_t *config;
static int shmid;

struct shared_disk_t {
    char ip[64];
    char local_dir[64];
    char remote_dir[64];
    char is_nfs;
};

typedef struct {
    char tz[30];
    char time_server[64];
    char cwd[64];
    char vnc_server[64];
    int  vnc_port;
    int  startup_this_feature;
    int  bitmask;
    char mythtv_ringbuf[64];
    char screen_capture_file[64];
    char mythtv_recdir[64];
    char rtv_init_str[64];
    char font[64];
    char imagedir[64];
    int pick_playlist;
    char playlist[64];
    char live365_userid[33];
    char live365_password[33];
    char share_user[16];
    char share_password[16];
    struct shared_disk_t share_disk[3];
    char vlc_server[64];
    char mvp_server[64];
    int rfb_mode;
    int flicker;
    int control;
    char wol_mac[18];
}web_config_t;

web_config_t *web_config;

#ifndef MVPMC_HOST
static int web_shmid;
#endif


char *config_file = NULL;

char *screen_capture_file = NULL;

static void atexit_handler(void);

static int
buffer_put(demux_handle_t *handle, void *buf, int len)
{
	int n;

	if (data_len)
		return 0;

	if (len <= 0)
		return 0;

	n = (len < DATA_SIZE) ? len : DATA_SIZE;

	memcpy(data, buf, n);

	data_len = n;

	return n;
}

static int 
buffer_write(demux_handle_t *handle, int fd)
{
	int n;

	n = write(fd, data, data_len);

	if (n != data_len)
		memcpy(data, data+n, data_len-n);

	data_len -= n;

	return n;
}

/*
 * print_help() - print command line arguments
 *
 * Arguments:
 *	prog	- program name
 *
 * Returns:
 *	nothing
 */
static void
print_help(char *prog)
{
	printf("Usage: %s <options>\n", prog);

	printf("\t-a aspect \taspect ratio (4:3, 4:3cco, 16:9)\n");
	printf("\t-b path   \tpath to NFS mounted mythtv ringbuf directory\n");
	printf("\t-C file   \tbitmap file to use for screen captures\n");
	printf("\t-d type   \ttype of local display (disable (default), IEE16x1, or IEE40x2)\n");
	printf("\t-f font   \tfont file\n");
	printf("\t-F file   \tconfig file\n");
	printf("\t-h        \tprint this help\n");
	printf("\t-i dir    \tmvpmc image directory\n");
	printf("\t-m mode   \toutput mode (ntsc or pal)\n");
	printf("\t-M        \tMythTV protocol debugging output\n");
	printf("\t-o output \toutput device for video (composite or svideo) and / or for audio (stereo or passthru)\n");
	printf("\t-s server \tmythtv server IP address\n");
	printf("\t-y server \tmythtv DB server IP address\n");
	printf("\t-u username \tmythtv mysql username\n");
	printf("\t-p password \tmythtv mysql password\n");
	printf("\t-T table \tmythtv mysql database name\n");
	printf("\t-S seconds\tscreensaver timeout in seconds (0 - disable)\n");
	printf("\t-r path   \tpath to NFS mounted mythtv recordings\n");
	printf("\t-R server \treplaytv server IP address\n");
	printf("\t-D display\tVNC server IP address[:display]\n");
	printf("\t-t file   \tXML theme file\n");
	printf("\t-c server \tslimdevices musicClient server IP address\n");
	printf("\n");
	printf("\t--no-wss  \tdisable Wide Screen Signalling(WSS)\n");
	printf("\t--no-filebrowser\n");
	printf("\t--no-reboot\n");
	printf("\t--no-settings\n");
	printf("\t--startup \t(replaytv, mythtv, mclient)\n");
	printf("\t--web-port port\tconfiguration port\n");
	printf("\t--vlc server \tvlc IP address\n");
	printf("\t--use-mplayer \tenable mplayer\n");
	printf("\n");
	printf("\t--emulate server \tIP address or ?\n");
	printf("\t--rfb-mode mode\t(0, 1, 2)\n");
	printf("\t--flicker \tflicker mode on\n");
}

/*
 * sighandler() - kill the child and exit
 */
static void
sighandler(int sig)
{
	/*
	 * Allow the child to do any exit processing before killing it.
	 */
	kill(-child, sig);
	usleep(1000*250);

	kill(-child, SIGKILL);
	if (shmctl(shmid, IPC_RMID, NULL) != 0)
		perror("shmctl()");
	exit(sig);
}

/*
 * doexit() - exit so any atexit processing can happen
 */
static void
doexit(int sig)
{
	av_deactivate();
	exit(sig);
}

/*
 * spawn_child() - spawn a child, and respawn it whenever it exits
 *
 * This is used to implement the power button.  If the child exits cleanly,
 * then another child should be started.  If the child exits with a signal,
 * restart another child.  If the child exits with a 1, the parent should
 * just give up and exit.
 *
 * Also, if the power key is pressed once, kill the child.  If it is pressed
 * again, restart the child.
 */

void
spawn_child(void)
{
	int status;
	fd_set fds;
	int fd;
	struct timeval to;
	int power = 1;
	int ret;

	if ((fd=open("/dev/rawir", O_RDONLY)) < 0) {
		perror("/dev/rawir");
		exit(1);
	}
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	while (1) {
		if ((child=fork()) == 0) {
			setsid();
			printf("child pid %d\n", getpid());
			close(fd);
			signal(SIGINT, doexit);
			signal(SIGTERM, doexit);
			return;
		}

		while (1) {
			unsigned short key;

			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			to.tv_sec = 1;
			to.tv_usec = 0;

			key = web_config->control;
			if ( key!=0 || select(fd+1, &fds, NULL, NULL, &to) > 0) {
                if (key == 0 ) {
       				read(fd, &key, sizeof(key));
	      			read(fd, &key, sizeof(key));
				} 

				if ((key & 0xff) == 0x3d) {
					power = !power;
                    web_config->control = 0;
					if (power == 0) {
						kill(-child, SIGINT);
						usleep(5000);
						kill(-child, SIGKILL);
						av_set_led(0);
						printf("Power OFF\n");
					} else {
						printf("Power ON\n");
						av_set_led(1);
						break;
					}
				}
			}

			if (((ret=waitpid(child, &status, WNOHANG)) < 0) &&
			    (errno == EINTR)) {
				break;
			}

			if ((ret == child) && (power == 1))
				break;
		}

		if (WIFEXITED(status))
			status = WEXITSTATUS(status);

		switch (status) {
		case 0:
			printf("child exited cleanly\n");
			break;
		default:
			printf("child failed, with status %d, restarting...\n",
			       status);
			if (status == 65) {
				printf("abort theme change\n");
				unlink(DEFAULT_THEME);
			}
			break;
		}
	}
}

/*
 * main()
 */
int
main(int argc, char **argv)
{
	int c, i;
	char *font = NULL;
	int mode = -1, output = -1;
	int tv_aspect = AV_TV_ASPECT_4x3;
	int width, height;
	uint32_t accel = MM_ACCEL_DJBFFT;
	char *theme_file = NULL;
	struct stat sb;
	int opt_index;
	char *tmp;
#ifndef MVPMC_HOST
	unsigned long start = (unsigned long)sbrk(0);
#endif

	char *optparm;
	char optarg_tmp[1024];

	if (version_number[0] != '\0') {
		version = version_number;
	} else if (git_revision[0] != '\0') {
		version = git_revision;
	}

	/*
	 * Initialize to a known state before
	 * eval command line.
	 */
	display_type = DISPLAY_DISABLE;
	mclient_type = MCLIENT_DISABLE;

	if (argc > 32) {
		fprintf(stderr, "too many arguments\n");
		exit(1);
	}
	for (i=0; i<argc; i++)
		saved_argv[i] = strdup(argv[i]);

	tzset();
    
	/*
	 * Allocate a shared memory region so that config options can
	 * survive a crash or restart.
	 */
#ifndef MVPMC_HOST
	if ((shmid=shmget(IPC_PRIVATE, sizeof(config_t), IPC_CREAT)) == -1) {
		perror("shmget()");
		exit(1);
	}
	if ((config=(config_t*)shmat(shmid, NULL, 0)) == (config_t*)-1) {
		perror("shmat()");
		exit(1);
	}
	if ((web_shmid=shmget(IPC_PRIVATE, sizeof(web_config_t), IPC_CREAT)) == -1) {
		perror("shmget()");
		exit(1);
	}
	if ((web_config=(web_config_t*)shmat(web_shmid, NULL, 0)) == (web_config_t*)-1) {
		perror("shmat()");
		exit(1);
	}
#else
	if ((config=(config_t*)malloc(sizeof(config_t))) == NULL) {
		perror("malloc()");
		exit(1);
	}
	if ((web_config=(web_config_t*)malloc(sizeof(web_config_t))) == NULL) {
		perror("malloc()");
		exit(1);
	}
#endif
	memset(config, 0, sizeof(*config));
#ifndef MVPMC_HOST
	config->firsttime = 1;
#endif
	config->magic = CONFIG_MAGIC;


       sizeof_strncpy(mysqlptr->db,"mythconverg");
       sizeof_strncpy(mysqlptr->user,"mythtv");
       sizeof_strncpy(mysqlptr->pass,"mythtv");
       sizeof_strncpy(mysqlptr->host,"localhost");

	/*
	 * Initialize the config options to the defaults.
	 */
	config->screensaver_timeout = screensaver_timeout;
	config->osd_bitrate = osd_settings.bitrate;
	config->osd_clock = osd_settings.clock;
	config->osd_demux_info = osd_settings.demux_info;
	config->osd_program = osd_settings.program;
	config->osd_progress = osd_settings.progress;
	config->osd_timecode = osd_settings.timecode;
	config->brightness = root_bright;
	config->mythtv_sort = mythtv_sort;
	config->mythtv_tcp_control = mythtv_tcp_control;
	config->mythtv_tcp_program = mythtv_tcp_program;
	config->av_audio_output = AUD_OUTPUT_STEREO;
	config->av_video_output = AV_OUTPUT_COMPOSITE;
	config->av_tv_aspect = AV_TV_ASPECT_4x3;
	config->av_mode = AV_MODE_PAL;
    vnc_server[0] = 0;
        
	/*
	 * Parse the command line options.  These settings must override
	 * the settings from all other sources.
	 */
	while ((c=getopt_long(argc, argv,
			      "a:b:C:c:d:D:f:F:hi:m:Mo:r:R:s:S:y:t:u:p:T:",
			      opts, &opt_index)) != -1) {
		switch (c) {
		case 0:
			/*
			 * Copy the parsed argument and look for
			 * parameters (which follow the ':' char).
			 */
			snprintf(optarg_tmp,1024,"%s",optarg);
			optparm = NULL;
			optparm = strchr(optarg_tmp,':');
			if (optparm != NULL) {
				*optparm = 0;
				optparm++;
			}

			if (strcmp(opts[opt_index].name, "no-settings") == 0) {
				settings_disable = 1;
			}
			if (strcmp(opts[opt_index].name, "no-reboot") == 0) {
				reboot_disable = 1;
			}
			if (strcmp(opts[opt_index].name, "no-wss") == 0) {
			    	av_wss_visible(0);
			}
			if (strcmp(opts[opt_index].name, "no-filebrowser") == 0) {
				filebrowser_disable = 1;
			}
			if (strcmp(opts[opt_index].name, "use-mplayer") == 0) {
				mplayer_disable = 0;
			}
			if (strcmp(opts[opt_index].name, "web-port") == 0) {
				web_port = atoi(optarg_tmp);
				if ( web_port == 23 || web_port== 443 || web_port == 8005 ) {
       			    fprintf(stderr, "The port (%d) is not supported!\n",web_port);
				    web_port = 0;
				} 
			}
			if (strcmp(opts[opt_index].name, "version") == 0) {
				printf("MediaMVP Media Center\n");
				printf("http://www.mvpmc.org/\n");
				if (version != NULL) {
					printf("Version: %s\n", version);
				}
				printf("Built by: %s\n", build_user);
				printf("Built at: %s\n", compile_time);
				exit(0);
			}
			if (strcmp(opts[opt_index].name, "vlc") == 0) {
				vlc_server = strdup(optarg);
			}
			if (strcmp(opts[opt_index].name, "emulate") == 0) {
				mvp_server = strdup(optarg);
			}
			if (strcmp(opts[opt_index].name, "rfb") == 0) {
				rfb_mode = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "flicker") == 0) {
				flicker = 1;
			}
			if (strcmp (opts[opt_index].name, "startup") == 0) {
			/*
			 * Decode the "startup" option parameter.
			 */
				if (strcmp (optarg_tmp, "replaytv") == 0) {
					startup_this_feature = MM_REPLAYTV;
			      	}
			    	else if (strcmp (optarg_tmp, "mythtv") == 0) {
					startup_this_feature = MM_MYTHTV;
			      	}
			    	else if (strcmp (optarg_tmp, "mclient") == 0) {
					startup_this_feature = MM_MCLIENT;
			      	}
			    	else if (strcmp (optarg_tmp, "vnc") == 0) {
					startup_this_feature = MM_VNC;
			      	}
			    	else if (strcmp (optarg_tmp, "emulate") == 0) {
					startup_this_feature = MM_EMULATE;
			      	}
			    	else if (strcmp (optarg_tmp, "settings") == 0) {
                        #ifndef MVPMC_HOST
	                    if (config->firsttime == 1)
                        #endif
				        startup_this_feature = MM_SETTINGS;
			      	}
			    	else if (strcmp (optarg_tmp, "about") == 0) {
					startup_this_feature = MM_ABOUT;
				}
			    	else if (strcmp (optarg_tmp, "filesystem") == 0) {
					startup_this_feature = MM_FILESYSTEM;
					/*
					 * Decode the "filesystem" option parameter.
					 */
					if(optparm != NULL) {
						if (strncmp (optparm, "file=", strlen("file=")) == 0) {
							optparm += strlen("file=");
							snprintf(cwd,1024,"%s",optparm);
						}
					}
			      	}
			}
			break;
		case 'a':
			if (strcmp(optarg, "4:3cco") == 0) {
				tv_aspect = AV_TV_ASPECT_4x3_CCO;
			} else if (strcmp(optarg, "4:3") == 0) {
				tv_aspect = AV_TV_ASPECT_4x3;
			} else if (strcmp(optarg, "16:9") == 0) {
				tv_aspect = AV_TV_ASPECT_16x9;
			} else if (strcmp(optarg, "16:9auto") == 0) {
			   	//Legacy option, treat same as 16:9
				tv_aspect = AV_TV_ASPECT_16x9;
			} else {
				fprintf(stderr, "unknown aspect ratio '%s'\n",
					optarg);
				print_help(argv[0]);
				exit(1);
			}
			config->bitmask |= CONFIG_TV_ASPECT;
			config->av_tv_aspect = tv_aspect;
			break;
		case 'b':
			mythtv_ringbuf = strdup(optarg);
			break;
		case 'C':
			screen_capture_file = strdup(optarg);
			break;
		case 'd':
			if (strcmp(optarg, "disable") == 0) {
				display_type = DISPLAY_DISABLE;
			} else if (strcmp(optarg, "IEE16x1") == 0) {
				display_type = DISPLAY_IEE16X1;
			} else if (strcmp(optarg, "IEE40x2") == 0) {
				display_type = DISPLAY_IEE40X2;
			} else {
				fprintf(stderr, "The local display type (%s) is not recognized!\n",optarg);
				print_help(argv[0]);
				exit(1);
			}
			break;
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		case 'i':
			imagedir = strdup(optarg);
			break;
		case 'f':
			font = strdup(optarg);
			break;
		case 'F':
			config_file = strdup(optarg);
			break;
		case 'm':
			if (strcasecmp(optarg, "pal") == 0) {
				mode = AV_MODE_PAL;
			} else if (strcasecmp(optarg, "ntsc") == 0) {
				mode = AV_MODE_NTSC;
			} else {
				fprintf(stderr, "unknown mode '%s'\n", optarg);
				print_help(argv[0]);
				exit(1);
			}
			config->bitmask |= CONFIG_MODE;
			config->av_mode = mode;
			break;
		case 'M':
			fprintf(stderr, "Turn on MythTV debugging\n");
			mythtv_debug = 1;
			break;
		case 'o':
			if (strcasecmp(optarg, "svideo") == 0) {
				output = AV_OUTPUT_SVIDEO;
				config->av_video_output = output;
				config->bitmask |= CONFIG_VIDEO_OUTPUT;
			} else if (strcasecmp(optarg, "composite") == 0) {
				output = AV_OUTPUT_COMPOSITE;
				config->av_video_output = output;
				config->bitmask |= CONFIG_VIDEO_OUTPUT;
			} else if (strcasecmp(optarg, "stereo") == 0) {
				audio_output_mode = AUD_OUTPUT_STEREO;
				config->av_audio_output = audio_output_mode;
				config->bitmask |= CONFIG_AUDIO_OUTPUT;
			} else if (strcasecmp(optarg, "passthru") == 0) {
				audio_output_mode = AUD_OUTPUT_PASSTHRU;
				config->av_audio_output = audio_output_mode;
				config->bitmask |= CONFIG_AUDIO_OUTPUT;
			} else {
				fprintf(stderr, "unknown output '%s'\n",
					optarg);
				print_help(argv[0]);
				exit(1);
			}
			break;
		case 'r':
			mythtv_recdir = strdup(optarg);
			break;
		case 'R':
			replaytv_server = "RTV";
			rtv_init_str = strdup(optarg);
			break;
		case 's':
			mythtv_server = strdup(optarg);
			sizeof_strncpy(config->mythtv_ip, optarg);
			config->bitmask |= CONFIG_MYTHTV_IP;
			break;
                case 'y':
                        tmp = strdup(optarg);
			sizeof_strncpy(mysqlptr->host, tmp);
                        fprintf (stderr, "mysqlhost = %s\n",mysqlptr->host);
			break;
                case 'u':
                        tmp = strdup(optarg);
			sizeof_strncpy(mysqlptr->user,tmp);
                        fprintf (stderr, "mysqluser = %s\n",mysqlptr->user);
                        break;
                case 'p':
                        tmp = strdup(optarg);
			sizeof_strncpy(mysqlptr->pass,tmp);
                        fprintf (stderr, "mysqlpass = %s\n",mysqlptr->pass);
                        break;
                case 'T':
                        tmp = strdup(optarg);
			sizeof_strncpy(mysqlptr->db,tmp);
                        fprintf (stderr, "mysqltable = %s\n",mysqlptr->db);
                        break;
		case 'D':
			if (sscanf(optarg, "%[^:]:%d", vnc_server, &vnc_port) != 2) {
				printf("Incorrectly formatted VNC server '%s'\n", optarg);
				print_help(argv[0]);
				exit(1);
			}

			if (vnc_port < 100)
				vnc_port += VNC_SERVERPORT;
			break;	
		case 'S':
			i = atoi(optarg);
			if ((i < 0) || (i > 3600)) {
				fprintf(stderr,
					"Invalid screeensaver timeout!\n");
				print_help(argv[0]);
				exit(1);
			}
			screensaver_default = i;
			screensaver_timeout = i;
			config->screensaver_timeout = screensaver_timeout;
			config->bitmask |= CONFIG_SCREENSAVER;
			break;
		case 't':
			theme_file = strdup(optarg);
			if (stat(theme_file, &sb) != 0) {
				perror(theme_file);
				exit(1);
			}
			break;
		case 'c':
			mclient_type = MCLIENT;
			mclient_server = strdup(optarg);
			sizeof_strncpy(config->mclient_ip, optarg);
			config->bitmask |= CONFIG_MCLIENT_IP;
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}
	if (web_port==-1) {
        if ( settings_disable == 1) {
            web_port = 0;
        } else {
            web_port = 80;
        }
    }

#ifndef MVPMC_HOST
	theme_parse(MASTER_THEME);
#endif

	if (theme_file) {
		for (i=0; i<THEME_MAX; i++) {
			if (theme_list[i].path == NULL) {
				theme_list[i].path = strdup(theme_file);
				break;
			}
			if (strcmp(theme_list[i].path, theme_file) == 0)
				break;
		}
		if (i == THEME_MAX) {
			fprintf(stderr, "too many theme files!\n");
			exit(1);
		}
#ifndef MVPMC_HOST
		unlink(DEFAULT_THEME);
		if (symlink(theme_file, DEFAULT_THEME) != 0)
			return -1;
#endif
	}

	/*
	 * Load the settings from the config file, without allowing them to
	 * override the command line settings.
	 */
	if (config_file)
		load_config_file(config_file, 0);

#ifndef MVPMC_HOST
	spawn_child();
#endif

	if (web_port) {
		load_web_config(font);
	}

	/*
	 * Make sure each copy of the child prints the version info.
	 */
	if (version != NULL) {
		printf("MediaMVP Media Center\nVersion %s\n%s",
		       version, compile_time);
	} else {
		printf("MediaMVP Media Center\nVersion Unknown\n%s",
		       compile_time);
	}

	srand(getpid());

	if (config->magic != CONFIG_MAGIC) {
		fprintf(stderr, "invalid config area!\n");
		exit(1);
	}

	signal(SIGPIPE, SIG_IGN);

#ifndef MVPMC_HOST
	if ((theme_file == NULL) && (config->bitmask & CONFIG_THEME)) {
		theme_parse(config->theme);
	} else {
		if ((stat(DEFAULT_THEME, &sb) == 0) && (sb.st_size > 0)) {
			theme_parse(DEFAULT_THEME);
		} else {
			if (font == NULL)
				font = DEFAULT_FONT;
		}
	}
#else
	if (theme_file)
		theme_parse(theme_file);
#endif

	if (font)
		fontid = mvpw_load_font(font);

	if ((demux_mode=av_init()) == AV_DEMUX_ERROR) {
		fprintf(stderr, "failed to initialize av hardware!\n");
		exit(1);
	}

	/*
	 * Update the config settings from the shared memory region.
	 */
	set_config();

    if (web_port) {
        reset_web_config();
    }

#ifndef MVPMC_HOST
	if(config->firsttime)
	{
	    config->firsttime = 0;
	    fprintf(stderr,"Re-starting to convince aspect ratio stuff to work...\n");
	    re_exec();
	}
#endif

	if (rtv_init_str) {
		replaytv_init(rtv_init_str);
	}

	if (mode != -1)
		av_set_mode(mode);

	if (av_get_mode() == AV_MODE_PAL) {
		printf("PAL mode, 720x576\n");
		width = 720;
		height = 576;
	} else {
		printf("NTSC mode, 720x480\n");
		width = 720;
		height = 480;
	}
	osd_set_screen_size(width, height);

	if (mw_init(mythtv_server, replaytv_server) < 0) {
		fprintf(stderr, "failed to initialize microwindows!\n");
		exit(1);
	}

	fd_audio = av_get_audio_fd();
	fd_video = av_get_video_fd();
	av_attach_fb();
	av_play();

	av_set_volume(volume);

	a52_state = a52_init (accel);

	if (!(config->bitmask & CONFIG_TV_ASPECT) && (tv_aspect != -1))
		av_set_tv_aspect(tv_aspect);

	if (!(config->bitmask & CONFIG_VIDEO_OUTPUT) && (output != -1))
		av_set_output(output);

	if ((handle=demux_init(1024*1024*2.5)) == NULL) {
		fprintf(stderr, "failed to initialize demuxer\n");
		exit(1);
	}

        if ((tshandle=ts_demux_init()) == NULL) {
                fprintf(stderr, "failed to initialize TS demuxer\n");
                exit(1);
        }

	demux_set_display_size(handle, width, height);

	video_init();

	pthread_attr_init(&thread_attr);
	pthread_attr_setstacksize(&thread_attr, 1024*64);
	pthread_attr_init(&thread_attr_small);
	pthread_attr_setstacksize(&thread_attr_small, 1024*8);

	/*
	 * If the demuxer is not being used, all mpeg data will go
	 * through the video device.  This will allow an external
	 * player to be used (ie, mplayer).
	 */
	if (demux_mode == AV_DEMUX_ON) {
		DEMUX_PUT = demux_put;
		DEMUX_WRITE_VIDEO = demux_write_video;
		DEMUX_WRITE_AUDIO = demux_write_audio;
		pthread_create(&audio_write_thread, &thread_attr,
			       audio_write_start, NULL);
	} else {
		DEMUX_PUT = buffer_put;
		DEMUX_WRITE_VIDEO = buffer_write;
		DEMUX_WRITE_AUDIO = buffer_write;
		if ((data=malloc(DATA_SIZE)) == NULL) {
			perror("malloc()");
			exit(1);
		}
	}

#ifndef MVPMC_HOST
	/*
	 * Allocate a bunch of pages followed by a guard page to ensure
	 * that mvpmc doesn't get sluggish after running for a while.
	 */
	{
#define NPAGES	1024
		unsigned long heap = (unsigned long)sbrk(0);
		char *pages[NPAGES], *buffer = NULL;
		int i, sz, k = 0;

		sz = getpagesize();
		printf("approximate heap size %ld\n", heap-start);
		for (i=0; i<NPAGES; i++) {
			if ((pages[i]=malloc(sz)) != NULL) {
				memset(pages[i], 0, sz);
				k++;
			} else {
				if (i == 0) {
					fprintf(stderr, "out of memory!\n");
					exit(1);
				} else {
					printf("Stealing guard page\n");
					buffer = pages[i-1];
					pages[i-1] = NULL;
					k--;
					break;
				}
			}
		}
		printf("Allocated %d heap bytes\n", sz*k);
		if (!buffer && ((buffer=malloc(sz)) != NULL)) {
			memset(buffer, 0, sz);
			printf("Allocated guard page\n");
		}
		for (i=0; i<k; i++) {
			if (pages[i])
				free(pages[i]);
		}
	}
#endif /* !MVPMC_HOST */

#ifdef CONFIG_MYTHTV_IP
	if (strcmp(mysqlptr->host,"localhost") == 0) {
		sizeof_strncpy(mysqlptr->host,config->mythtv_ip);
	}
#endif

	pthread_create(&video_read_thread, &thread_attr_small,
		       video_read_start, NULL);
	pthread_create(&video_write_thread, &thread_attr_small,
		       video_write_start, NULL);
	pthread_create(&audio_thread, &thread_attr_small,
		       audio_start, NULL);
	pthread_create(&video_events_thread, &thread_attr_small,
			video_events_start, NULL);

#ifndef MVPMC_HOST
	if (display_init() < 0) {
		fprintf(stderr, "failed to initialize display driver!\n");
		exit(1);
	}
#endif

	if (gui_init(mythtv_server, replaytv_server) < 0) {
		fprintf(stderr, "failed to initialize gui!\n");
		exit(1);
	}

	if (music_client() < 0) {
		fprintf(stderr, "failed to start up music client!\n");
		exit(1);
	}
	if (web_port) {
        web_server = 1;
	    pthread_create(&web_server_thread, &thread_attr_small,www_mvpmc_start, NULL);
	}

	atexit(atexit_handler);

	mvpw_set_idle(NULL);
	mvpw_event_loop();

	return 0;
}

void
re_exec(void)
{
	/*
	 * exiting will allow the parent to do another fork()...
	 */
	exit(0);
}

void
switch_hw_state(mvpmc_state_t new)
{
	if (new == hw_state)
		return;

	printf("%s(): changing from %d to %d\n", __FUNCTION__, hw_state, new);

	switch (hw_state) {
	case MVPMC_STATE_NONE:
		break;
	case MVPMC_STATE_FILEBROWSER:
	case MVPMC_STATE_HTTP:
		fb_exit();
		break;
	case MVPMC_STATE_MYTHTV:
		mythtv_exit();
		break;
	case MVPMC_STATE_REPLAYTV:
		replaytv_exit();
		break;
	case MVPMC_STATE_MCLIENT:
		mclient_exit();
		break;
    case MVPMC_STATE_EMULATE:
        break;
	}

	hw_state = new;
}

void
switch_gui_state(mvpmc_state_t new)
{
	if (new == gui_state)
		return;
    
    if (new==MVPMC_STATE_REPLAYTV && web_port==80) {
        web_server = 0;
    } else if (gui_state==MVPMC_STATE_REPLAYTV && web_port==80 ) {
        web_server = 1;
        pthread_create(&web_server_thread, &thread_attr_small,www_mvpmc_start, NULL);
    }

	printf("%s(): changing from %d to %d\n", __FUNCTION__, gui_state, new);

	gui_state = new;
}

void
atexit_handler(void)
{
	printf("%s(): hw_state=%d gui_state=%d\n", __FUNCTION__, hw_state, gui_state);

	switch (hw_state) {
	case MVPMC_STATE_NONE:
		break;
	case MVPMC_STATE_FILEBROWSER:
	case MVPMC_STATE_HTTP:
		fb_exit();
		break;
	case MVPMC_STATE_MYTHTV:
		mythtv_atexit();
		break;
	case MVPMC_STATE_REPLAYTV:
		break;
	case MVPMC_STATE_MCLIENT:
		break;
    case MVPMC_STATE_EMULATE:
        break;

	}

	switch (gui_state) {
	case MVPMC_STATE_NONE:
		break;
	case MVPMC_STATE_HTTP:
	case MVPMC_STATE_FILEBROWSER:
		break;
	case MVPMC_STATE_MYTHTV:
		break;
	case MVPMC_STATE_REPLAYTV:
		replaytv_atexit();
		break;
	case MVPMC_STATE_MCLIENT:
		break;
    case MVPMC_STATE_EMULATE:
        break;

	}

	printf("%s(): exiting...\n", __FUNCTION__);
}

/* credits to 
 * tiny.c - a minimal HTTP server that serves static and
 *          Dave O'Hallaron, Carnegie Mellon
 * http://www.cs.cmu.edu/afs/cs/academic/class/15213-s00/www/class28/tiny.c
 * used for opening socket
 *
 * micro_httpd - really small HTTP server
 *             ACME Labs Software
 * http://www.acme.com/software/micro_httpd/
 *            
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <dirent.h>

/* define DEBUGDOT if you want to see the server make progress */
#define DEBUGDOTx

/* define DEBUGCGI if you want to see CGI script err msgs on screen */
#define DEBUGCGIx

#define BUFSIZE 1024
#define MAXERRS 16

/* externally defined globals */
//extern char **environ; /* the environment from libc */

/*
 * error - wrapper for perror used for bad syscalls
 */
void error(char *msg) {
    perror(msg);
    exit(1);
}

#define SERVER_NAME "MediaMVP PC Config"
#define SERVER_URL "/"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

/* Forwards. */
static void file_details(FILE *stream, char* dir, char* name );
static void send_error(FILE *stream, int status, char* title, char* extra_header, char* text );
static void send_headers(FILE *stream, int status, char* title, char* extra_header, char* mime_type, off_t length, time_t mod );
static char* get_mime_type( char* name );
static void strdecode( char* to, char* from );
static void post_post_garbage_hack( FILE *stream,int conn_fd );
static void set_ndelay( int fd );
static void clear_ndelay( int fd );


static int hexit( char c );
void strencode( char* to, size_t tosize, const char* from );
int mvp_load_data(FILE *stream,char *);
int mvp_config_radio(char *);
int mvp_config_general(char *);
int mvp_config_script(char *);
int mvp_load_radio_playlist(FILE * stream);
void output_playlist_html(FILE *stream,int lineno,char *title,char *url);


void *
www_mvpmc_start(void *arg) {
    /* variables for connection management */
    int listenfd;          /* listening socket */
    int connfd;            /* connection socked */
    unsigned int clientlen;/* byte size of client's address */
    int optval;            /* flag value for setsockopt */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int requestno;        /* how many connections have we recieved? */
    FILE *stream;          /* stream version of connfd */

    /* variables for connection I/O */


    char line[10000], method[10000], path[10000], protocol[10000], idx[20000], location[20000];
    char* file;
    size_t len;
    struct stat sb;
    FILE* fp;
    struct dirent **dl;
    static char mytoken[20]="^";


    int contentType;

    int i, n;

    /* open socket descriptor */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        error("ERROR opening socket");

    /* allows us to restart server immediately */
    optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval , sizeof(int));

    /* bind port to socket */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)web_port);
    if (bind(listenfd, (struct sockaddr *) &serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    /* get us ready to accept connection requests */
    if (listen(listenfd, 5) < 0) /* allow 5 requests to queue up */
        error("ERROR on listen");

    /*
     * main loop: wait for a connection request, parse HTTP,
     * serve requested content, close connection.
     */
    clientlen = sizeof(clientaddr);
    requestno = 0;
    file = NULL;
    printf("web server thread started (pid %d)\n", getpid());
                                                   
    fd_set read_fds;
    FD_ZERO (&read_fds);

    while (web_server == 1) {
        struct timeval tv;

        int n1 = 0;

        FD_ZERO (&read_fds);
        FD_SET (listenfd, &read_fds);
        if (listenfd > n1)
            n1 = listenfd;

        /*
         * Wait until we receive data from server or up to 100ms
         * (1/10 of a second).
         */

        tv.tv_usec = 100000;

        if (select (n1 + 1, &read_fds, NULL, NULL, &tv) == -1) {
            if (errno != EINTR){
                abort ();
            }
        }
        if (FD_ISSET (listenfd, &read_fds)) {
            /* wait for a connection request */
            connfd = accept(listenfd, (struct sockaddr *) &clientaddr, &clientlen);
            if (connfd < 0)
                error("ERROR on accept");
    
            requestno++;
    
            /* open the child socket descriptor as a stream */
            if ((stream = fdopen(connfd, "r+")) == NULL)
                error("ERROR on fdopen");
    
            if ( fgets( line, sizeof(line), stream ) == (char*) 0 ) {
                send_error(stream, 400, "Bad Request", (char*) 0, "No request found." );
                continue;
            }
            printf("%s\n",line);
            if ( sscanf( line, "%[^ ] %[^ ] %[^ ]", method, path, protocol ) != 3 ) {
                send_error(stream, 400, "Bad Request", (char*) 0, "Can't parse request." );
                continue;
            }
            contentType = 0;
            int conlen = 0;
            int reset;
            char *ptr;
            reset = 0;
    
            while ( fgets( line, sizeof(line), stream ) != (char*) 0 ) {
                if (strstr(line,"application/x-www-form-urlencoded")!=NULL ) {
                    contentType = 1;
                }
                ptr = strstr(line,"Content-Length:");
                if (ptr !=NULL ) {
                    ptr+=15;
                    conlen = atoi(ptr);
                    if (conlen> 5000) {
                        break;
                    }
                }
            
                if ( strcmp( line, "\n" ) == 0 || strcmp( line, "\r\n" ) == 0 )
                    break;
            }
    
            if ( conlen> 5000 || (strcasecmp( method, "get" ) != 0  && strcasecmp( method, "post" ) != 0) ) {
                send_error(stream, 501, "Not Implemented", (char*) 0, "That method is not implemented." );
                continue;
            }
            if ( contentType == 1 ) {
                contentType = 0;
                int cc;
                cc = fread(line,sizeof(char),conlen,stream);
                post_post_garbage_hack(stream,connfd);
                line[cc]=0;
                ptr = strstr(line,"token=");
                if ( ptr != NULL) {
                    ptr+=6;
                    strdecode(ptr,ptr);
                    if (strncmp(ptr,mytoken,strlen(mytoken))!=0) {
                        send_error(stream, 400, "Request Timeout", (char*) 0, "POST Outdated<BR />Please resubmit mvpmc update" );
                        continue;
                    }
                    if (strcmp(path,"/radio")==0 ) {
                        reset = mvp_config_radio(line);                   
                    } else if (strcmp(path,"/config")==0 ) {
                        mvp_config_general(line);
                    } else if (strcmp(path,"/mount")==0 ) {
                        mvp_config_script(line);                
                    } else {
                        send_error(stream, 400, "Bad request", (char*) 0, "POST received not supported" );
                        continue;                
                    }                
                } else {
                    send_error(stream, 400, "Bad request", (char*) 0, "POST received but not accepted" );
                    continue;
    
                }
                send_headers(stream, 202, "OK", (char*) 0, "text/html", -1, sb.st_mtime );
                (void) fprintf(stream, "<html><body><H1>Changes accepted</H1><BR /><a href=\"%s\">%s</a></address>\n</body></html>\n", "/", "Return to configuration");                
                fflush(stream);
                fclose(stream);
                if (reset==0x3d) {
                    printf("Power off from browser\n");
                    web_config->control = reset;
                }
                continue;
            }
    
            if ( path[0] != '/' ) {
                send_error(stream, 400, "Bad Request", (char*) 0, "Bad filename." );
                continue;
            }
            file = &(path[1]);
            strdecode( file, file );
            if ( file[0] == '\0' ) {
                file = "/usr/share/mvpmc/setup.html";
            } else {
                snprintf(method,256,"/usr/share/mvpmc/%s",file);
                file = method;
            }
            len = strlen( file );
            if ( strstr( file, ".." )!=NULL) {
                send_error(stream,  400, "Bad Request", (char*) 0, "Illegal filename." );
                continue;
            }
            if ( stat( file, &sb ) < 0 ) {
                send_error(stream, 404, "Not Found", (char*) 0, "File not found." );
                continue;
            }
            if ( S_ISDIR( sb.st_mode ) ) {
                if ( file[len-1] != '/' ) {
                    (void) snprintf(
                                   location, sizeof(location), "Location: %s/", path );
                    send_error(stream, 302, "Found", location, "Directories must end with a slash." );
                    continue;
                }
                (void) snprintf( idx, sizeof(idx), "%sindex.html", file );
                if ( stat( idx, &sb ) >= 0 ) {
                    file = idx;
                    goto do_file;
                }
                send_headers(stream, 200, "Ok", (char*) 0, "text/html", -1, sb.st_mtime );
                (void) fprintf(stream,"<html><head><title>Index of %s</title></head>\n<body bgcolor=\"#99cc99\"><h4>Index of %s</h4>\n<pre>\n", file, file );
                n = scandir( file, &dl, NULL, alphasort );
                if ( n < 0 )
                    perror( "scandir" );
                else
                    for ( i = 0; i < n; ++i )
                        file_details(stream, file, dl[i]->d_name );
                (void) fprintf(stream, "</pre>\n<hr>\n<address><a href=\"%s\">%s</a></address>\n</body></html>\n", SERVER_URL, SERVER_NAME );
            } else {
                do_file:
                fp = fopen( file, "r" );
                if ( fp == (FILE*) 0 ) {
                    send_error(stream, 403, "Forbidden", (char*) 0, "File is protected." );
                    continue;
                }
                send_headers(stream, 200, "Ok", (char*) 0, get_mime_type( file ), sb.st_size, sb.st_mtime );
    
                if ( strcasecmp( method, "get" ) == 0   ) {
                    snprintf(mytoken,20,"%X*%X",rand(),rand());
                }
                if (strstr(file,".htm")==NULL ) {
                    int ich;
                    while ( ( ich = getc( fp ) ) != EOF ){
                        fprintf(stream,"%c", ich );
                    }
                } else {
                    while (fgets(location,1024,fp)!=NULL) {
                        if (strstr(location,"<FORM method = \"POST\" ACTION=\"")!=NULL ) {
                            fprintf(stream,"%s", location);
                            fprintf(stream,"<input type=\"hidden\" name=\"token\" value=\"%s\" />\n",mytoken);
                        } else {
                            mvp_load_data(stream,location);
                        }
                    }
    
                }
            }
    
            /* clean up */
            fclose(stream);
        }
    }
    close(listenfd);
    return NULL;

}


static void
file_details(FILE *stream, char* dir, char* name )
{
    static char encoded_name[1000];
    static char path[2000];
    struct stat sb;
    char timestr[16];

    strencode( encoded_name, sizeof(encoded_name), name );
    (void) snprintf( path, sizeof(path), "%s/%s", dir, name );
    if ( lstat( path, &sb ) < 0 )
        (void) fprintf(stream, "<a href=\"%s\">%-32.32s</a>    ???\n", encoded_name, name );
    else {
        (void) strftime( timestr, sizeof(timestr), "%d%b%Y %H:%M", localtime( &sb.st_mtime ) );
        (void) fprintf(stream, "<a href=\"%s\">%-32.32s</a>    %15s %14lld\n", encoded_name, name, timestr, (int64_t) sb.st_size );
    }
}


static void
send_error(FILE *stream, int status, char* title, char* extra_header, char* text )
{
    send_headers(stream, status, title, extra_header, "text/html", -1, -1 );
    (void) fprintf(stream, "<html><head><title>%d %s</title></head>\n<body bgcolor=\"#cc9999\"><h4>%d %s</h4>\n", status, title, status, title );
    (void) fprintf(stream, "%s\n", text );
    (void) fprintf(stream, "<hr>\n<address><a href=\"%s\">%s</a></address>\n</body></html>\n", SERVER_URL, SERVER_NAME );
    fclose(stream);
}


static void
send_headers(FILE *stream, int status, char* title, char* extra_header, char* mime_type, off_t length, time_t mod )
{
    time_t now;
    char timebuf[100];

    (void) fprintf(stream, "%s %d %s\015\012", PROTOCOL, status, title );
    (void) fprintf(stream, "Server: %s\015\012", SERVER_NAME );
    now = time( (time_t*) 0 );
    (void) strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );
    (void) fprintf(stream, "Date: %s\015\012", timebuf );
    if ( extra_header != (char*) 0 )
        (void) fprintf(stream, "%s\015\012", extra_header );
    if ( mime_type != (char*) 0 )
        (void) fprintf(stream, "Content-Type: %s\015\012", mime_type );
    if ( length >= 0 ) {
        (void) fprintf(stream, "Content-Length: %lld\015\012", (int64_t) 50000  );
    }
    if ( mod != (time_t) -1 ) {
        (void) strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &mod ) );
        (void) fprintf(stream, "Last-Modified: %s\015\012", timebuf );
    }
    (void) fprintf(stream, "Connection: close\015\012" );
    (void) fprintf(stream, "\015\012" );
}


static char*
get_mime_type( char* name )
{
    char* dot;

    dot = strrchr( name, '.' );
    if ( dot == (char*) 0 || strcmp( dot, ".log" )==0)
        return "text/plain; charset=iso-8859-1";
    if ( strcmp( dot, ".html" ) == 0 || strcmp( dot, ".htm" ) == 0 )
        return "text/html; charset=iso-8859-1";
    if ( strcmp( dot, ".jpg" ) == 0 || strcmp( dot, ".jpeg" ) == 0 )
        return "image/jpeg";
    if ( strcmp( dot, ".gif" ) == 0 )
        return "image/gif";
    if ( strcmp( dot, ".png" ) == 0 )
        return "image/png";
    if ( strcmp( dot, ".css" ) == 0 )
        return "text/css";
    if ( strcmp( dot, ".wav" ) == 0 )
        return "audio/wav";
    return "text/plain; charset=iso-8859-1";
}

static void
strdecode( char* to, char* from )
{
    for ( ; *from != '\0'; ++to, ++from ) {
        if ( from[0] == '%' && isxdigit( from[1] ) && isxdigit( from[2] ) ) {
            *to = hexit( from[1] ) * 16 + hexit( from[2] );
            from += 2;
        } else if (*from =='+') {
            *to = ' ';
        } else {
            *to = *from;
        }
    }
    *to = '\0';
}


static int
hexit( char c )
{
    if ( c >= '0' && c <= '9' )
        return c - '0';
    if ( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return 0;       /* shouldn't happen, we're guarded by isxdigit() */
}


void strencode( char* to, size_t tosize, const char* from )
{
    int tolen;

    for ( tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from ) {
        if ( isalnum(*from) || strchr( "/_.-~", *from ) != (char*) 0 ) {
            *to = *from;
            ++to;
            ++tolen;
        } else {
            (void) sprintf( to, "%%%02x", (int) *from & 0xff );
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}



/* Special hack to deal with broken browsers that send a LF or CRLF
** after POST data, causing TCP resets - we just read and discard up
** to 2 bytes.  Unfortunately this doesn't fix the problem for CGIs
** which avoid the interposer process due to their POST data being
** short.  Creating an interposer process for all POST CGIs is
** unacceptably expensive.
*/
static void post_post_garbage_hack( FILE *stream,int conn_fd )
{
    char buf[2];
    set_ndelay( conn_fd );
    fread(buf, sizeof(buf),1,stream );
    clear_ndelay( conn_fd );
}

/* Set NDELAY mode on a socket. */
static void set_ndelay( int fd )
{
    int flags, newflags;
    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 ){
	newflags = flags | (int) O_NONBLOCK;
	if ( newflags != flags )
	    (void) fcntl( fd, F_SETFL, newflags );
    }
}


/* Clear NDELAY mode on a socket. */
static void clear_ndelay( int fd )
{
    int flags, newflags;
    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 ){
	newflags = flags & ~ (int) O_NONBLOCK;
	if ( newflags != flags )
	    (void) fcntl( fd, F_SETFL, newflags );
    }
}

#define WEB_CONFIG_FILENAME "/etc/browser.config"

#define WEB_CONFIG_TZ          400
#define WEB_CONFIG_TIMESERVER  401
#define WEB_CONFIG_CWD         500
#define WEB_CONFIG_MCLIENT_IP  501
#define WEB_CONFIG_VNC_SERVER  502
#define WEB_CONFIG_VNC_PORT    503
#define WEB_CONFIG_MYTHTV_IP   504
#define WEB_CONFIG_STARTUP     505
#define WEB_CONFIG_MYTH_RECORD 506
#define WEB_CONFIG_MYTH_RING   507
#define WEB_CONFIG_CAPTURE     508
#define WEB_CONFIG_IMAGE       509
#define WEB_CONFIG_RTV_INIT    510
#define WEB_CONFIG_FONT        511
#define WEB_CONFIG_LIVE_USER   512
#define WEB_CONFIG_LIVE_PASS   513
#define WEB_CONFIG_PLAYLIST    514
#define WEB_CONFIG_NET_USER    515
#define WEB_CONFIG_NET_PASS    516
#define WEB_CONFIG_NET_IP1     517
#define WEB_CONFIG_NET_IP2     518
#define WEB_CONFIG_NET_IP3     519
#define WEB_CONFIG_NET_LOCAL1  520
#define WEB_CONFIG_NET_LOCAL2  521
#define WEB_CONFIG_NET_LOCAL3  522
#define WEB_CONFIG_NET_REMOTE1 523
#define WEB_CONFIG_NET_REMOTE2 524
#define WEB_CONFIG_NET_REMOTE3 525
#define WEB_CONFIG_VLC_SERVER  526
#define WEB_CONFIG_MVP_SERVER  527
#define WEB_CONFIG_RFB_MODE    528
#define WEB_CONFIG_FLICKER     529

#define WEB_CONFIG_USE_MYTH      600
#define WEB_CONFIG_USE_REPLAY    601
#define WEB_CONFIG_USE_MCLIENT   602
#define WEB_CONFIG_USE_VNC       603
#define WEB_CONFIG_USE_REBOOT    604
#define WEB_CONFIG_USE_SETTING   605
#define WEB_CONFIG_USE_FILE      606
#define WEB_CONFIG_USE_VLC       607
#define WEB_CONFIG_USE_MPLAYER   608
#define WEB_CONFIG_USE_MYTHDEBUG 609
#define WEB_CONFIG_USE_EMULATE   610

#define WEB_CONFIG_START_MAIN    620
#define WEB_CONFIG_START_MYTH    621
#define WEB_CONFIG_START_FILE    622
#define WEB_CONFIG_START_VNC     624
#define WEB_CONFIG_START_SETTING 625
#define WEB_CONFIG_START_REPLAY  626
#define WEB_CONFIG_START_MCLIENT 627
#define WEB_CONFIG_START_EMULATE 628



#define WEB_CONFIG_DEFAULT_M3U   "reserved 700"

#define WEB_CONFIG_STATUS             800
#define WEB_CONFIG_CREATE_PLAYLIST   1000
#define WEB_CONFIG_EDIT_PLAYLIST     1001


#define IS_WEB_ENABLED(x) (web_config->bitmask & (1 << (x-WEB_CONFIG_USE_MYTH)) )
#define NOT_WEB_ENABLED(x) (0==(IS_WEB_ENABLED(x)))
void playlist_key_callback(mvp_widget_t *widget, char key);
void playlist_change(playlist_t *next);
void fb_key_callback(mvp_widget_t *widget, char key);

extern playlist_t *playlist_head;

int mvp_config_radio(char *line)
{
    int rc = 0;
    char form_value[1024];
    char title[51];
    char *ptr,*equals;
    char *duplicate;
    int  stateSubmit = 0;
    FILE *fp;

    duplicate = strdup(line);
    ptr = strtok(duplicate,"&");
    equals = NULL;
    while (ptr!=NULL) {
        if (strstr(ptr,"=Play")!=NULL ){
            stateSubmit = 1;
        } else if (strncmp(ptr,"Control=",8)==0 ){
            printf("%s\n",ptr);
            stateSubmit = 2;
        }
        if (stateSubmit != 0 ) {
            strdecode( form_value, ptr );
            equals = strchr(form_value,'=');
            if (equals == NULL) {
                stateSubmit = -1;
                rc = -1;
            } else {
                equals++;
            }
            break;
        }
        ptr = strtok(NULL,"&");
        
    }
    free(duplicate);

    switch (stateSubmit) {
        case 0  :
            printf("%s\n",web_config->playlist);
            fp = fopen( web_config->playlist, "w" );
            fprintf(fp,"#EXTM3U\n");
            ptr = strtok(line,"&");
            title[0]=0;
            while (ptr!=NULL) {
                if ( strlen(ptr) > 1024) {
                    rc = -1;
                    break;
                } else {
                    strdecode( form_value, ptr );
                    equals = strchr(form_value,'=');
                    if (equals== NULL) {
                        rc = -1;
                        break;
                    }
                    equals++;
                    if (strncmp(form_value,"desc",4)==0) {
                        snprintf(title,50,"%s",equals);
                    } else if (strncmp(form_value,"url",3)==0 ) {
                        if ( strlen(equals) > 0 && strlen(equals) <= 128 ) {
        		           if ( strlen(title)==0 ) {
        		                fprintf(fp,"#EXTINF:-1,%50s\n",equals);
                            } else {
        	                    fprintf(fp,"#EXTINF:-1,%s\n",title);
                            }
        	                fprintf(fp,"%s\n",equals);
        		        }
                        title[0]=0;
                    }
                    ptr = strtok(NULL,"&");
                }
            } 
            fclose(fp);
            break;
        case 1:
           if (audio_playing== 0 &&  gui_state == MVPMC_STATE_FILEBROWSER ) {
                strdecode( form_value, ptr );
                equals = strchr(form_value,'=');
                if (equals!= NULL) {
                    *equals = 0;
                    equals = form_value;
                    ptr = strrchr(web_config->playlist,'/') ;
                    if (ptr!=NULL) {
                        ptr++;
                        if ( playlist == NULL ) {
                            mvpw_select_via_text( file_browser,ptr);
                            fb_start_thumbnail();
                        } else {
                            playlist = playlist_head;
                        }
                        mvpw_select_via_text( playlist_widget,equals);
                        while (playlist!=NULL) {
                            if (strcmp(playlist->name,equals)==0 ){
                                if (current==NULL) {
                                    current = strdup(playlist->filename);
                                    if (is_video(current) ) {
                                        mvpw_hide(file_browser);
                                        mvpw_hide(playlist_widget);
                                        mvpw_hide(fb_progress);
                                    } else {
                                        mvpw_show(fb_progress);
                                    }
                                    if (current!=NULL) {
                                        playlist_change(playlist);
                                    }
                                }
                                break;
                            }
                            playlist = playlist->next;
                        }
                    }

                }
            } else {
                rc = -1;
            }
            break;
        case 2:
            if (*equals == 'P' ) {
                rc = 0x3d;
            } else if (gui_state == MVPMC_STATE_NONE || gui_state == MVPMC_STATE_FILEBROWSER || gui_state == MVPMC_STATE_HTTP ) {
                switch (*equals){
                    case 'R':
                        doexit(SIGTERM);
                        break;
                    case 'S':
                        if (is_video(current) ) {
                            mvpw_show(file_browser);
                        }
                        mvpw_show(playlist_widget);
                        fb_exit();
                        // fall through so I can play again
                    case 'E':
                        if ( playlist!=NULL ) {
                            playlist_key_callback(playlist_widget,MVPW_KEY_EXIT);
                        } else {
                            fb_key_callback(file_browser,MVPW_KEY_EXIT);
                        }
                        break;
                }
            }
            break;
    }
    return rc;
}

int mvp_config_general(char *line)
{
    int rc = 0;
    int setTime = 0;
    char *ptr,*equals;
    int id;
    int bitmask=0;
    ptr = strtok(line,"&");
    while (ptr!=NULL) {
        strdecode( ptr, ptr );
        printf("%s|\n",ptr);
        equals = strchr(ptr,'=');
    	if (equals == NULL) {
    	    rc = -1;
            break;
    	} else {
            *equals = 0;
            id = atoi(ptr);
            if (id <= 0 ) {              
                *equals = '=';
            }
    	}
        if (id > 0 ) {
            equals++;
            switch (id) {
                case WEB_CONFIG_TZ:
                    snprintf(web_config->tz,30,"%s",equals);
                    break;
                case WEB_CONFIG_TIMESERVER:
                    snprintf(web_config->time_server,64,"%s",equals);
                    break;
                case WEB_CONFIG_CWD:
                    web_config->bitmask = bitmask;
                    snprintf(web_config->cwd,64,"%s",equals);
                    break;
                case WEB_CONFIG_STARTUP:
                    web_config->startup_this_feature = atoi(equals);
                    break;
                case WEB_CONFIG_VNC_SERVER:
                    snprintf(web_config->vnc_server,64,"%s",equals);
                    break;
                case WEB_CONFIG_VNC_PORT:
                    web_config->vnc_port = atoi(equals);
                    break;
                case WEB_CONFIG_MYTH_RECORD:
                    snprintf(web_config->mythtv_recdir,64,"%s",equals);
                    break;
                case WEB_CONFIG_MYTH_RING:
                    snprintf(web_config->mythtv_ringbuf,64,"%s",equals);
                    break;
                case WEB_CONFIG_CAPTURE:
                    snprintf(web_config->screen_capture_file,64,"%s",equals);
                    break;
                case WEB_CONFIG_IMAGE:
                    snprintf(web_config->imagedir,64,"%s",equals);
                    break;
                case WEB_CONFIG_RTV_INIT:
                    snprintf(web_config->rtv_init_str,64,"%s",equals);
                    break;
                case WEB_CONFIG_FONT:
                    snprintf(web_config->font,64,"%s",equals);
                    break;
                case WEB_CONFIG_LIVE_USER:
                    snprintf(web_config->live365_userid,32,"%s",equals);
                    break;
                case WEB_CONFIG_LIVE_PASS:
                    snprintf(web_config->live365_password,32,"%s",equals);
                    break;
                case WEB_CONFIG_NET_USER:
                    snprintf(web_config->share_user,16,"%s",equals);
                    break;
                case WEB_CONFIG_NET_PASS:
                    snprintf(web_config->share_password,16,"%s",equals);
                    break;
                case WEB_CONFIG_NET_IP1:
                case WEB_CONFIG_NET_IP2:
                case WEB_CONFIG_NET_IP3:
                    snprintf(web_config->share_disk[id-WEB_CONFIG_NET_IP1].ip,64,"%s",equals);
                    break;
                case WEB_CONFIG_NET_LOCAL1:
                case WEB_CONFIG_NET_LOCAL2:
                case WEB_CONFIG_NET_LOCAL3:
                    snprintf(web_config->share_disk[id-WEB_CONFIG_NET_LOCAL1].local_dir,64,"%s",equals);
                    break;
                case WEB_CONFIG_NET_REMOTE1:
                case WEB_CONFIG_NET_REMOTE2:
                case WEB_CONFIG_NET_REMOTE3:
                    snprintf(web_config->share_disk[id-WEB_CONFIG_NET_REMOTE1].remote_dir,64,"%s",equals);
                    break;
                case WEB_CONFIG_VLC_SERVER:
                    snprintf(web_config->vlc_server,64,"%s",equals);
                    break;
                case WEB_CONFIG_MVP_SERVER:
                    snprintf(web_config->mvp_server,64,"%s",equals);
                    break;
                case WEB_CONFIG_RFB_MODE:
                    web_config->rfb_mode = atoi(equals);
                    break;
                case WEB_CONFIG_FLICKER:
                    web_config->flicker = atoi(equals);
                    break;
                case WEB_CONFIG_USE_MYTH:
                case WEB_CONFIG_USE_REPLAY:
                case WEB_CONFIG_USE_MCLIENT:
                case WEB_CONFIG_USE_VNC:
                case WEB_CONFIG_USE_REBOOT:
                case WEB_CONFIG_USE_SETTING:
                case WEB_CONFIG_USE_FILE:
                case WEB_CONFIG_USE_VLC:
                case WEB_CONFIG_USE_MPLAYER:
                case WEB_CONFIG_USE_EMULATE:
                    bitmask |= (1 << (id-WEB_CONFIG_USE_MYTH));
                    break;
                case WEB_CONFIG_PLAYLIST:
                    ptr = strchr(equals,'/');
                    if (ptr==NULL) {
                        web_config->pick_playlist = 0;
                        snprintf(web_config->playlist,64,"%s/default.m3u","/usr/playlist");
                    } else {
                        snprintf(web_config->playlist,64,"%s%s.m3u","/usr/playlist",ptr);
                        *ptr = 0;
                        web_config->pick_playlist = atoi(equals);
                    }
                    break;
                default:
                    break;
            }
        } else if (strcmp(ptr,"SetTime=Set Time")==0 ){
            setenv("TZ",web_config->tz,1);
            tzset();
            pthread_mutex_lock (&web_server_mutex);
            if (fork()==0) {
                rc = execlp("/usr/sbin/rdate","rdate", "-s", web_config->time_server, (char *)0);
            }
            pthread_mutex_unlock (&web_server_mutex);
            setTime = 1;
            break;
        } else if (strcmp(ptr,"Share=Save")==0 ){
            rc = 100;
        } else if (strcmp(ptr,"Share=Run")==0 ){
            rc = 101;
        }
        ptr = strtok(NULL,"&");
    }
    if (setTime==0) {
        FILE *web_config_file;
        web_config_file = fopen (WEB_CONFIG_FILENAME,"wb");
        if (web_config_file!=NULL) {
            fwrite((char *)web_config,sizeof(web_config_t),1,web_config_file);
            fclose(web_config_file);
        }

        if (web_config->live365_userid[0]!=0) {
            char live_environ[75];
            snprintf(live_environ,75,"%s&password=%s",web_config->live365_userid,web_config->live365_password);
            setenv("LIVE365DATA",live_environ,1);
        }
    }
    return rc;
}

#define MVPSHAREFN "/etc/mvpshares"

int mvp_config_script(char *line)
{
    int rc = 0;
    int i;
    int upexec;
    int offset = 0;

    upexec = mvp_config_general(line);

    FILE *fp;
    fp = fopen( MVPSHAREFN, "w" );
    fprintf (fp,"# mvpmc configuration script\n\n");

    for (i=0;i<3;i++) {
        if (web_config->share_disk[i].ip[0]!=0) {
            if (web_config->share_disk[i].remote_dir[0]!=0) {
                if (web_config->share_disk[i].local_dir[0]!=0) {
                    if (web_config->share_user) {
                       // cifs for now
                        fprintf(fp,"if [ ! -d %s ] ; then\n",web_config->share_disk[i].local_dir);
                        printf("mkdir %s\n",web_config->share_disk[i].local_dir);
                        fprintf(fp,"mkdir %s\n",web_config->share_disk[i].local_dir);
                        fprintf(fp,"fi\n");
                        fprintf(fp,"if [ -d %s ] ; then\n",web_config->share_disk[i].local_dir);
                        if (web_config->share_disk[i].remote_dir[0]=='/') {
                            offset=1;
                        } else {
                            offset=0;
                        }
                        printf("mount.cifs //%s/%s %s -o username=%s\n",web_config->share_disk[i].ip,
                               &web_config->share_disk[i].remote_dir[offset],web_config->share_disk[i].local_dir,web_config->share_user);
                        fprintf(fp,"mount.cifs //%s/%s %s -o username=%s,password=%s,rsize=34000;\n",web_config->share_disk[i].ip,
                               &web_config->share_disk[i].remote_dir[offset],web_config->share_disk[i].local_dir,web_config->share_user,web_config->share_password);
                        fprintf(fp,"fi\n");
                    }
                }
            }
        }
    }
    fclose(fp);
    chmod(MVPSHAREFN,S_IRWXU|S_IRWXG);
    if (upexec==101) {
	    system(MVPSHAREFN);
    }
    return rc;
}

int load_playlists_from_menu(FILE *stream,char *select);
int mvp_load_data(FILE *stream,char *line)
{
    char *ptr,*embed,*remaining;
    int id=0;
    time_t tm;
    struct tm *ptm;
    int isEditRadio = 1;
    remaining = line;

    while (( embed = strstr(remaining,"<!--mvpmc")) != NULL ){
        id = -1;
        if ((ptr = strstr(embed,"ID=\""))!=NULL) {
            ptr+=4;
            if (sscanf(ptr,"%d",&id) == 1 ) {
                *embed = 0;
                embed = strchr(ptr,'>');
                if (embed!=NULL) {
                    if (id < 100) {
                        // undefined zero is a problem
                    } else if (id>=700 && id < 800) {
                        fwrite(remaining,sizeof(char),strlen(remaining),stream);
                        if (web_config->pick_playlist == (id - 700) ) {
                            fprintf(stream," SELECTED");
                        }

                    } else if (id < 10000) {
                        fwrite(remaining,sizeof(char),strlen(remaining),stream);
                        switch (id) {
                            case WEB_CONFIG_TZ:
                                fprintf(stream,"VALUE=\"%s\"",web_config->tz);
                                break;
                            case WEB_CONFIG_TIMESERVER:
                                fprintf(stream,"VALUE=\"%s\"",web_config->time_server);
                                break;
                            case WEB_CONFIG_CWD:
                                fprintf(stream,"VALUE=\"%s\"",web_config->cwd);
                                break;
                            case WEB_CONFIG_MCLIENT_IP:
                                fprintf(stream,"VALUE=\"%s\"",config->mclient_ip);
                                break;
                            case WEB_CONFIG_VNC_SERVER:
                                fprintf(stream,"VALUE=\"%s\"",web_config->vnc_server);
                                break;
                            case WEB_CONFIG_VNC_PORT:
                                fprintf(stream,"VALUE=\"%d\"",web_config->vnc_port);
                                break;
                            case WEB_CONFIG_MYTHTV_IP:
                                fprintf(stream,"VALUE=\"%s\"",config->mythtv_ip);
                                break;
                            case WEB_CONFIG_MYTH_RECORD:
                                fprintf(stream,"VALUE=\"%s\"",web_config->mythtv_recdir);
                                break;
                            case WEB_CONFIG_MYTH_RING:
                                fprintf(stream,"VALUE=\"%s\"",web_config->mythtv_ringbuf);
                                break;
                            case WEB_CONFIG_CAPTURE:
                                fprintf(stream,"VALUE=\"%s\"",web_config->screen_capture_file);	
                                break;
                            case WEB_CONFIG_IMAGE:
                                fprintf(stream,"VALUE=\"%s\"",web_config->imagedir);
                                break;
                            case WEB_CONFIG_RTV_INIT:
                                fprintf(stream,"VALUE=\"%s\"",web_config->rtv_init_str);
                                break;
                            case WEB_CONFIG_FONT:
                                fprintf(stream,"VALUE=\"%s\"",web_config->font);
                                break;
                            case WEB_CONFIG_LIVE_USER:
                                fprintf(stream,"VALUE=\"%s\"",web_config->live365_userid);
                                break;
                            case WEB_CONFIG_LIVE_PASS:
                                fprintf(stream,"VALUE=\"%s\"",web_config->live365_password);
                                break;
                            case WEB_CONFIG_NET_USER:
                                fprintf(stream,"VALUE=\"%s\"",web_config->share_user);
                                break;
                            case WEB_CONFIG_NET_PASS:
                                fprintf(stream,"VALUE=\"%s\"",web_config->share_password);
                                break;
                            case WEB_CONFIG_NET_IP1:
                            case WEB_CONFIG_NET_IP2:
                            case WEB_CONFIG_NET_IP3:
                                fprintf(stream,"VALUE=\"%s\"",web_config->share_disk[id-WEB_CONFIG_NET_IP1].ip);
                                break;
                            case WEB_CONFIG_NET_LOCAL1:
                            case WEB_CONFIG_NET_LOCAL2:
                            case WEB_CONFIG_NET_LOCAL3:
                                fprintf(stream,"VALUE=\"%s\"",web_config->share_disk[id-WEB_CONFIG_NET_LOCAL1].local_dir);
                                break;
                            case WEB_CONFIG_NET_REMOTE1:
                            case WEB_CONFIG_NET_REMOTE2:
                            case WEB_CONFIG_NET_REMOTE3:
                                fprintf(stream,"VALUE=\"%s\"",web_config->share_disk[id-WEB_CONFIG_NET_REMOTE1].remote_dir);
                                break;
                            case WEB_CONFIG_VLC_SERVER:
                                fprintf(stream,"VALUE=\"%s\"",web_config->vlc_server);
                                break;
                            case WEB_CONFIG_MVP_SERVER:
                                fprintf(stream,"VALUE=\"%s\"",web_config->mvp_server);
                                break;
                            case WEB_CONFIG_RFB_MODE:
                                fprintf(stream,"VALUE=\"%1.1d\"",web_config->rfb_mode);
                                break;
                            case WEB_CONFIG_FLICKER:
                                fprintf(stream,"VALUE=\"%1.1d\"",web_config->flicker);
                                break;
                            case WEB_CONFIG_STATUS:
                                tm = time(NULL);
                                ptm = localtime(&tm);
                                fprintf(stream,"System Time %s<BR />",asctime(ptm));
                                fprintf(stream, "%s/%s<BR />", cwd, current_hilite);
                                if (audio_playing) {
                                    fprintf (stream,"Playing Audio<BR />");
                                    fprintf(stream,"%s<BR />", playlist->name);
                                }
                                break;
                            case WEB_CONFIG_CREATE_PLAYLIST:
                                if ( mvp_load_radio_playlist(stream) > 25 ) {
                                    isEditRadio = 0;
                                }
                                break;
                            case WEB_CONFIG_EDIT_PLAYLIST:
                                if ( isEditRadio == 0 ) {
                                    fprintf(stream,"DISABLED");
                                }
                                break;
                            case WEB_CONFIG_USE_MYTH:
                            case WEB_CONFIG_USE_REPLAY:
                            case WEB_CONFIG_USE_MCLIENT:
                            case WEB_CONFIG_USE_VNC:
                            case WEB_CONFIG_USE_REBOOT:
                            case WEB_CONFIG_USE_SETTING:
                            case WEB_CONFIG_USE_FILE:
                            case WEB_CONFIG_USE_VLC:
                            case WEB_CONFIG_USE_MPLAYER:
                            case WEB_CONFIG_USE_EMULATE:
                                if ( IS_WEB_ENABLED(id) ) {
                                    fprintf(stream," CHECKED");
                                }
                                break;
                            case WEB_CONFIG_START_MAIN:
                            case WEB_CONFIG_START_MYTH:
                            case WEB_CONFIG_START_FILE:
                            case WEB_CONFIG_START_VNC:
                            case WEB_CONFIG_START_SETTING:
                            case WEB_CONFIG_START_REPLAY:
                            case WEB_CONFIG_START_MCLIENT:
                            case WEB_CONFIG_START_EMULATE:
                                if (web_config->startup_this_feature == (id - WEB_CONFIG_START_MAIN) ) {
                                    fprintf(stream," SELECTED");
                                }
                                break;
                            default:
                                fprintf(stream,"VALUE=\"Missing id %d\"",id);
                                break;
                        }
                    }
                    embed++;
                    remaining = embed;
//                    printf("%s\n",remaining);
                    id = 1;
                } else {
                    *embed = '<';
                    id = -2;
                    break;
                }
            } else {
                id = -3;
                break;
            }
        }
    }
    fprintf(stream,"%s",remaining);
    return id;
}
int mvp_load_radio_playlist(FILE * stream)
{
    int i=0;
    FILE *fp;
    char file_data[131];
    char title[131];
    int want_extinf;
    char *ptr;

    fp = fopen( web_config->playlist, "r" );
    if ( fp!=NULL ) {
        file_data[130]=0;
        if ( fgets(file_data,130,fp) != NULL) {
            if (strncmp(file_data,"#EXTM3U",7) ) {
                rewind(fp);
            }
            want_extinf=1;
            for (i=0;i < 100; ) {
                if (fgets(file_data,130,fp) == NULL ) {
                    break;
                }
                ptr = strpbrk(file_data,"\n\r");
                if (ptr!=NULL) {
            	    *ptr=0;
                }
                if (strlen(file_data) > 128 ) {
                    break;
		    }
                if (strncmp(file_data,"#EXTINF:-1,",11)==0) {
    	              if ( want_extinf == 0 ) {
                        // corrupt file
                        break;
                    }
                    snprintf(title,60,"%s",&file_data[11]);
                    want_extinf = 0;
                } else {
                    // not extend info
                    if ( want_extinf == 1 ) {
                        // use possible url as description
                        snprintf(title,60,"%s",file_data);
                    }
                    output_playlist_html(stream,i,title,file_data);
                    want_extinf = 1;
                    i++;
                 }
             }
        }
        fclose(fp);
    } else {
        char *genre, *dot;
        genre = strrchr(web_config->playlist,'/');
        if (genre != NULL) {
            genre++;
            dot = strchr(genre,'.');
            if (dot !=NULL ) {
                *dot = 0;
                snprintf(title,50,"Initialize %s playlist",genre);
                if (strcmp(genre,"random") ) {
                    snprintf(file_data,80,"http://www.shoutcast.com/sbin/newxml.phtml?genre=%s",genre);
                } else {
                    snprintf(file_data,50,"http://www.shoutcast.com/sbin/newxml.phtml?random=20");
                }
                output_playlist_html(stream,i,title,file_data);
                *dot = '.';
                i++;
            }
        }
    }

// allow edit of 25
    if (i >= 10 && i < 25 ) {
         output_playlist_html(stream,i,"","");
         i++;
    }

// but show at least 10

    for (;i < 10; i++) {
         output_playlist_html(stream,i,"","");
    }

    return i;
}

void output_playlist_html(FILE *stream,int lineno,char *title,char *url)
{
    fprintf(stream,"<TR><TD><PRE>\n\n");
    fflush(stream);
    fprintf(stream,"  Name: <INPUT TYPE=\"TEXT\" NAME=\"desc%d\" VALUE=\"%s\" SIZE=61 MAXLENGTH=60 />",lineno,title);
    if (url[0]!=0) {
        fprintf(stream,"  <INPUT TYPE=\"SUBMIT\" NAME=\"%s\" VALUE=\"Play\">",title);
    }
    fprintf(stream,"\n\n   URL: <INPUT TYPE=\"TEXT\" NAME=\"url%d\" VALUE=\"",lineno);
    if (strchr(url,'"') == NULL) {
        fprintf(stream,"%s",url);
    } else {
        char *p;
        p = url;
        while (*p!=0) {
            if (*p == '"') {
                fputs("&#34",stream);
            } else {
                fputc(*p,stream);
            }
            p++;
       }
    }
    fprintf(stream,"\" SIZE=80 MAXLENGTH=127 />\n\n</TD></TR></PRE>");
    return;
}

void load_web_config(char *font)
{
    // set up from browser
    FILE *web_config_file;
	struct stat sb;
    
    web_config->control = 0;
    web_config_file = fopen (WEB_CONFIG_FILENAME,"rb");
    if (web_config_file !=NULL ) {
        printf("web file open\n");
        memset(web_config, 0, sizeof(*web_config));
        fread((char *)web_config,sizeof(web_config_t),1,web_config_file);
        fclose(web_config_file);
        char live_environ[75];
        snprintf(live_environ,75,"%s&password=%s",web_config->live365_userid,web_config->live365_password);
        setenv("LIVE365DATA",live_environ,1);
        setenv("TZ",web_config->tz,1);

    } else {
        memset(web_config, 0, sizeof(web_config_t));
        snprintf(web_config->tz,30,"%s",getenv("TZ"));
        snprintf(web_config->cwd,64,"%s",cwd);
        web_config->startup_this_feature = startup_this_feature;
        strcpy(web_config->rtv_init_str,"ip=discover");
        strcpy(web_config->imagedir,"/usr/share/mvpmc");
        strcpy(web_config->font,"/etc/helvR10.fnt");
        
        web_config->bitmask = 0;
       
        if (config->bitmask & CONFIG_MYTHTV_IP) {
            web_config->bitmask |=1;
        }
        if (replaytv_server != NULL ) {
            web_config->bitmask |=2;
        }
        if (config->bitmask & CONFIG_MCLIENT_IP) {
            web_config->bitmask |=4;
        }
        if ( vnc_server[0] != 0 ) {
            web_config->bitmask |=8;
	        snprintf(web_config->vnc_server,64,"%s",vnc_server);
        }
        if (reboot_disable==0) {
            web_config->bitmask |=16;
        }
        if (settings_disable==0) {
            web_config->bitmask |=32;
        }
        if (filebrowser_disable==0) {
            web_config->bitmask |=64;
        }
        if ( vlc_server != NULL ) {
            web_config->bitmask |=128;
	        snprintf(web_config->vlc_server,64,"%s",vlc_server);
        }
        if (mplayer_disable==0) {
            web_config->bitmask |=256;
        }
        /* mythtv debug not enabled */
        if ( mvp_server != NULL ) {
            web_config->bitmask |=1024;
	        snprintf(web_config->mvp_server,64,"%s",mvp_server);
        }
        web_config->rfb_mode = rfb_mode;
        web_config->flicker = flicker;

        snprintf(web_config->playlist,64,"/usr/playlist/default.m3u");
        strcpy(web_config->share_user,"guest");
        snprintf(web_config->share_disk[0].local_dir,64,"%s","/usr/video");
        snprintf(web_config->share_disk[1].local_dir,64,"%s","/usr/music");
        snprintf(web_config->share_disk[2].local_dir,64,"%s","/usr/playlist");

        if (getenv("LIVE365DATA")!=NULL) {
            sscanf(getenv("LIVE365DATA"),"%32[^&]%*[^=]%*1s%32s",web_config->live365_userid,web_config->live365_password);
        }
    }
    
    if (startup_this_feature == MM_EXIT) {
        startup_this_feature = web_config->startup_this_feature;
    }
    if (strcmp(cwd,"/")==0 && web_config->cwd[0]=='/' ) {
        if (stat(web_config->cwd, &sb) == 0) {
            if (S_ISDIR(sb.st_mode)) {
                strcpy(cwd,web_config->cwd);
            }
        }
    }
    if (font == NULL) {
        if (stat(web_config->font, &sb) == 0) {
            font = strdup(web_config->font);
        }
    }
    if (strcmp(imagedir,"/usr/share/mvpmc")==0 && strcmp(web_config->imagedir,"/usr/share/mvpmc")) {
        if (web_config->imagedir[0]=='/') {
              imagedir = strdup(web_config->imagedir);
        }
    }
    if (screen_capture_file == NULL) {
        if (web_config->screen_capture_file[0]=='/') {
            screen_capture_file = strdup(web_config->screen_capture_file);
        }
    }
    if (mythtv_server != NULL) {
        if (IS_WEB_ENABLED(WEB_CONFIG_USE_MYTH)) {
            if (mythtv_ringbuf == NULL) {
                if (web_config->mythtv_ringbuf[0]=='/') {
                    mythtv_ringbuf = strdup(web_config->mythtv_ringbuf);
                }
            }
            if (mythtv_recdir == NULL) {
                if (web_config->mythtv_recdir[0]=='/') {
                    mythtv_recdir = strdup(web_config->mythtv_recdir);
                }
            }

        } else {
            free(mythtv_server);
            mythtv_server = NULL;
      }
    }
    if ( replaytv_server == NULL) {
        if (IS_WEB_ENABLED(WEB_CONFIG_USE_REPLAY) && web_config->rtv_init_str[0]) {
            replaytv_server = "RTV";
            rtv_init_str = strdup(web_config->rtv_init_str);
        }
    } else if (NOT_WEB_ENABLED(WEB_CONFIG_USE_REPLAY) ){
        free(rtv_init_str);
        rtv_init_str = replaytv_server = NULL;
    } else if (web_config->rtv_init_str[0] && strcmp(web_config->rtv_init_str,rtv_init_str) && strcmp(rtv_init_str,"ip=discover")==0){
        free(rtv_init_str);
        rtv_init_str = strdup(web_config->rtv_init_str);
    }
    if (NOT_WEB_ENABLED(WEB_CONFIG_USE_MCLIENT) && mclient_server != NULL) {
        free(mclient_server);
        mclient_server = NULL;
    }
    if (IS_WEB_ENABLED(WEB_CONFIG_USE_VNC) && vnc_server[0]==0 ) {
        strcpy(vnc_server,web_config->vnc_server);
        vnc_port = web_config->vnc_port;
    }
    if (NOT_WEB_ENABLED(WEB_CONFIG_USE_SETTING) ){
        settings_disable = 1;
    } else {
        settings_disable = 0;
    }
    if (NOT_WEB_ENABLED(WEB_CONFIG_USE_REBOOT) ){
        reboot_disable = 1;
    } else {
        reboot_disable = 0;
    }
    if (NOT_WEB_ENABLED(WEB_CONFIG_USE_FILE) ){
        filebrowser_disable = 1;
    } else {
        filebrowser_disable = 0;
    }
    if (IS_WEB_ENABLED(WEB_CONFIG_USE_VLC) && vlc_server == NULL ) {
        vlc_server = strdup(web_config->vlc_server);
    }
    if (NOT_WEB_ENABLED(WEB_CONFIG_USE_MPLAYER) ){
        mplayer_disable = 1;
    } else {
        mplayer_disable = 0;
    }
    if (IS_WEB_ENABLED(WEB_CONFIG_USE_EMULATE) && mvp_server == NULL ) {
        mvp_server = strdup(web_config->mvp_server);
    }

}

void reset_web_config(void)
{
    if (NOT_WEB_ENABLED(WEB_CONFIG_USE_MCLIENT) && mclient_server) {
        free(mclient_server);
        mclient_server = NULL;
    }
    if (NOT_WEB_ENABLED(WEB_CONFIG_USE_MYTH) && mythtv_server ) {
        free(mythtv_server);
        mythtv_server = NULL;
    }
}

int wol_getmac(char *ip)
{
    char command[128];
    FILE * in;
    char *p,*p1;
    snprintf(command,128,"/usr/bin/arping -c 1 %s",ip);
    in = popen(command, "r");
    if (in==NULL)printf("%d\n",errno);
    while (fgets(command,128,in)!=NULL) {
        if (strncmp(command,"Unicast reply",13)==0) {
            p = strchr(command,'[');
            if (p!=NULL) {
                p++;
                p1 = strchr(p,']');
                if (p1!=NULL) {
                    *p1 = 0;
                    snprintf(web_config->wol_mac,18,"%s",p);
                    printf("Emulation server mac address is %s\n",p);
                }
            }
        }
    }
    fclose(in);
    return 0;
}

int wol_wake(void)
{
    int rc = -1;
    if (web_config->wol_mac[0]!=0) {
        if (fork()==0) {
            rc = execlp("/usr/bin/ether-wake","ether-wake",web_config->wol_mac,(char *)0);
        }
    }
    return rc;
}

