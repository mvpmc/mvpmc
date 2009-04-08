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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <mvp_demux.h>
#include <mvp_widget.h>
#include <mvp_av.h>
#include <ts_demux.h>

#include "mvpmc.h"
#include "mythtv.h"
#include "replaytv.h"
#include "config.h"
#include "http_stream.h"

#if 0
#define PRINTF(x...) printf(x)
#define TRC(fmt, args...) printf(fmt, ## args) 
#else
#define PRINTF(x...)
#define TRC(fmt, args...) 
#endif

extern int new_live_tv;
extern int jit_mode;

/* #define STREAM_TEST 1 */
#ifdef STREAM_TEST 
unsigned int stream_test_cnt     = 0;
int          stream_test_started = 0;
struct timeval      start_tv;
struct timeval      done_tv;
#endif 

#define SEEK_FUDGE	2
#define ASPECT_FUDGE	0

static char inbuf_static[VIDEO_BUFF_SIZE * 3 / 2];
static char tsbuf_static[VIDEO_BUFF_SIZE]; 

int showing_guide = 0;

pthread_cond_t video_cond = PTHREAD_COND_INITIALIZER;
static sem_t   write_threads_idle_sem;

int fd = -1;

static av_stc_t seek_stc;
static int seek_seconds;
volatile int seeking = 0;
volatile int jumping = 0;
volatile long long jump_target;
static volatile int seek_Bps;
static volatile int seek_attempts;
static volatile int gop_seek_attempts;
static volatile int pts_seek_attempts;
static volatile struct timeval seek_timeval;
static volatile off_t seek_start_pos;
static volatile int seek_start_seconds;
static volatile int audio_type = 0;
static volatile int audio_selected = 0;
static volatile int audio_checks = 0;
static volatile int pcm_decoded = 0;
volatile int paused = 0;
static int zoomed = 0;
int display_on = 0, display_on_alt = 0;

static volatile int thruput = 0;
static volatile int thruput_count = 0;
static struct timeval thruput_start;

int fd_audio, fd_video;
demux_handle_t *handle;
ts_demux_handle_t *tshandle;

static stream_type_t audio_output = STREAM_MPEG;

static unsigned char ac3buf[1024*32];
static volatile int ac3len = 0, ac3more = 0;
void sync_ac3_audio(void);

static volatile int video_reopen = 0;
volatile int video_playing = 0;

static long long file_seek(long long, int);
static long long file_size(void);

int seek_osd_timeout = 0;
int pause_osd = 0;

video_callback_t file_functions = {
	.open      = file_open,
	.read      = file_read,
	.read_dynb = NULL,
	.seek      = file_seek,
	.size      = file_size,
	.notify    = NULL,
	.key       = NULL,
	.halt_stream = NULL,
};

int mvp_file_read(char *,int);
long long mvp_seek(long long,int);

video_callback_t mvp_functions = {
	.open      = file_open,
	.read      = mvp_file_read,
	.read_dynb = NULL,
	.seek      = mvp_seek,
	.size      = NULL,
	.notify    = NULL,
	.key       = NULL,
	.halt_stream = NULL,
};

volatile video_callback_t *video_functions = NULL;

void video_play(mvp_widget_t *widget);

static void video_change_aspect(int new_aspect, int new_afd);

static void
sighandler(int sig)
{
	/*
	 * The signal handler is here simply to allow the threads writing to
	 * the hardware devices to be interrupted, and break out of the write()
	 * system call.
	 */
}

void
video_thumbnail(av_thumbnail_mode_t thumb_mode, vid_thumb_location_t loc)
{
	static int enable = 1;

	if (thumb_mode != AV_THUMBNAIL_OFF) {
		int x,y;
		if((loc & 1) == 0)
			x = VIEWPORT_LEFT;
		else
			x = VIEWPORT_RIGHT - av_thumbnail_width(thumb_mode);

		if((loc & 2) == 0)
			y = VIEWPORT_TOP;
		else
			y = VIEWPORT_BOTTOM - av_thumbnail_height(thumb_mode);
		av_move(x, y, thumb_mode);
		screensaver_enable();
		enable = 1;
	} else {
		av_wss_redraw();
		if (gui_state != MVPMC_STATE_EMULATE) {
			av_move(0, 0, 0);
		}
		if (enable)
			screensaver_disable();
		enable = 0;
	}
}

static void
video_subtitle_display(mvp_widget_t *widget)
{
	spu_item_t *spu;

	spu = demux_spu_get_next(handle);

	if (spu) {
		char *image;

		if ((image=demux_spu_decompress(handle, spu)) != NULL) {
			mvpw_bitmap_attr_t bitmap;

			if (spu_widget) {
				mvpw_hide(spu_widget);
				mvpw_expose(root);
				mvpw_destroy(spu_widget);
			}

			spu_widget = mvpw_create_bitmap(NULL,
							spu->x, spu->y,
							spu->w, spu->h,
							0, 0, 0);

			bitmap.image = image;

			/*
			 * XXX: we really should wait until the proper
			 *      moment to display the subtitle
			 */
			if (spu_widget) {
				mvpw_set_bitmap(spu_widget, &bitmap);
				mvpw_lower(spu_widget);
				mvpw_show(spu_widget);
				mvpw_expose(spu_widget);
			}

			free(image);
		} else {
			printf("fb: got subtitle, decompress failed\n");
		}

		free(spu);
	}
}

void
video_subtitle_check(mvp_widget_t *widget)
{
	extern mvp_widget_t *main_menu;

	if (demux_spu_get_id(handle) >= 0)
		mvpw_set_timer(root, video_subtitle_display, 250);
	else
		mvpw_set_timer(root, video_subtitle_check, 1000);

	if (gui_state == MVPMC_STATE_EMULATE ) {
		return;
	}
	if (! (mvpw_visible(file_browser) ||
	       mvpw_visible(playlist_widget) ||
	       mvpw_visible(mythtv_browser) ||
	       mvpw_visible(main_menu) ||
	       mvpw_visible(settings) ||
	       mvpw_visible(ct_text_box) ||
	       mvpw_visible(mythtv_menu))) {
		if(showing_guide == 0) {
			video_thumbnail(AV_THUMBNAIL_OFF,0);
		}
	} 
}

int video_init(void)
{
	sem_init(&write_threads_idle_sem, 0, 2);
	return(0);
}

void
video_set_root(void)
{
	av_state_t state;

	if (root_bright > 0)
		root_color = mvpw_color_alpha(MVPW_WHITE, root_bright*4);
	else if (root_bright < 0)
		root_color = mvpw_color_alpha(MVPW_BLACK, root_bright*-4);
	else
		root_color = 0;

	mvpw_set_bg(root, root_color);

	if (av_get_state(&state) == 0) {
		if (state.pause) {
			mvpw_show(pause_widget);
		}
		if (state.mute) {
			mvpw_show(mute_widget);
		}
		if (state.ffwd) {
			mvpw_show(ffwd_widget);
		}
	}
}

void
video_play(mvp_widget_t *widget)
{
	demux_attr_t *attr;
	video_info_t *vi;
	TRC("%s\n", __FUNCTION__);
	mvpw_set_idle(NULL);

	if (demux_spu_get_id(handle) < 0)
		mvpw_set_timer(root, video_subtitle_check, 1000);
	else
		mvpw_set_timer(root, video_subtitle_display, 250);

	video_set_root();

	av_set_tv_aspect(config->av_tv_aspect);
	attr = demux_get_attr(handle);
	vi = &attr->video.stats.info.video;
	video_change_aspect(vi->aspect,vi->afd);
	/*Force video demux code to detect and trigger an aspect event for any
	 *new AFD or aspect ratio: */
	vi->aspect = -1;
	vi->afd = -1;
	paused = 0;
	video_reopen = 1;
	video_playing = 1;
	pthread_cond_broadcast(&video_cond);
}

void
video_clear(void)
{
	int cnt;
	TRC("%s\n", __FUNCTION__);
	if (fd >= 0)
		close(fd);
	fd = -1;
	video_playing = 0;
	audio_type = 0;
	pthread_kill(video_write_thread, SIGURG);
	pthread_kill(audio_write_thread, SIGURG);

	sem_getvalue(&write_threads_idle_sem, &cnt);
	while ( cnt != 2 ) {
		sleep(0);
		sem_getvalue(&write_threads_idle_sem, &cnt);
		pthread_cond_broadcast(&video_cond);
	}
	av_stop();
	av_video_blank();
	av_reset();
	av_reset_stc();
	pthread_cond_broadcast(&video_cond);

	mvpw_set_bg(root, MVPW_BLACK);
	av_wss_update_aspect(WSS_ASPECT_FULL_4x3);
}

void
video_stop_play(void)
{
	video_playing = 0;   
}

void
video_progress(mvp_widget_t *widget)
{
	long long offset = 0, size;
	int off;
	char buf[32];

	if ((size=video_functions->size()) < 0) {
		disable_osd();
		mvpw_hide(osd_widget);
		mvpw_hide(mute_widget);
		mvpw_hide(pause_widget);
		mvpw_hide(ffwd_widget);
		mvpw_hide(zoom_widget);
		display_on = 0;
		zoomed = 0;
		return;
	}
	if (video_functions->seek)
		offset = video_functions->seek(0, SEEK_CUR);
	off = (int)((double)(offset/1000) /
		    (double)(size/1000) * 100.0);
	snprintf(buf, sizeof(buf), "%d%%", off);
	mvpw_set_text_str(offset_widget, buf);

	mvpw_set_graph_current(offset_bar, off);

	mvpw_expose(offset_widget);
}

void
video_timecode(mvp_widget_t *widget)
{
	demux_attr_t *attr;
	char buf[32];
	av_stc_t stc;

	attr = demux_get_attr(handle);
	av_current_stc(&stc);
	snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d",
		 stc.hour, stc.minute, stc.second);
	if (running_mythtv && !mythtv_livetv) {
		int seconds = 0, minutes = 0, hours = 0;

		seconds = mythtv_program_runtime();

		if (seconds > 0) {
			hours = seconds / (60 * 60);
			minutes = (seconds / 60) % 60;
			seconds = seconds % 60;

			snprintf(buf, sizeof(buf),
				 "%.2d:%.2d:%.2d / %.2d:%.2d:%.2d",
				 stc.hour, stc.minute, stc.second,
				 hours, minutes, seconds);
		}
	}
	if (using_vlc) {
		vlc_timecode(buf);
	}
	mvpw_set_text_str(time_widget, buf);
}

int
video_get_byterate()
{
    if(handle != NULL)
    {
	demux_attr_t *attr = demux_get_attr(handle);
	if(attr != NULL && attr->Bps > 1024)
	    return attr->Bps;
    }
    return 512 * 1024; /* Default to 4Mbps (0.5MBps) */
}

void
video_bitrate(mvp_widget_t *widget)
{
	demux_attr_t *attr;
	av_stc_t stc;
	char buf[32];
	int Mb;

	attr = demux_get_attr(handle);
	av_current_stc(&stc);
	Mb = (attr->Bps * 8) / (1024 * 1024);
	snprintf(buf, sizeof(buf), "%d.%.2d Mbps",
		 Mb, (attr->Bps * 8) / (1024 * 1024 / 100) - (Mb * 100));
	mvpw_set_text_str(bps_widget, buf);
}

void
video_clock(mvp_widget_t *widget)
{
	time_t t;
	struct tm *tm;
	char buf[64];
	char format[12];

	if (mythtv_use_12hour_clock)
		strcpy(format, "%I:%M:%S %p");
	else
		strcpy(format, "%H:%M:%S");

	t = time(NULL);
	tm = localtime(&t);

	strftime(buf, 64, format, tm);

	mvpw_set_text_str(widget, buf);
}

void
video_demux(mvp_widget_t *widget)
{
	demux_attr_t *attr;

	attr = demux_get_attr(handle);

	mvpw_set_graph_current(demux_video, attr->video.stats.cur_bytes);
	mvpw_expose(demux_video);
	mvpw_set_graph_current(demux_audio, attr->audio.stats.cur_bytes);
	mvpw_expose(demux_audio);
}

void
osd_callback(mvp_widget_t *widget)
{
	struct stat64 sb;
	long long offset = 0;
	int off, Mb;
	char buf[32];
	av_stc_t stc;
	demux_attr_t *attr;

	fstat64(fd, &sb);
	if (video_functions->seek)
		offset = video_functions->seek(0, SEEK_CUR);
	off = (int)((double)(offset/1000) /
		    (double)(sb.st_size/1000) * 100.0);
	snprintf(buf, sizeof(buf), "%d", off);
	mvpw_set_text_str(offset_widget, buf);

	mvpw_set_graph_current(offset_bar, off);

	attr = demux_get_attr(handle);
	av_current_stc(&stc);
	Mb = (attr->Bps * 8) / (1024 * 1024);
	snprintf(buf, sizeof(buf), "Mbps: %d.%.2d   Time: %.2d:%.2d:%.2d",
		 Mb, (attr->Bps * 8) / (1024 * 1024 / 100) - (Mb * 100),
		 stc.hour, stc.minute, stc.second);
	mvpw_set_text_str(bps_widget, buf);
}

void
seek_to(long long seek_offset)
{
	long long offset;

	if (video_functions->seek == NULL) {
		fprintf(stderr, "cannot seek on this video!\n");
		return;
	}

	pthread_kill(video_write_thread, SIGURG);
	pthread_kill(audio_write_thread, SIGURG);

	if (mvpw_visible(ffwd_widget)) {
		mvpw_hide(ffwd_widget);
		av_ffwd();
	}

	offset = video_functions->seek(seek_offset, SEEK_SET);

	jump_target = seek_offset;
	jumping = 1;

	PRINTF("-> %lld\n", offset);

	pthread_cond_broadcast(&video_cond);
}

static void
seek_by(int seconds)
{
	demux_attr_t *attr = demux_get_attr(handle);
	int delta;
	int stc_time, gop_time, pts_time;
	long long offset, size;

	if (video_functions->seek == NULL) {
		fprintf(stderr, "cannot seek on this video!\n");
		return;
	}

	pthread_kill(video_write_thread, SIGURG);
	pthread_kill(audio_write_thread, SIGURG);

	if (mvpw_visible(ffwd_widget)) {
		mvpw_hide(ffwd_widget);
		av_ffwd();
	}

	gop_seek_attempts = 0;
	pts_seek_attempts = 0;

	av_current_stc(&seek_stc);

	seek_Bps = ((1024*1024) * 4) / 8;  /* default to 4 megabits per second */
	gop_time = 0;
	if ( attr->gop_valid ) {
		gop_time = (attr->gop.hour*60 + attr->gop.minute)*60 +
			attr->gop.second;
		if (attr->Bps)
			seek_Bps = attr->Bps;
	}
	pts_time = attr->gop.pts/PTS_HZ;
	stc_time = (seek_stc.hour*60 + seek_stc.minute)*60 + seek_stc.second;

	/*
	 * If the STC and GOP timestamps are close, use the STC timestamp as
	 * the starting point, because it is the timestamp for the frame the
	 * hardware is currently playing. If GOP time not set use STC anyway.
	 */
	if (abs(gop_time - stc_time) < 10) {
		printf("STC SEEK from: %d, (%d)\n", stc_time, gop_time);
		seek_start_seconds = stc_time;
		gop_seek_attempts = 4;
	} else if (gop_time) {
		printf("GOP SEEK from: %d, (%d)\n", gop_time, pts_time);
		seek_start_seconds = gop_time;
		gop_seek_attempts = 4;
	} else {
		printf("PTS SEEK from: %d, (%d)\n", pts_time, gop_time);
		seek_start_seconds = pts_time;
		pts_seek_attempts = 4;
	}
	seek_start_pos = video_functions->seek(0, SEEK_CUR);
	size = video_functions->size();

	seek_seconds = seek_start_seconds + seconds;

	/* The mvpmc hardware can only handle 12Mbps and some off-air DVB/ATSC
	 * SD broadcasts appear to some way result in huge (200+Mbps) values
	 * to appear in the seeks and cause 30 seconds seeks to go for a lot
	 * longer */
	if ( seek_Bps > (12/8)*1024*1024) {
		printf("Calculated Seek Bps was %d kbps, which is too high " \
		       "- set to 4Mbps\n",8*seek_Bps/1024);
		seek_Bps = (4/8) * 1024 * 1024;
	}

	delta = seek_Bps * seconds;

	/*
	 * Abort the seek if near the end of the file
	 */
	if ((size < 0) || (seek_start_pos + delta > size)) {
		fprintf(stderr, "near end of file, seek aborted\n");
		return;
	}

	PRINTF("%d Bps, currently %lld + %d\n",
	       seek_Bps, seek_start_pos, delta);

	gettimeofday((struct timeval*)&seek_timeval, NULL);
	seek_attempts = 16;
	seeking = 1;
	
	offset = video_functions->seek(delta, SEEK_CUR);

	PRINTF("-> %lld\n", offset);

	pthread_cond_broadcast(&video_cond);
}

void
disable_osd(void)
{
	set_osd_callback(OSD_BITRATE, NULL);
	set_osd_callback(OSD_CLOCK, NULL);
	set_osd_callback(OSD_DEMUX, NULL);
	set_osd_callback(OSD_PROGRESS, NULL);
	set_osd_callback(OSD_PROGRAM, NULL);
	set_osd_callback(OSD_TIMECODE, NULL);
}

static void
seek_disable_osd(mvp_widget_t *widget)
{
	if (display_on_alt != 1)
		return;

	display_on_alt = 0;

	if (!display_on) {
		disable_osd();
	}
	mvpw_expose(root);
}

void
set_bookmark_status_fail(mvp_widget_t *widget)
{
	char buf[32];
	char data[16]="Bookmark Status";
	//display_on=!display_on;
	display_on=0;
	snprintf(buf, sizeof(buf),"Bookmark Failed");
	mvpw_set_text_str(mythtv_osd_program, data);
	mvpw_set_text_str(mythtv_osd_description, buf);
	mvpw_expose(mythtv_osd_description);
}

void
set_bookmark_status(mvp_widget_t *widget)
{
	char buf[32];
	char data[16]="Bookmark Status";
	//display_on=!display_on;
	display_on=0;
	snprintf(buf, sizeof(buf),"New Bookmark Set");
	mvpw_set_text_str(mythtv_osd_description, buf);
	mvpw_set_text_str(mythtv_osd_program, data);
	mvpw_expose(mythtv_osd_description);
}

void
goto_bookmark_status(mvp_widget_t *widget)
{
	char buf[32];
	char data[16]="Bookmark Status";
	display_on=0;
	snprintf(buf, sizeof(buf),"Jumping to bookmark");
	mvpw_set_text_str(mythtv_osd_description, buf);
	mvpw_set_text_str(mythtv_osd_program, data);
	mvpw_expose(mythtv_osd_description);
}

void
set_commbreak_status(mvp_widget_t *widget)
{
	char buf[55];
	char data[16]="Commercial Skip";
	display_on=0;
	snprintf(buf, sizeof(buf),"MythTV Commercial Skip\n");
	mvpw_set_text_str(mythtv_osd_description, buf);
	mvpw_set_text_str(mythtv_osd_program, data);
	mvpw_expose(mythtv_osd_description);
}

void
set_seek_status(mvp_widget_t *widget)
{
	char buf[42];
	char data[16]="Skip";
	display_on=0;
	if (mythtv_commskip) 
		snprintf(buf, sizeof(buf),"MythTV Seek\nNot in Commercial Break");
	else 
		snprintf(buf, sizeof(buf),"MythTV Seek\nCommercial Skip Disabled");
	mvpw_set_text_str(mythtv_osd_description, buf);
	mvpw_set_text_str(mythtv_osd_program, data);
	mvpw_expose(mythtv_osd_description);
}

void
display_bookmark_status_osd(int function)
{
	display_on_alt = 1;
	set_osd_callback(OSD_PROGRESS, video_progress);
	set_osd_callback(OSD_TIMECODE, video_timecode);

	switch (function) {
		case 0:
			//seek to bookmark
			set_osd_callback(OSD_PROGRAM, goto_bookmark_status);
			break;
		case 1:
			// set bookmark
			set_osd_callback(OSD_PROGRAM, set_bookmark_status);
			break;
		case 2:
			// doing a commskip seek
			set_osd_callback(OSD_PROGRAM, set_commbreak_status);
			break;
		case 3:
			// doing a normal seek
			set_osd_callback(OSD_PROGRAM, set_seek_status);
			break;
		case 4:
			// set bookmark failed
			set_osd_callback(OSD_PROGRAM, set_bookmark_status_fail);
			break;
	}
	mvpw_set_timer(mythtv_osd_description, seek_disable_osd , 5000);
	mvpw_set_timer(mythtv_osd_program, seek_disable_osd , 5000);
}

void
enable_osd(void)
{
	set_osd_callback(OSD_PROGRESS, video_progress);
	set_osd_callback(OSD_TIMECODE, video_timecode);
	set_osd_callback(OSD_BITRATE, video_bitrate);
	set_osd_callback(OSD_CLOCK, video_clock);
	set_osd_callback(OSD_DEMUX, video_demux);
	switch (hw_state) {
	case MVPMC_STATE_MYTHTV:
	case MVPMC_STATE_MYTHTV_SHUTDOWN:
		set_osd_callback(OSD_PROGRAM, mythtv_program);
		break;
	case MVPMC_STATE_REPLAYTV:
	case MVPMC_STATE_REPLAYTV_SHUTDOWN:
		set_osd_callback(OSD_PROGRAM, replaytv_osd_proginfo_update);
		break;
	case MVPMC_STATE_HTTP:
	case MVPMC_STATE_HTTP_SHUTDOWN:
	case MVPMC_STATE_FILEBROWSER:
	case MVPMC_STATE_FILEBROWSER_SHUTDOWN:
		set_osd_callback(OSD_PROGRAM, fb_program);
		break;
	default:
		set_osd_callback(OSD_PROGRAM, NULL);
		break;
	}
}

void
back_to_guide_menu()
{
	
		disable_osd();
		if ( !running_replaytv ) {
			video_thumbnail(AV_THUMBNAIL_EIGTH,VID_THUMB_BOTTOM_RIGHT);
		}
		if (spu_widget) {
			mvpw_hide(spu_widget);
			mvpw_expose(root);
			mvpw_destroy(spu_widget);
			spu_widget = NULL;
			mvpw_set_timer(root, NULL, 0);
		}
		mvpw_hide(osd_widget);
		mvpw_hide(mute_widget);
		mvpw_hide(pause_widget);
		mvpw_hide(ffwd_widget);
		mvpw_hide(zoom_widget);
		display_on = 0;
		zoomed = 0;
		switch (gui_state) {
		case MVPMC_STATE_NONE:
		case MVPMC_STATE_EMULATE:
		case MVPMC_STATE_EMULATE_SHUTDOWN:
		case MVPMC_STATE_WEATHER:
			/*
			 * XXX: redisplay the main menu?
			 */
			break;
		case MVPMC_STATE_MYTHTV:
		case MVPMC_STATE_MYTHTV_SHUTDOWN:
			printf("%s(): %d\n", __FUNCTION__, __LINE__);
			if (mythtv_livetv == 1) {
				if (mythtv_state == MYTHTV_STATE_LIVETV) {
					if (mvpw_visible(mythtv_browser) || new_live_tv) {
						mythtv_livetv_stop();
						mythtv_livetv = 0;
						running_mythtv = 0;
						if(new_live_tv) {
							switch_gui_state(MVPMC_STATE_MYTHTV);
							mvpw_show(mythtv_logo);
							mvpw_show(mythtv_menu);
							mvpw_focus(mythtv_menu);
						}
					} else {
						mvpw_show(mythtv_channel);
						mvpw_show(mythtv_date);
						mvpw_show(mythtv_description);
						mvpw_show(mythtv_logo);
						mvpw_show(mythtv_browser);
						mvpw_focus(mythtv_browser);
						video_thumbnail(AV_THUMBNAIL_EIGTH,VID_THUMB_BOTTOM_RIGHT);
					}
				} else if (mythtv_state == MYTHTV_STATE_MAIN) {
					mvpw_show(mythtv_logo);
					mvpw_show(mythtv_menu);
				} else {
					mythtv_show_widgets();
				}
			} else if (mythtv_main_menu) {
				mvpw_show(mythtv_logo);
				mvpw_show(mythtv_menu);
				mvpw_focus(mythtv_menu);
			} else if (running_mythtv) {
				printf("%s(): %d\n", __FUNCTION__, __LINE__);
				mythtv_show_widgets();
				mvpw_focus(mythtv_browser);
			}
			break;
		case MVPMC_STATE_REPLAYTV:
		case MVPMC_STATE_REPLAYTV_SHUTDOWN:
			video_playing = 0;
			replaytv_back_from_video();
			break;
		case MVPMC_STATE_FILEBROWSER:
		case MVPMC_STATE_FILEBROWSER_SHUTDOWN:
		case MVPMC_STATE_HTTP:
		case MVPMC_STATE_HTTP_SHUTDOWN:
			if (playlist) {
				mvpw_show(fb_progress);
				mvpw_show(playlist_widget);
				mvpw_focus(playlist_widget);
			} else {
				mvpw_show(fb_progress);
				mvpw_show(file_browser);
				mvpw_focus(file_browser);
			}
			break;
		case MVPMC_STATE_MCLIENT:
		case MVPMC_STATE_MCLIENT_SHUTDOWN:
			/*
			 * No code is necessary here because:
			 * - The key is already trapped / processed in gui.c/mclient_key_callback.
			 * - And the mclient show / focus is in gui.c/main_select_callback.
			 */
			break;
		}
		mvpw_expose(root);
}

		
void
video_callback(mvp_widget_t *widget, char key)
{
	int jump;
	long long offset, size;
	pts_sync_data_t async, vsync;
	av_state_t state;

	/*
	printf("**SSDEBUG: In video_callback and got key %d \n",key);
	*/

	if (!video_playing)
		return;

	if(showing_guide) {
		if(mvp_tvguide_callback(widget, key) == 1)
			return;
		}

	if ( video_functions->key != NULL ) {
		if ( video_functions->key(key) == 1 ) {
			return;
		}
	}

	switch (key) {
	case MVPW_KEY_GO:
	case MVPW_KEY_GUIDE:
		if(showing_guide == 0 && showing_guide == 0) {
			printf("In %s showing guide %d \n",__FUNCTION__, key);
			showing_guide = 1;
			mvp_tvguide_video_topright(1);
			mvp_tvguide_show(mythtv_livetv_program_list, mythtv_livetv_description,
											 mythtv_livetv_clock);
			break;
		}
	/* if the guide button is pressed while guide is active fall through to go back to remove guide and return to TV */
	case MVPW_KEY_TV:
		if(showing_guide == 1) {
			printf("In %s hiding guide %d \n", __FUNCTION__, key);
			showing_guide = 0;
			mvp_tvguide_video_topright(0);
			mvp_tvguide_hide(mythtv_livetv_program_list, mythtv_livetv_description,
											 mythtv_livetv_clock);
		}
		break;
	case MVPW_KEY_STOP:
	case MVPW_KEY_EXIT:
		back_to_guide_menu();
		new_live_tv = 0;
		break;
		
	case MVPW_KEY_PAUSE:
		if (av_pause()) {
			mvpw_show(pause_widget);
			mvpw_hide(ffwd_widget);
			paused = 1;
			if (pause_osd && !display_on && (display_on_alt < 2)) {
				display_on_alt = 2;
				enable_osd();
			}
			screensaver_enable();
		} else {
			if (pause_osd && !display_on &&
			    (display_on_alt == 2)) {
				display_on_alt = 0;
				disable_osd();
				mvpw_expose(root);
			}
			av_get_state(&state);
			if (state.mute)
				mvpw_show(mute_widget);
			else
				mvpw_hide(mute_widget);
			mvpw_hide(pause_widget);
			paused = 0;
			screensaver_disable();
		}
		break;
	case MVPW_KEY_PLAY:
		if ( paused ) {
			/*
			 * play key can be used to un-pause
			 */
			av_pause();
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
		break;
	case MVPW_KEY_REPLAY:
		seek_by(-30);
		timed_osd(seek_osd_timeout*1000);
		break;
	case MVPW_KEY_REWIND:
		seek_by(-10);
		timed_osd(seek_osd_timeout*1000);
		break;
	case MVPW_KEY_SKIP:
		if (mythtv_seek_amount == 0 ) {
			seek_by(30);
		}
		else if (mythtv_seek_amount == 1 ) {
			seek_by(60);
		}
		else {
			seek_by(30);
		}
		timed_osd(seek_osd_timeout*1000);
		break;
	case MVPW_KEY_FFWD:
		if (av_ffwd() == 0) {
			demux_flush(handle);
			demux_seek(handle);
			av_get_state(&state);
			av_stop();
			av_reset();
			if (state.mute) {
				av_set_mute(1);
			}
			av_play();
			mvpw_hide(ffwd_widget);
		} else {
			av_get_state(&state);
			mvpw_show(ffwd_widget);
			mvpw_hide(pause_widget);
			screensaver_disable();
		}
		timed_osd(seek_osd_timeout*1000);
		break;
	case MVPW_KEY_LEFT:
		if (video_functions->seek) {
			size = video_functions->size();
			jump_target = -1;
			jumping = 1;
			pthread_kill(video_write_thread, SIGURG);
			pthread_kill(audio_write_thread, SIGURG);
			offset = video_functions->seek(0, SEEK_CUR);
			jump_target = ((-size / 100.0) + offset);
			pthread_cond_broadcast(&video_cond);
		}
		break;
	case MVPW_KEY_RIGHT:
		if (video_functions->seek) {
			size = video_functions->size();
			jump_target = -1;
			jumping = 1;
			pthread_kill(video_write_thread, SIGURG);
			pthread_kill(audio_write_thread, SIGURG);
			offset = video_functions->seek(0, SEEK_CUR);
			jump_target = ((size / 100.0) + offset);
			pthread_cond_broadcast(&video_cond);
			timed_osd(seek_osd_timeout*1000);
		}
		break;
	case MVPW_KEY_ZERO ... MVPW_KEY_NINE:
		if(new_live_tv) {
			printf("In %s showing guide %d \n",__FUNCTION__, key);
			showing_guide = 1;
			mvp_tvguide_video_topright(1);
			mvp_tvguide_show(mythtv_livetv_program_list, mythtv_livetv_description,
											 mythtv_livetv_clock);
			mvp_tvguide_callback(widget, key);
		}
		else if (mythtv_livetv) {
			back_to_guide_menu();
			mythtv_key_callback(mythtv_browser,  key);
		}
		else {
			size = video_functions->size();
			jump_target = -1;
			jumping = 1;
			pthread_kill(video_write_thread, SIGURG);
			pthread_kill(audio_write_thread, SIGURG);
			jump = key;
			jump_target = size * (jump / 10.0);
			pthread_cond_broadcast(&video_cond);
			timed_osd(seek_osd_timeout*1000);
		}
		break;
	case MVPW_KEY_MENU:
		mvpw_show(popup_menu);
		mvpw_focus(popup_menu);
		break;
	case MVPW_KEY_MUTE:
		if (mvpw_visible(ffwd_widget)) {
			mvpw_hide(mute_widget);
			break;
		}
		if (av_mute() == 1) {
			mvpw_show(mute_widget);
		} else {
			mvpw_hide(mute_widget);
		}
		break;
	case MVPW_KEY_BLANK:
	case MVPW_KEY_OK:
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
	case MVPW_KEY_FULL:
	case MVPW_KEY_PREV_CHAN:
		if(IS_4x3(av_get_tv_aspect())) {
			if(av_get_tv_aspect() == AV_TV_ASPECT_4x3_CCO)
				av_set_tv_aspect(AV_TV_ASPECT_4x3);
			else
				av_set_tv_aspect(AV_TV_ASPECT_4x3_CCO);
		}
		break;
	case MVPW_KEY_CHAN_UP:
	case MVPW_KEY_UP:
		if (mythtv_livetv)
			mythtv_channel_up();
		break;
	case MVPW_KEY_CHAN_DOWN:
	case MVPW_KEY_DOWN:
		if (mythtv_livetv)
			mythtv_channel_down();
		break;
	case MVPW_KEY_RECORD:
		/*
		 * XXX: This is a temporary hack until we figure out how
		 *      to tell when the audio and video are out of sync,
		 *      and correct it automatically.
		 */
		av_get_audio_sync(&async);
		av_get_video_sync(&vsync);
		printf("PRE SYNC:  a 0x%llx 0x%llx  v 0x%llx 0x%llx\n",
		       async.stc, async.pts, vsync.stc, vsync.pts);
		av_delay_video(1000);
		av_get_audio_sync(&async);
		av_get_video_sync(&vsync);
		printf("POST SYNC: a 0x%llx 0x%llx  v 0x%llx 0x%llx\n",
		       async.stc, async.pts, vsync.stc, vsync.pts);
		break;
	case MVPW_KEY_VOL_UP:
	case MVPW_KEY_VOL_DOWN:
		volume_key_callback(volume_dialog, key);
		mvpw_show(volume_dialog);
		mvpw_set_timer(volume_dialog, timer_hide, 3000);
		break;
	default:
		PRINTF("button %d\n", key);
		break;
	}
}

void
add_audio_streams(mvp_widget_t *widget, mvpw_menu_item_attr_t *item_attr)
{
	demux_attr_t *attr;
	int i;
	char buf[32];
	unsigned int id;
	stream_type_t type;
	char *str = "";

	mvpw_clear_menu(widget);

	attr = demux_get_attr(handle);

	for (i=0; i<attr->audio.existing; i++) {
		id = attr->audio.ids[i].id;
		type = attr->audio.ids[i].type;
		switch (type) {
		case STREAM_MPEG:
			str = "MPEG";
			break;
		case STREAM_AC3:
			str = "AC3";
			break;
		case STREAM_PCM:
			str = "PCM";
			break;
		}
		snprintf(buf, sizeof(buf), "Stream ID 0x%x - %s", id, str);
		mvpw_add_menu_item(widget, buf, (void*)id, item_attr);
	}

	mvpw_check_menu_item(widget, (void*)attr->audio.current, 1);
}

void
add_video_streams(mvp_widget_t *widget, mvpw_menu_item_attr_t *item_attr)
{
	demux_attr_t *attr;
	int i;
	char buf[32];
	unsigned int id;
	stream_type_t type;
	char *str = "";

	mvpw_clear_menu(widget);

	attr = demux_get_attr(handle);

	for (i=0; i<attr->video.existing; i++) {
		id = attr->video.ids[i].id;
		type = attr->video.ids[i].type;
		if (type == STREAM_MPEG)
			str = "MPEG";
		snprintf(buf, sizeof(buf), "Stream ID 0x%x - %s", id, str);
		mvpw_add_menu_item(widget, buf, (void*)id, item_attr);
	}

	mvpw_check_menu_item(widget, (void*)attr->video.current, 1);
}

void
add_subtitle_streams(mvp_widget_t *widget, mvpw_menu_item_attr_t *item_attr)
{
	demux_attr_t *attr;
	int i, current;
	char buf[32];

	mvpw_clear_menu(widget);

	attr = demux_get_attr(handle);

	current = demux_spu_get_id(handle);

	for (i=0; i<32; i++) {
		if (attr->spu[i].bytes > 0) {
			snprintf(buf, sizeof(buf), "Stream ID 0x%x", i);
			mvpw_add_menu_item(widget, buf, (void*)i, item_attr);
			if (current == i)
				mvpw_check_menu_item(widget, (void*)i, 1);
			else
				mvpw_check_menu_item(widget, (void*)i, 0);
		}
	}
}

int
audio_switch_stream(mvp_widget_t *widget, int stream)
{
	demux_attr_t *attr;

	attr = demux_get_attr(handle);

	if (attr->audio.current != stream) {
		stream_type_t type;
		int old, ret;

		old = attr->audio.current;

		if ((ret=demux_set_audio_stream(handle, stream)) < 0)
			return -1;
		type = (stream_type_t)ret;

		if (widget) {
			mvpw_check_menu_item(widget, (void*)old, 0);
			mvpw_check_menu_item(widget, (void*)stream, 1);
		}

		if (type == STREAM_MPEG)
			av_set_audio_output(AV_AUDIO_MPEG);
		else
			av_set_audio_output(AV_AUDIO_AC3);

		fd_audio = av_get_audio_fd();

		printf("switched from audio stream 0x%x to 0x%x\n",
		       old, stream);

		if (type != audio_output) {
			printf("switching audio output types\n");
			audio_output = type;
		}
	}

	return 0;
}

void
video_switch_stream(mvp_widget_t *widget, int stream)
{
	demux_attr_t *attr;

	attr = demux_get_attr(handle);

	if (attr->video.current != stream) {
		int old, ret;

		old = attr->video.current;

		if ((ret=demux_set_video_stream(handle, stream)) < 0)
			return;

		mvpw_check_menu_item(widget, (void*)old, 0);
		mvpw_check_menu_item(widget, (void*)stream, 1);

		printf("switched from video stream 0x%x to 0x%x\n",
		       old, stream);
	}
}

void
subtitle_switch_stream(mvp_widget_t *widget, int stream)
{
	int old;

	old = demux_spu_get_id(handle);

	demux_spu_set_id(handle, stream);

	if (old >= 0)
		mvpw_check_menu_item(widget, (void*)old, 0);
	if (old != stream) {
		mvpw_check_menu_item(widget, (void*)stream, 1);
		printf("switched from subtitle stream 0x%x to 0x%x\n",
		       old, stream);
	}
}

static int
do_seek(void)
{
	demux_attr_t *attr;
	int seconds, new_seek_Bps;
	long long offset;
	struct timeval now, delta;
	static int count = 0;

	count++;

	attr = demux_get_attr(handle);
	gettimeofday(&now, NULL);
	timersub(&now, &seek_timeval, &delta);

	/*
	 * Give up after 2 seconds
	 */
	if ((delta.tv_sec >= 2) && seeking) {
		seeking = 0;
		printf("SEEK ABORTED (%lu.%.2lu) %d\n",
		       delta.tv_sec, delta.tv_usec/10000, count);
		count = 0;

		return 0;
	}

	if (!attr->gop_valid) {
		if ( --seek_attempts > 0 ) {
			PRINTF("GOP retry\n");
			return -1;
		}
		printf("SEEK RETRY due to lack of GOP\n");
		demux_flush(handle);
		demux_seek(handle);
		seek_attempts = 16;
		return -1;
	}

	if (pts_seek_attempts > 0) {
		seconds = attr->gop.pts/PTS_HZ;
	} else if (gop_seek_attempts > 0) {
		seconds = (attr->gop.hour * 3600) +
			(attr->gop.minute * 60) + attr->gop.second;
	} else {
		seconds = 0;
	}

	/*
	 * Recompute Bps from actual time and position differences
	 * provided the time difference is big enough
	 */
	if ( abs(seconds - seek_start_seconds) > SEEK_FUDGE ) {
		offset = video_functions->seek(0, SEEK_CUR);
		new_seek_Bps = (offset - seek_start_pos) /
			(seconds - seek_start_seconds);
		if ( new_seek_Bps > 10000 ) /* Sanity check */
			seek_Bps = new_seek_Bps;
	}

	PRINTF("New Bps %d\n", seek_Bps);
	
	if ( abs(seconds - seek_seconds) <= SEEK_FUDGE ) {
		seeking = 0;
		printf("SEEK DONE: to %d at %d (%lu.%.2lu) %d\n",
		       seek_seconds, seconds,
		       delta.tv_sec, delta.tv_usec/10000, count);
	} else {
		offset = video_functions->seek(0, SEEK_CUR);
		PRINTF("RESEEK: From %lld + %d\n", offset,
		       seek_Bps * (seek_seconds-seconds));
		offset = video_functions->seek(seek_Bps * (seek_seconds-seconds), SEEK_CUR);
		demux_flush(handle);
		demux_seek(handle);
		seek_attempts--;
		PRINTF("SEEKING 1: %d/%d %lld\n",
		       seconds, seek_seconds, offset);
		return -1;
	}

	count = 0;

	return 0;
}

static long long
file_size(void)
{
	if ( http_playing == HTTP_FILE_CLOSED ) {
		struct stat64 sb;
		fstat64(fd, &sb);
		return sb.st_size;    
	} else {
		return 40000000;
	}
}

static long long
file_seek(long long offset, int whence)
{
	return lseek(fd, offset, whence);
}

int
file_read(char *buf, int len)
{
	/*
	 * Force myth recordings to start with the numerically
	 * lowest audio stream, rather than the first audio
	 * stream seen in the file.
	 */
	if (running_mythtv && !audio_selected) {
		if (audio_switch_stream(NULL, 0xc0) == 0) {
			printf("selected audio stream 0xc0\n");
			audio_selected = 1;
		} else if (audio_checks++ == 4) {
			printf("audio stream 0xc0 not found\n");
			audio_selected = 1;
		}
	}

	return read(fd, buf, len);
}

int
file_open(void)
{
	seeking = 1;
	audio_selected = 0;
	audio_checks = 0;

	if ( http_playing == HTTP_FILE_CLOSED ) {
		if (video_reopen == 1)
			audio_clear();
		
		close(fd);
		fd = -1;
	}

	pthread_kill(video_write_thread, SIGURG);
	pthread_kill(audio_write_thread, SIGURG);

	if ( http_playing == HTTP_FILE_CLOSED ) {
		if (gui_state != MVPMC_STATE_EMULATE) {
			fd=open(current, O_RDONLY|O_LARGEFILE);
		} else {
			fd = open("/tmp/FIFO", O_RDONLY);
		}
		if (fd < 0) {
			printf("Open failed errno %d file %s\n",
			       errno, current);
			video_reopen = 0;
			return -1;
		}
		printf("opened %s\n", current);
	} else {
		printf("http opened %s\n", current);
	}

	if (video_reopen == 1) {
		av_set_audio_output(AV_AUDIO_MPEG);
		fd_audio = av_get_audio_fd();

		av_play();

		demux_reset(handle);
		ts_demux_reset(tshandle);
		demux_attr_reset(handle);
		demux_seek(handle);
		vid_event_discontinuity_possible();
		if (gui_state == MVPMC_STATE_EMULATE || http_playing == HTTP_VIDEO_FILE_MPG) {
			video_thumbnail(AV_THUMBNAIL_OFF, 0);
		} else {
			video_thumbnail(AV_THUMBNAIL_EIGTH, 0);
		}
		av_play();
	}

	zoomed = 0;
	display_on = 0;

	seeking = 0;
	jumping = 0;
	audio_type = 0;
	pcm_decoded = 0;
	ac3len = 0;

	video_reopen = 0;

	pthread_cond_broadcast(&video_cond);

	printf("write threads released\n");

	return 0;
}

void*
video_read_start(void *arg)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	int ret;
	int n = 0, len = 0, reset = 1;
	int sent_idle_notify;
	demux_attr_t *attr;
	video_info_t *vi;
	int set_aspect = 1;
	char *inbuf = inbuf_static;
	char *tsbuf;
	int tslen;
	int tsmode = TS_MODE_UNKNOWN;
	av_state_t state;

	pthread_mutex_lock(&mutex);

	printf("mpeg read thread started (pid %d)\n", getpid());
	pthread_cond_wait(&video_cond, &mutex);

	while (1) {
		sent_idle_notify = 0;
		while (!video_playing) {
			demux_reset(handle);
			ts_demux_reset(tshandle);
			demux_seek(handle);
			vid_event_discontinuity_possible();
			if ( !(sent_idle_notify) ) {
				if ( video_functions != NULL &&
				     video_functions->notify != NULL ) {
					video_functions->notify(MVP_READ_THREAD_IDLE);
				}
				printf("mpeg read thread sleeping...\n");
				sent_idle_notify = 1;
			}
			pthread_cond_wait(&video_cond, &mutex);
			TRC("%s: past pthread_cond_wait(&video_cond, &mutex)\n", __FUNCTION__);
		}

#ifdef STREAM_TEST
		if ( stream_test_started == 0 ) {
			stream_test_started = 1;
			//Get start time
			gettimeofday(&start_tv, NULL);
		}
#endif

		if (video_reopen) {
		        vid_event_clear();
			if (video_functions->open() == 0) {
				/* Jump to the start of the new file */
				jump_target = 0;
				jumping = 1;
				video_reopen = 0;
				tsmode = TS_MODE_UNKNOWN;
			} else {
				fprintf(stderr, "video open failed!\n");
				video_playing = 0;
				continue;
			}
			len = 0;
			reset = 1;
			set_aspect = 1;
		}

		if ((seeking && reset) || jumping) {
			demux_reset(handle);
			ts_demux_reset(tshandle);
			demux_seek(handle);
			av_get_state(&state);
			av_reset();
			av_reset_stc();
			vid_event_discontinuity_possible();
			if (seeking)
				reset = 0;
			if (state.mute)
				av_set_mute(1);
			if (paused) {
				av_play();
				paused = 0;
				mvpw_hide(pause_widget);
				screensaver_disable();
			}
			pcm_decoded = 0;
			ac3len = 0;
			len = 0;
			if (jumping) {
				while (jump_target < 0)
					usleep(1000);
				video_functions->seek(jump_target, SEEK_SET);
			}
			jumping = 0;
		}

		if ( !video_playing ) {
			continue;
		}

		if (len == 0) {

			if ( video_functions->read_dynb != NULL ){
				tslen = video_functions->read_dynb(&tsbuf, 1024 * 256);
			}
			else {
				tsbuf = tsbuf_static;
				do {
					tslen = video_functions->read(tsbuf,
						      sizeof(tsbuf_static));
				} while ( tslen==-1 && errno==EAGAIN);
			}
			thruput_count += tslen;
			inbuf = inbuf_static;

			if (tsmode == TS_MODE_UNKNOWN) {
				if (tslen > 0) {
					tsmode = ts_demux_is_ts(tshandle, tsbuf, tslen);
					printf("auto detection transport stream returned %d\n", tsmode);
					if (tsmode == TS_MODE_NO)
						len = tslen;
			    	}
			} else if (tsmode == TS_MODE_NO) {
				len = tslen;
			} else {
				len = ts_demux_transform(tshandle, tsbuf, tslen, inbuf, sizeof(inbuf_static));
				int resyncs = ts_demux_resync_count(tshandle);
				if (resyncs > 50) {
					printf("resync count = %d, switch back to unknown mode\n", resyncs);
					tsmode = TS_MODE_UNKNOWN;
					ts_demux_reset(tshandle);
				}
			}
			n = 0;
			if (len == 0 && playlist ) {
				video_reopen = 2;
				playlist_next();
			}
		}

		if ( !video_playing ) {
			continue;
		}

#ifdef STREAM_TEST
		stream_test_cnt += len;
		len = 0;

		if ( stream_test_cnt > 1024*1024*20 ) {
			unsigned int delta_ms;

			gettimeofday(&done_tv, NULL);
			if ( done_tv.tv_usec < start_tv.tv_usec ) {
				done_tv.tv_usec += 1000000;
				done_tv.tv_sec	 -= 1;
			}
			delta_ms = (done_tv.tv_sec - start_tv.tv_sec) * 1000;
			delta_ms += (done_tv.tv_usec - start_tv.tv_usec) / 1000;
			printf("Test Done\n");
			printf("Bytes transferred: %u\n", stream_test_cnt);
			printf("Elapsed time %u mS\n", delta_ms);
			while ( 1 ) {
				sleep(10);
				printf("Test Done....\n");
			}
		}
		continue;
#else
		if (tsmode == TS_MODE_YES)
			ret = DEMUX_PUT(handle, inbuf+n, len-n);
		else
			ret = DEMUX_PUT(handle, tsbuf+n, len-n);
#endif

		if ((ret <= 0) && (!seeking)) {
			pthread_cond_broadcast(&video_cond);
			usleep(1000);
			continue;
		}

		if (seeking) { 
			if (do_seek()) {
				len = 0;
				continue;
			} else {
				reset = 1;
			}
		}

		n += ret;
		if (n == len)
			len = 0;

		pthread_cond_broadcast(&video_cond);

		attr = demux_get_attr(handle);
		vi = &attr->video.stats.info.video;
		if (attr->audio.type != audio_type) {
			audio_type = attr->audio.type;
			switch (audio_type) {
			case AUDIO_MODE_AC3:
				if (audio_output_mode == AUD_OUTPUT_PASSTHRU ) {
					if (av_set_audio_output(AV_AUDIO_AC3) < 0) {
						/* revert to downmixing */
						audio_output_mode = AUD_OUTPUT_STEREO;
					    // fall through to PCM
					} else {
                                    // don't set audio_type
						audio_output = AV_AUDIO_AC3;
						printf("switch to AC3 Passthru\n");
						break;
					}
				}
			case AUDIO_MODE_PCM:
				audio_output = AV_AUDIO_PCM;
				printf("switch to PCM audio output device\n");
				break;
			default:
				av_set_audio_type(audio_type);
				audio_output = AV_AUDIO_MPEG;
				printf("switch to MPEG audio output device\n");
				break;
			}
			av_set_audio_output(audio_output);
		} else {
			if (audio_type==AUDIO_MODE_AC3){
				sync_ac3_audio();
			}
		}

	} //while

	return NULL;
}

void*
video_events_start(void *arg)
{
    while(1)
    {
	eventq_type_t type = -1;
	void *pData = NULL;
	vid_event_wait_next(&type,&pData);
	switch(type)
	{
	    case VID_EVENT_ASPECT:
		    {
			  aspect_change_t *pAspect = (aspect_change_t *)pData;
			  video_change_aspect(pAspect->aspect,pAspect->afd);
		    }
		    break;
	    default:
		    fprintf(stderr,"Pulled unknown item off the vid event queue (type %d)\n",type);
	}
	if(pData != NULL)
	    free(pData);
    }
    return NULL;
}

static void video_change_aspect(int new_aspect, int new_afd)
{
    printf("Changing to aspect %d, afd %d\n", new_aspect, new_afd);
    if (new_aspect != 0 && new_aspect != -1) {
	av_wss_aspect_t wss;
	if (new_aspect == 3) {
	    printf("Source video aspect ratio: 16:9\n");
	    fflush(stdout);
	    wss = av_set_video_aspect(AV_VIDEO_ASPECT_16x9, new_afd);
	} else {
	    printf("Source video aspect ratio: 4:3\n");
	    fflush(stdout);
	    wss = av_set_video_aspect(AV_VIDEO_ASPECT_4x3, new_afd);
	}
	av_wss_update_aspect(wss);
    } else {
	printf("Video aspect reported as 0 or -1 - not changing setting\n");
	fflush(stdout);
    }
}

void*
video_write_start(void *arg)
{
	int idle = 1;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	int len;
	sigset_t sigs;

	signal(SIGURG, sighandler);
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGURG);
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);

	pthread_mutex_lock(&mutex);

	printf("video write thread started (pid %d)\n", getpid());
	pthread_cond_wait(&video_cond, &mutex);

	while (1) {
		while (!video_playing) {
			if ( !(idle) ) {
				sem_post(&write_threads_idle_sem);
				idle = 1;
				printf("video write thread sleeping...\n");
			}
			pthread_cond_wait(&video_cond, &mutex);
		}
		if ( idle ) {
			sem_wait(&write_threads_idle_sem);
			idle = 0;
			printf("video write thread running\n");
		}

		while (seeking || jumping)
			pthread_cond_wait(&video_cond, &mutex);
#ifdef STREAM_TEST
		sleep(1);
#else
		if (video_playing && (len=DEMUX_WRITE_VIDEO(handle, fd_video)) > 0)
			pthread_cond_broadcast(&video_cond);
		else
			pthread_cond_wait(&video_cond, &mutex);
#endif 
	}

	return NULL;
}

void
timed_osd(int timeout)
{
	if ((timeout == 0) || display_on)
		return;

	display_on_alt = 1;

	enable_osd();
	mvpw_set_timer(root, seek_disable_osd, timeout);
}

static inline unsigned int get_cur_vid_stc()
{
    pts_sync_data_t pts_struct;
    av_get_video_sync(&pts_struct);
    return pts_struct.stc & 0xFFFFFFFF;
}

static void
video_unpause_timer_callback(mvp_widget_t * widget)
{
        if(!paused)
		av_play();
	mvpw_set_timer(widget,NULL,0);
}

void*
audio_write_start(void *arg)
{
	int idle = 1;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	int len;
	sigset_t sigs;
	demux_attr_t *attr;
	video_info_t *vi;

	signal(SIGURG, sighandler);
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGURG);
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);

	pthread_mutex_lock(&mutex);

	printf("audio write thread started (pid %d)\n", getpid());
	pthread_cond_wait(&video_cond, &mutex);

	while (1) {
		while (seeking || jumping)
			pthread_cond_wait(&video_cond, &mutex);

		while (!video_playing) {
			pcm_decoded = 0;
			empty_ac3();
			if ( !(idle) ) {
				sem_post(&write_threads_idle_sem);
				idle = 1;
				printf("audio write thread sleeping...\n");
			}
			pthread_cond_wait(&video_cond, &mutex);
		}
		if ( idle ) {
			sem_wait(&write_threads_idle_sem);
			idle = 0;
			printf("audio write thread running\n");
		}

		if (running_replaytv) {
			attr = demux_get_attr(handle);
			vi = &attr->video.stats.info.video;
			if (attr->audio.type != audio_type) {
            // JBH: FIX ME: This code never gets hit
				audio_type = attr->audio.type;
				switch (audio_type) {
				case AUDIO_MODE_AC3:
				case AUDIO_MODE_PCM:
					audio_output = AV_AUDIO_PCM;
					printf("switch to PCM audio\n");
					break;
				default:
					av_set_audio_type(audio_type);
					audio_output = AV_AUDIO_MPEG;
					printf("switch to MPEG audio\n");
					break;
				}
				av_set_audio_output(audio_output);
			}
		}

		switch (audio_type) {
		case AUDIO_MODE_MPEG1_PES:
		case AUDIO_MODE_MPEG2_PES:
		case AUDIO_MODE_ES:
			if (jit_mode == 0) {
				if ((len=DEMUX_WRITE_AUDIO(handle, fd_audio)) > 0)
					pthread_cond_broadcast(&video_cond);
				else
					pthread_cond_wait(&video_cond, &mutex);
			} else {
			    int flags, duration;
			    len=DEMUX_JIT_WRITE_AUDIO(handle, fd_audio,
						get_cur_vid_stc(),jit_mode,
						&flags,&duration);
			    if(flags & 4)
			    {
				/*4 is the same as 1 followed by 2*/
				flags = (flags | 1 | 2) & ~4;
			    }
			    if(flags & 1)
			    {
				av_pause_video();
			    }
			    if((flags & 2) && !paused)
			    {
				if(duration <= 10)
				    video_unpause_timer_callback(pause_widget);
				else
				    mvpw_set_timer(pause_widget,
				       video_unpause_timer_callback, duration);
			    }
			    if(flags & 8)
			    {
				usleep(duration*1000);
			    }


			    if(len > 0)
				    pthread_cond_broadcast(&video_cond);
			    else if(!(flags & 8))
				    pthread_cond_wait(&video_cond, &mutex);
			}
			break;
		case AUDIO_MODE_PCM:
			/*
			 * XXX: PCM audio does not work yet
			 */
			pthread_cond_wait(&video_cond, &mutex);
			break;
		case AUDIO_MODE_AC3:
			if (audio_output_mode == AUD_OUTPUT_PASSTHRU ) {
				if ((len=DEMUX_WRITE_AUDIO(handle, fd_audio)) > 0)
					pthread_cond_broadcast(&video_cond);
				else
					pthread_cond_wait(&video_cond, &mutex);
				break;
			}
			if (ac3more == -1)
				ac3more = a52_decode_data(NULL, NULL,
							  pcm_decoded);
			if (ac3more == 1) {
				ac3more = a52_decode_data(ac3buf,
							  ac3buf + ac3len,
							  pcm_decoded);
				if (ac3more == 1) {
					pthread_cond_wait(&video_cond, &mutex);
					break;
				}
			}

			if (ac3more == 0) {
				ac3len = demux_get_audio(handle, ac3buf,
							 sizeof(ac3buf));
				ac3more = 1;
			}

			if (ac3more == 0)
				pthread_cond_wait(&video_cond, &mutex);
			else
				pthread_cond_broadcast(&video_cond);

			pcm_decoded = 1;
			break;
		}
	}

	return NULL;
}

static int 
demux_write_video_nop(demux_handle_t *handle, int fd)
{
	static int null = -1;
	int len;

	if (null < 0)
		null = open("/dev/null", O_WRONLY);

	len = demux_write_video(handle, null);

	return len;
}

static int 
demux_write_audio_nop(demux_handle_t *handle, int fd)
{
	static int null = -1;
	int len;

	if (null < 0)
		null = open("/dev/null", O_WRONLY);

	len = demux_write_audio(handle, null);

	return len;
}

static int
demux_jit_write_audio_nop(demux_handle_t *handle, int fd, unsigned int pts,
			  int mode, int* flags, int *duration)
{
    return demux_write_audio_nop(handle,fd);
}

void
start_thruput_test(void)
{
	switch_hw_state(MVPMC_STATE_NONE);

	DEMUX_WRITE_VIDEO = demux_write_video_nop;
	DEMUX_WRITE_AUDIO = demux_write_audio_nop;
	DEMUX_JIT_WRITE_AUDIO = demux_jit_write_audio_nop;

	thruput = 1;

	gettimeofday(&thruput_start, NULL);

	mvpw_set_text_str(thruput_widget, "Press STOP to end test.");
	mvpw_show(thruput_widget);
	mvpw_focus(thruput_widget);
}

void
end_thruput_test(void)
{
	struct timeval thruput_end, delta;
	char buf[256];
	float rate, sec;

	if (thruput == 0)
		return;

	gettimeofday(&thruput_end, NULL);

	if ( video_functions->halt_stream != NULL ) {
 		video_functions->halt_stream();
	}
   
	timersub(&thruput_end, &thruput_start, &delta);

	sec = ((float)delta.tv_sec + (delta.tv_usec / 1000000.0));
	rate = (float)thruput_count / sec;
	rate = (rate * 8) / (1024 * 1024);

	snprintf(buf, sizeof(buf),
		 "Bytes: %d\nSeconds: %5.2f\nThroughput: %5.2f mb/s",
		 thruput_count, sec, rate);
	printf("thruput test:\n%s\n", buf);

	switch_hw_state(MVPMC_STATE_NONE);

	DEMUX_WRITE_VIDEO = demux_write_video;
	DEMUX_WRITE_AUDIO = demux_write_audio;
	DEMUX_JIT_WRITE_AUDIO = demux_jit_write_audio;

	thruput = 0;
	thruput_count = 0;

	mvpw_set_text_str(thruput_widget, buf);
}

#include <sys/ioctl.h>

#define AC3PASS  0xf800
#define AC3FIXED 0x5400
#define AC3OK    0x400

void sync_ac3_audio(void)
{
#ifndef MVPMC_HOST
	pts_sync_data_t async, vsync;
	long long syncDiff;
	int threshold=0;

	if (audio_output_mode == AUD_OUTPUT_PASSTHRU ) {
		threshold = AC3PASS;
	} else {
		threshold = AC3FIXED;
	}
	av_get_audio_sync(&async);
	av_get_video_sync(&vsync);
	syncDiff = async.stc-vsync.stc;
#if 0

	printf("PRE SYNC:  a 0x%llx 0x%llx  v 0x%llx 0x%llx 0x%llx\n",
		async.stc, async.pts, vsync.stc, vsync.pts, syncDiff);
#endif
		
#if 1
	if ( abs(syncDiff) > AC3OK ) {
		if ( syncDiff < threshold ) {
			av_delay_video(threshold-syncDiff);
		} else if ( syncDiff > threshold  ) {
                        mvpstb_audio_end();
			/*
			if (ioctl(fd_audio, _IOW('a',4,int), 0) < 0) {
			} else {
			}
			*/
		}
	}

#endif
#endif	
}
