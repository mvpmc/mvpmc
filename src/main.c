/*
 *  Copyright (C) 2004, Jon Gettler
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

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>

#include "mvpmc.h"
#include "replaytv.h"

#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

char *mythtv_server = NULL;
char *replaytv_server = NULL;

int fontid, big_font;
extern demux_handle_t *handle;

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
pthread_attr_t thread_attr;

static pid_t child;

av_demux_mode_t demux_mode;

int (*DEMUX_PUT)(demux_handle_t*, char*, int);
int (*DEMUX_WRITE_VIDEO)(demux_handle_t*, int);

#define DATA_SIZE (1024*1024)
static char *data = NULL;
static int data_len = 0;

static int
buffer_put(demux_handle_t *handle, char *buf, int len)
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

	printf("\t-a aspect \taspect ratio (4:3 or 16:9)\n");
	printf("\t-b path   \tpath to NFS mounted mythtv ringbuf directory\n");
	printf("\t-f font   \tfont file\n");
	printf("\t-h        \tprint this help\n");
	printf("\t-i dir    \tmvpmc image directory\n");
	printf("\t-m mode   \toutput mode (ntsc or pal)\n");
	printf("\t-M        \tMythTV protocol debugging output\n");
	printf("\t-o output \toutput device for video (composite or svideo) and / or for audio (stereo or passthru)\n");
	printf("\t-s server \tmythtv server IP address\n");
	printf("\t-S seconds\tscreensaver timeout in seconds (0 - disable)\n");
	printf("\t-r path   \tpath to NFS mounted mythtv recordings\n");
	printf("\t-R server \treplaytv server IP address\n");
	printf("\t-t file   \tXML theme file\n");
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
	kill(child, sig);
	usleep(5000);

	kill(child, SIGKILL);
	exit(sig);
}

/*
 * doexit() - exit so any atexit processing can happen
 */
static void
doexit(int sig)
{
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
			printf("child pid %d\n", getpid());
			close(fd);
			signal(SIGINT, doexit);
			signal(SIGTERM, doexit);
			return;
		}

		while (1) {
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			to.tv_sec = 1;
			to.tv_usec = 0;

			if (select(fd+1, &fds, NULL, NULL, &to) > 0) {
				unsigned short key;

				read(fd, &key, sizeof(key));
				read(fd, &key, sizeof(key));

				if ((key & 0xff) == 0x3d) {
					power = !power;

					if (power == 0) {
						printf("Power OFF\n");
						kill(child, SIGKILL);
						av_set_led(0);
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

		switch (status) {
		case 0:
			printf("child exited cleanly\n");
			break;
		case 1:
			printf("child failed\n");
			exit(1);
			break;
		default:
			printf("child failed, with status %d, restarting...\n",
			       status);
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
	int mode = -1, output = -1, aspect = -1;
	int width, height;
	uint32_t accel = MM_ACCEL_DJBFFT;
	char *theme_file = NULL;

	if (argc > 32) {
		fprintf(stderr, "too many arguments\n");
		exit(1);
	}

	for (i=0; i<argc; i++)
		saved_argv[i] = strdup(argv[i]);

	tzset();

	while ((c=getopt(argc, argv, "a:b:f:hi:m:Mo:r:R:s:S:t:")) != -1) {
		switch (c) {
		case 'a':
			if (strcmp(optarg, "4:3") == 0) {
				aspect = 0;
			} else if (strcmp(optarg, "16:9") == 0) {
				aspect = 1;
			} else {
				fprintf(stderr, "unknown aspect ratio '%s'\n",
					optarg);
				print_help(argv[0]);
				exit(1);
			}
			break;
		case 'b':
			mythtv_ringbuf = strdup(optarg);
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
			break;
		case 'M':
			mythtv_debug = 1;
			break;
		case 'o':
			if (strcasecmp(optarg, "svideo") == 0) {
				output = AV_OUTPUT_SVIDEO;
			} else if (strcasecmp(optarg, "composite") == 0) {
				output = AV_OUTPUT_COMPOSITE;
			} else if (strcasecmp(optarg, "stereo") == 0) {
				audio_output_mode = AUD_OUTPUT_STEREO;
			} else if (strcasecmp(optarg, "passthru") == 0) {
				audio_output_mode = AUD_OUTPUT_PASSTHRU;
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
			break;
		case 't':
			theme_file = strdup(optarg);
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}

	if (theme_file)
		theme_parse(theme_file);

#ifndef MVPMC_HOST
	spawn_child();
#endif

	signal(SIGPIPE, SIG_IGN);

	if (font)
		fontid = mvpw_load_font(font);
#ifdef MVPMC_HOST
	big_font = fontid;
#else
	if ((big_font=mvpw_load_font("/etc/helvB18.pcf")) <= 0)
		big_font = fontid;
#endif

	printf("fonts: %d %d\n", fontid, big_font);

	if ((demux_mode=av_init()) == AV_DEMUX_ERROR) {
		fprintf(stderr, "failed to initialize av hardware!\n");
		exit(1);
	}
   
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

	a52_state = a52_init (accel);

	if (aspect != -1)
		av_set_aspect(aspect);

	if (output != -1)
		av_set_output(output);

	if ((handle=demux_init(1024*1024*4)) == NULL) {
		fprintf(stderr, "failed to initialize demuxer\n");
		exit(1);
	}
	demux_set_display_size(handle, width, height);

	video_init();

	pthread_attr_init(&thread_attr);
	pthread_attr_setstacksize(&thread_attr, 1024*128);

	/*
	 * If the demuxer is not being used, all mpeg data will go
	 * through the video device.  This will allow an external
	 * player to be used (ie, mplayer).
	 */
	if (demux_mode == AV_DEMUX_ON) {
		DEMUX_PUT = demux_put;
		DEMUX_WRITE_VIDEO = demux_write_video;
		pthread_create(&audio_write_thread, &thread_attr,
			       audio_write_start, NULL);
	} else {
		DEMUX_PUT = buffer_put;
		DEMUX_WRITE_VIDEO = buffer_write;
		if ((data=malloc(DATA_SIZE)) == NULL) {
			perror("malloc()");
			exit(1);
		}
	}
	pthread_create(&video_read_thread, &thread_attr,
		       video_read_start, NULL);
	pthread_create(&video_write_thread, &thread_attr,
		       video_write_start, NULL);

	if (gui_init(mythtv_server, replaytv_server) < 0) {
		fprintf(stderr, "failed to initialize gui!\n");
		exit(1);
	}

#ifdef __UCLIBC__
	/*
	 * XXX: moving to uClibc seems to have exposed a problem...
	 *
	 * It appears that uClibc must make more use of malloc than glibc
	 * (ie, uClibc probably frees memory when not needed).  This is
	 * exposing a problem with memory exhaustion under linux where
	 * mvpmc needs memory, and it will hang until linux decides to
	 * flush some of its NFS pages.
	 *
	 * BTW, the heart of this problem is the linux buffer cache.  Since
	 * all file systems use the buffer cache, and linux assumes that
	 * the buffer cache is flushable, all hell breaks loose when you
	 * have a ramdisk.  Essentially, Linux is waiting for something
	 * that will never happen (the ramdisk to get flushed to backing
	 * store).
	 *
	 * So, force the stack and the heap to grow, and leave a hole
	 * in the heap for future calls to malloc().
	 */
	{
#define HOLE_SIZE (1024*1024*1)
#define PAGE_SIZE 4096
		int i, n = 0;
		char *ptr[HOLE_SIZE/PAGE_SIZE];
		char *last = NULL, *guard;
		char stack[1024*512];

		for (i=0; i<HOLE_SIZE/PAGE_SIZE; i++) {
			if ((ptr[i]=malloc(PAGE_SIZE)) != NULL) {
				*(ptr[i]) = 0;
				n++;
				last = ptr[i];
			}
		}

		for (i=1; i<1024; i++) {
			guard = malloc(1024*i);
			if ((unsigned int)guard < (unsigned int)last) {
				if (guard)
					free(guard);
			} else {
				break;
			}
		}
		if (guard)
			*guard = 0;

		memset(stack, 0, sizeof(stack));

		for (i=0; i<HOLE_SIZE/PAGE_SIZE; i++) {
			if (ptr[i] != NULL)
				free(ptr[i]);
		}

		if (guard)
			printf("Created hole in heap of size %d bytes\n",
			       n*PAGE_SIZE);
		else
			printf("Failed to create hole in heap\n");
	}
#endif /* __UCLIBC__ */

	atexit(mythtv_atexit);

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
