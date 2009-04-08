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
 * \li \link mvp_refmem.h librefmem \endlink
 * \li \link tiwlan.h libtiwlan \endlink
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
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <libgen.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>
#include <mvp_string.h>
#include <ts_demux.h>

#include "mvpmc.h"
#include "mythtv.h"
#include "replaytv.h"
#include "config.h"
#include "web_config.h"

#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

#include "display.h"
#include "mclient.h"

char *version = NULL;
char build_info[256];

static struct option opts[] = {
	{ "aspect", required_argument, 0, 'a' },
	{ "config", required_argument, 0, 'F' },
	{ "iee", required_argument, 0, 'd' },
	{ "mclient", required_argument, 0, 'c' },
	{ "mode", required_argument, 0, 'm' },
	{ "mythtv", required_argument, 0, 's' },
	{ "mythtv-debug", no_argument, 0, 'M' },
	{ "no-wss", no_argument, 0, 0 },
	{ "jit-sync", no_argument, 0, 0 },
	{ "no-seek-sync", no_argument, 0, 0 },
	{ "no-filebrowser", no_argument, 0, 0 },
	{ "no-reboot", no_argument, 0, 0 },
	{ "no-settings", no_argument, 0, 0 },
	{ "theme", required_argument, 0, 't' },
	{ "startup", required_argument, 0, 0},
	{ "web-port", required_argument, 0, 0},
	{ "version", no_argument, 0, 0},
	{ "vlc", required_argument, 0, 0},
	{ "vlc-vopts", required_argument, 0, 0},
	{ "vlc-aopts", required_argument, 0, 0},
	{ "vlc-vb", required_argument, 0, 0},
	{ "vlc-ab", required_argument, 0, 0},
	{ "use-mplayer", no_argument, 0, 0 },
	{ "emulate", required_argument, 0, 0},
	{ "rfb-mode", required_argument, 0, 0},
	{ "rtwin", required_argument, 0, 0},
	{ "fs-rtwin", required_argument, 0, 0},
	{ "em-rtwin", required_argument, 0, 0},
	{ "em-wolwt", required_argument, 0, 0},
	{ "em-conwt", required_argument, 0, 0},
	{ "em-safety", required_argument, 0, 0},
	{ "em-flac", no_argument, 0, 0},
	{ "flicker", required_argument, 0, 0},
	{ "mythtv-db", required_argument, 0, 'y'},
	{ "mythtv-username", required_argument, 0, 'u'},
	{ "mythtv-password", required_argument, 0, 'p'},
	{ "mythtv-database", required_argument, 0, 'T'},
	{ "weather-location", required_argument, 0, 0},
	{ "friendly-date", no_argument, 0, 0 },
	{ "duration-minutes", no_argument, 0, 0 },
	{ "classic", no_argument, 0, 0 },
	{ 0, 0, 0, 0 }
};


int settings_disable = 0;
int reboot_disable = 0;
int jit_mode = 1;
int filebrowser_disable = 0;
int mplayer_disable = 1;
int em_connect_wait = 0;
int em_wol_wait = 0;
int em_flac = 0;
char em_wol_mac[20];
int em_safety=-1;
int em_rtwin = -1;
int rfb_mode = 3;
int flicker = -1;
int wireless = 0;
int mythtv_seek_amount=0;
int mythtv_commskip=1;  // manual commskip in mythtv - default value - config overrides
int mythtv_auto_commskip=0; // auto commskip in mythtv
int mvpmc_classic = 0;


int mount_djmount(char *);
int unmount_djmount(void);
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
char *weather_location = NULL;
char *weather_cmdline = NULL;

char vnc_server[256];
int vnc_port = 0;
int rtwin = -1;
int fs_rtwin = 4096;

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

static pid_t child;

av_demux_mode_t demux_mode;

int (*DEMUX_PUT)(demux_handle_t*, void*, int);
int (*DEMUX_WRITE_VIDEO)(demux_handle_t*, int);
int (*DEMUX_WRITE_AUDIO)(demux_handle_t*, int);
int (*DEMUX_JIT_WRITE_AUDIO)(demux_handle_t*, int, unsigned int,int,int*,int*);

#define DATA_SIZE (1024*1024)
static char *data = NULL;
static int data_len = 0;

mvpmc_state_t hw_state = MVPMC_STATE_NONE;
mvpmc_state_t gui_state = MVPMC_STATE_NONE;

config_t *config;
static int shmid;

#ifndef MVPMC_HOST
static int web_shmid;
#endif


int set_route(int rtwin);

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

static int
jit_buffer_write(demux_handle_t *handle, int fd, unsigned int pts, int mode, int *flags, int *duration)
{
    return buffer_write(handle,fd);
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
	printf("\t-H        \tdisplay 12 hour clock\n");
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
	printf("\t--jit-sync\tenable JIT a/v Sync\n");
	printf("\t--no-seek-sync\tdisable post-seek a/v Sync attempts\n");
	printf("\t--no-filebrowser\n");
	printf("\t--no-reboot\n");
	printf("\t--no-settings\n");
	printf("\t--startup \t(replaytv, mythtv, mclient)\n");
	printf("\t--web-port port\tconfiguration port\n");
	printf("\t--vlc server \tvlc IP address\n");
	printf("\t--vlc-vopts opt\t[dvd|svcd|vcd|none] VLC video stream quality\n");
	printf("\t--vlc-aopts opt\t[mp3|flac] VLC audio stream encoding\n");
	printf("\t--vlc-vb rate\tVLC video stream bitrate (Mb)\n");
	printf("\t--vlc-ab rate\tVLC audio stream bitrate(Kb)\n");
	printf("\t--use-mplayer \tenable mplayer\n");
	printf("\n");
	printf("\t--emulate server \tIP address or ?\n");
	printf("\t--em-wolwt seconds \tWait # seconds for Emulation WOL\n");
	printf("\t--em-conwt seconds \tWait # seconds for Emulation connect\n");
	printf("\t--em-safety level \tEmulation protection level\n");
	printf("\t--em-rtwin size \tbuffer size of Emulation rt_window\n");
	printf("\t--flac \tFLAC enabled in Emulation Mode\n");
	printf("\t--rfb-mode mode\t(0, 1, 2)\n");
	printf("\t--flicker value\tflicker value 0-3\n");
	printf("\t--rtwin size \tbuffer size of global rt_window\n");
	printf("\t--fs-rtwin size \tbuffer size of filesystem rt_window\n");
	printf("\t--weather-location location \tZIP code or location code for weather\n");
	printf("\n");
	printf("\t--friendly-date \tSat Dec 15 instead of 12/15\n");
	printf("\t--duration-minutes \tDisplay duration in minutes intead of a date/time range\n");
	printf("\t--classic \tDisplay original file browser display\n");
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
void
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
					if(read(fd, &key, sizeof(key)) != sizeof(key)
					   || read(fd, &key, sizeof(key)) != sizeof(key))
						continue;
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
						if (strstr(cwd,"/uPnP")!=NULL ){ 
							unmount_djmount();
						}
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

#ifndef MVPMC_HOST
static void
check_wireless(void)
{
	struct ifreq ifr;
	int fd;

	strncpy(ifr.ifr_name, "eth1", IFNAMSIZ);

	if ((fd=socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("wireless device not found\n");
		return;
	}

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		printf("wireless device not found\n");
		return;
	}

	printf("wireless device found\n");

	close(fd);

	if ((ifr.ifr_flags & (IFF_RUNNING|IFF_UP)) == (IFF_RUNNING|IFF_UP)) {
		wireless = 1;
		printf("using eth1 (wireless)\n");
	} else {
		wireless = 0;
		printf("using eth0 (wired)\n");
	}
}
#endif /* MVPMC_HOST */

void start_me_up(void);

/*
 * main()
 */
int
mvpmc_main(int argc, char **argv)
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

	/*
	 * Ensure the build info is easily found in any corefile.
	 */
	snprintf(build_info, sizeof(build_info),
		 "BUILD_INFO: '%s' '%s' '%s' '%s' '%s'\n",
		 version_number,
		 compile_time,
		 build_user,
		 git_revision,
		 git_diffs);

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
	config->mythtv_commskip = mythtv_commskip;
	config->mythtv_auto_commskip = mythtv_auto_commskip;
	//config->mythtv_use_friendly_date = mythtv_use_friendly_date;
	//config->mythtv_use_duration_minutes = mythtv_use_duration_minutes;
	vnc_server[0] = 0;
	em_wol_mac[0] = 0;
	/*
	 * Parse the command line options.  These settings must override
	 * the settings from all other sources.
	 */
	while ((c=getopt_long(argc, argv,
			      "a:b:C:c:d:D:f:F:hHi:m:Mo:r:R:s:S:y:t:u:p:T:",
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
			if (strcmp(opts[opt_index].name, "jit-sync") == 0) {
			    	jit_mode = jit_mode | 2;
			}
			if (strcmp(opts[opt_index].name, "no-seek-sync") == 0) {
			    	jit_mode = jit_mode & (~1);
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
			if (strcmp(opts[opt_index].name, "rtwin") == 0) {
				rtwin = atoi(optarg_tmp);
				set_route(rtwin);
			}
			if (strcmp(opts[opt_index].name, "fs-rtwin") == 0) {
				fs_rtwin = atoi(optarg_tmp);
			}
			if (strcmp(opts[opt_index].name, "em-rtwin") == 0) {
				em_rtwin = atoi(optarg_tmp);
			}
			if (strcmp(opts[opt_index].name, "em-flac") == 0) {
				em_flac = 1;
			}
			if (strcmp(opts[opt_index].name, "version") == 0) {
				printf("MediaMVP Media Center\n");
				printf("http://www.mvpmc.org/\n");
				if (version != NULL) {
					printf("Version: %s\n", version);
				}
				printf("Built by: %s\n", build_user);
				printf("Built at: %s\n", compile_time);
				if (git_diffs[0] != '\0') {
					printf("git diffs: %s\n", git_diffs);
				}
				if (git_revision[0] != '\0') {
					printf("git revision: %s\n", git_revision);
				}
				exit(0);
			}
			if (strcmp(opts[opt_index].name, "vlc") == 0) {
				vlc_server = strdup(optarg);
				sizeof_strncpy(config->vlc_ip, optarg);
			}
			if (strcmp(opts[opt_index].name, "vlc-vopts") == 0) {
				sizeof_strncpy(config->vlc_vopts, optarg);
			}
			if (strcmp(opts[opt_index].name, "vlc-aopts") == 0) {
				sizeof_strncpy(config->vlc_aopts, optarg);
			}
			if (strcmp(opts[opt_index].name, "vlc-vb") == 0) {
				config->vlc_vb = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "vlc-ab") == 0) {
				config->vlc_ab = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "emulate") == 0) {
				mvp_server = strdup(optarg);
			}
			if (strcmp(opts[opt_index].name, "em-conwt") == 0) {
				em_connect_wait = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "em-wolwt") == 0) {
				em_wol_wait = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "em-safety") == 0) {
				em_safety = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "rfb") == 0) {
				rfb_mode = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "flicker") == 0) {
				flicker = atoi(optarg);
			}
			if (strcmp(opts[opt_index].name, "weather-location") == 0) {
				weather_location = strdup(optarg);
				printf("cmdline location '%s' %p\n", weather_location, weather_location);
				weather_cmdline = strdup(optarg);
				sizeof_strncpy(config->weather_location, optarg);
				config->bitmask |= CONFIG_WEATHER_LOCATION;
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
			if (strcmp(opts[opt_index].name, "friendly-date") == 0) {
				mythtv_use_friendly_date = 1;
				config->mythtv_use_friendly_date = 1;
			}
			if (strcmp(opts[opt_index].name, "duration-minutes") == 0) {
				mythtv_use_duration_minutes = 1;
				config->mythtv_use_duration_minutes = 1;
			}
			if (strcmp(opts[opt_index].name, "classic") == 0) {
				mvpmc_classic = 1;
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
		case 'H':
			mythtv_use_12hour_clock = 1;
			config->use_12_hour_clock = 1;
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
			if (replaytv_server) {
				web_port = 8080;
			} else {
				web_port = 80;
			}
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
	check_wireless();
	spawn_child();
#endif

	// init here for globals 

	strcpy(em_wol_mac,web_config->wol_mac);
	memset(web_config, 0, sizeof(web_config_t));
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

	if (strstr(cwd,"/uPnP")!=NULL ){
		unmount_djmount();
	}
	if ((demux_mode=av_init()) == AV_DEMUX_ERROR) {
		fprintf(stderr, "failed to initialize av hardware!\n");
		unmount_djmount();
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

	if (mw_init() < 0) {
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
		DEMUX_JIT_WRITE_AUDIO = demux_jit_write_audio;
		pthread_create(&audio_write_thread, &thread_attr,
			       audio_write_start, NULL);
	} else {
		DEMUX_PUT = buffer_put;
		DEMUX_WRITE_VIDEO = buffer_write;
		DEMUX_WRITE_AUDIO = buffer_write;
		DEMUX_JIT_WRITE_AUDIO = jit_buffer_write;
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

	if ((config->bitmask & CONFIG_MYTHTV_IP) != 0
		&& strcmp(mysqlptr->host,"localhost") == 0) {
		sizeof_strncpy(mysqlptr->host,config->mythtv_ip);
	}

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

	if (web_port) {
		web_server = 1;
		pthread_create(&web_server_thread, &thread_attr_small,www_mvpmc_start, NULL);
	}

	atexit(atexit_handler);

	if (gui_init(mythtv_server, replaytv_server) < 0) {
		fprintf(stderr, "failed to initialize gui!\n");
		exit(1);
	}
	if (music_client() < 0) {
		fprintf(stderr, "failed to start up music client!\n");
		exit(1);
	}

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

/*
 * Call appropriate shutdown function to prepare to switch between
 * applications.
 */
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
	case MVPMC_STATE_FILEBROWSER_SHUTDOWN:
	case MVPMC_STATE_HTTP:
	case MVPMC_STATE_HTTP_SHUTDOWN:
		fb_exit();
		if (strstr(cwd,"/uPnP")!=NULL ){
			unmount_djmount();
		}
		break;
	case MVPMC_STATE_MYTHTV:
	case MVPMC_STATE_MYTHTV_SHUTDOWN:
		mythtv_exit();
		break;
	case MVPMC_STATE_REPLAYTV:
	case MVPMC_STATE_REPLAYTV_SHUTDOWN:
		replaytv_exit();
		break;
	case MVPMC_STATE_MCLIENT:
	case MVPMC_STATE_MCLIENT_SHUTDOWN:
		mclient_exit();
		break;
	case MVPMC_STATE_EMULATE:
	case MVPMC_STATE_EMULATE_SHUTDOWN:
		break;
	case MVPMC_STATE_WEATHER:
		break;
	}

	hw_state = new;
}

void
switch_gui_state(mvpmc_state_t new)
{
	if (new == gui_state)
		return;
 
	if (gui_state==MVPMC_STATE_FILEBROWSER) {
		if (strstr(cwd,"/uPnP")!=NULL ){
			unmount_djmount();
		}
		set_route(web_config->rtwin);
	} else if (new==MVPMC_STATE_FILEBROWSER) {
		set_route(web_config->fs_rtwin);
	}

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
	case MVPMC_STATE_FILEBROWSER_SHUTDOWN:
	case MVPMC_STATE_HTTP:
	case MVPMC_STATE_HTTP_SHUTDOWN:
		fb_exit();
		break;
	case MVPMC_STATE_MYTHTV:
	case MVPMC_STATE_MYTHTV_SHUTDOWN:
		mythtv_atexit();
		break;
	case MVPMC_STATE_REPLAYTV:
	case MVPMC_STATE_REPLAYTV_SHUTDOWN:
		break;
	case MVPMC_STATE_MCLIENT:
	case MVPMC_STATE_MCLIENT_SHUTDOWN:
		break;
	case MVPMC_STATE_EMULATE:
	case MVPMC_STATE_EMULATE_SHUTDOWN:
		break;
	case MVPMC_STATE_WEATHER:
		break;
	}

	switch (gui_state) {
	case MVPMC_STATE_NONE:
		break;
	case MVPMC_STATE_HTTP:
	case MVPMC_STATE_HTTP_SHUTDOWN:
	case MVPMC_STATE_FILEBROWSER:
	case MVPMC_STATE_FILEBROWSER_SHUTDOWN:
		if (strstr(cwd,"/uPnP")!=NULL ){
			unmount_djmount();
		}
		break;
	case MVPMC_STATE_MYTHTV:
	case MVPMC_STATE_MYTHTV_SHUTDOWN:
		break;
	case MVPMC_STATE_REPLAYTV:
	case MVPMC_STATE_REPLAYTV_SHUTDOWN:
		replaytv_atexit();
		break;
	case MVPMC_STATE_MCLIENT:
	case MVPMC_STATE_MCLIENT_SHUTDOWN:
		break;
	case MVPMC_STATE_EMULATE:
	case MVPMC_STATE_EMULATE_SHUTDOWN:
		break;
	case MVPMC_STATE_WEATHER:
		break;
	}

	printf("%s(): exiting...\n", __FUNCTION__);
}

int
main(int argc, char **argv)
{
	extern int ticonfig_main(int argc, char **argv);
	extern int vpdread_main(int argc, char **argv);
	extern int splash_main(int argc, char **argv);
	char *prog;

	prog = basename(argv[0]);

	if (strcmp(prog, "mvpmc") == 0) {
		return mvpmc_main(argc, argv);
#ifndef MVPMC_HOST
	} else if (strcmp(prog, "ticonfig") == 0) {
		return ticonfig_main(argc, argv);
	} else if (strcmp(prog, "vpdread") == 0) {
		return vpdread_main(argc, argv);
	} else if (strcmp(prog, "splash") == 0) {
		return splash_main(argc, argv);
#endif /* !MVPMC_HOST */
	}

	fprintf(stderr, "Unknown app: %s\n", prog);

	return -1;
}
