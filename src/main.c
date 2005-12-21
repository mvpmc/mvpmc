/*
 *  Copyright (C) 2004, 2005, Jon Gettler
 *  http://mvpmc.sourceforge.net/
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

#ident "$Id$"

#include <stdio.h>
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

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>
#include <ts_demux.h>

#include "mvpmc.h"
#include "replaytv.h"
#include "config.h"

#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

#include "display.h"
#include "mclient.h"

static struct option opts[] = {
	{ "aspect", required_argument, 0, 'a' },
	{ "config", required_argument, 0, 'F' },
	{ "iee", required_argument, 0, 'd' },
	{ "mclient", required_argument, 0, 'c' },
	{ "mode", required_argument, 0, 'm' },
	{ "mythtv", required_argument, 0, 's' },
	{ "mythtv-debug", no_argument, 0, 'M' },
	{ "no-reboot", no_argument, 0, 0 },
	{ "no-settings", no_argument, 0, 0 },
	{ "theme", required_argument, 0, 't' },
	{ 0, 0, 0, 0 }
};

int settings_disable = 0;
int reboot_disable = 0;

#define VNC_SERVERPORT (5900)    /* Offset to VNC server for regular connections */

char *mythtv_server = NULL;
char *replaytv_server = NULL;
char *mclient_server = NULL;

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

	printf("\t-a aspect \taspect ratio (4:3, 4:3cco, 16:9, 16:9auto)\n");
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
	printf("\t-S seconds\tscreensaver timeout in seconds (0 - disable)\n");
	printf("\t-r path   \tpath to NFS mounted mythtv recordings\n");
	printf("\t-R server \treplaytv server IP address\n");
	printf("\t-D display\tVNC server IP address[:display]\n");
	printf("\t-t file   \tXML theme file\n");
	printf("\t-c server \tslimdevices musicClient server IP address\n");
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

			key = 0;
			if (select(fd+1, &fds, NULL, NULL, &to) > 0) {

				read(fd, &key, sizeof(key));
				read(fd, &key, sizeof(key));

				if ((key & 0xff) == 0x3d) {
					power = !power;

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
	int aspect = AV_ASPECT_4x3;
	int width, height;
	uint32_t accel = MM_ACCEL_DJBFFT;
	char *theme_file = NULL;
	struct stat sb;
	int opt_index;
#ifndef MVPMC_HOST
	unsigned long start = (unsigned long)sbrk(0);
#endif

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
#else
	if ((config=(config_t*)malloc(sizeof(config_t))) == NULL) {
		perror("malloc()");
		exit(1);
	}
#endif
	memset(config, 0, sizeof(*config));
#ifndef MVPMC_HOST
	config->firsttime = 1;
#endif
	config->magic = CONFIG_MAGIC;

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
	config->av_aspect = AV_ASPECT_4x3;
	config->av_mode = AV_MODE_PAL;

	/*
	 * Parse the command line options.  These settings must override
	 * the settings from all other sources.
	 */
	while ((c=getopt_long(argc, argv,
			      "a:b:C:c:d:D:f:F:hi:m:Mo:r:R:s:S:t:",
			      opts, &opt_index)) != -1) {
		switch (c) {
		case 0:
			if (strcmp(opts[opt_index].name, "no-settings") == 0) {
				settings_disable = 1;
			}
			if (strcmp(opts[opt_index].name, "no-reboot") == 0) {
				reboot_disable = 1;
			}
			break;
		case 'a':
			if (strcmp(optarg, "4:3cco") == 0) {
				aspect = AV_ASPECT_4x3_CCO;
			} else if (strcmp(optarg, "4:3") == 0) {
				aspect = AV_ASPECT_4x3;
			} else if (strcmp(optarg, "16:9") == 0) {
				aspect = AV_ASPECT_16x9;
			} else if (strcmp(optarg, "16:9auto") == 0) {
				aspect = AV_ASPECT_16x9_AUTO;
			} else {
				fprintf(stderr, "unknown aspect ratio '%s'\n",
					optarg);
				print_help(argv[0]);
				exit(1);
			}
			config->bitmask |= CONFIG_ASPECT;
			config->av_aspect = aspect;
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
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
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
	osd_set_surface_size(width, height);

	if (mw_init(mythtv_server, replaytv_server) < 0) {
		fprintf(stderr, "failed to initialize microwindows!\n");
		exit(1);
	}

	fd_audio = av_audio_fd();
	fd_video = av_video_fd();
	av_attach_fb();
	av_play();

	av_set_volume(volume);

	a52_state = a52_init (accel);

	if (!(config->bitmask & CONFIG_ASPECT) && (aspect != -1))
		av_set_aspect(aspect);

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

	pthread_create(&video_read_thread, &thread_attr_small,
		       video_read_start, NULL);
	pthread_create(&video_write_thread, &thread_attr_small,
		       video_write_start, NULL);
	pthread_create(&audio_thread, &thread_attr_small,
		       audio_start, NULL);

	if (gui_init(mythtv_server, replaytv_server) < 0) {
		fprintf(stderr, "failed to initialize gui!\n");
		exit(1);
	}

#ifndef MVPMC_HOST
	if (display_init() < 0) {
		fprintf(stderr, "failed to initialize display driver!\n");
		exit(1);
	}
#endif

	if (music_client() < 0) {
		fprintf(stderr, "failed to start up music client!\n");
		exit(1);
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
	}

	hw_state = new;
}

void
switch_gui_state(mvpmc_state_t new)
{
	if (new == gui_state)
		return;

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
		break;
	case MVPMC_STATE_MYTHTV:
		mythtv_atexit();
		break;
	case MVPMC_STATE_REPLAYTV:
		break;
	case MVPMC_STATE_MCLIENT:
		break;
	}

	switch (gui_state) {
	case MVPMC_STATE_NONE:
		break;
	case MVPMC_STATE_FILEBROWSER:
		break;
	case MVPMC_STATE_MYTHTV:
		break;
	case MVPMC_STATE_REPLAYTV:
		replaytv_atexit();
		break;
	case MVPMC_STATE_MCLIENT:
		break;
	}

	printf("%s(): exiting...\n", __FUNCTION__);
}
