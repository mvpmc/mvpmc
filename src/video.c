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
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>

#include <mvp_demux.h>
#include <mvp_widget.h>
#include <mvp_av.h>

#include "mvpmc.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

#define BSIZE	(1024*96)
#define SEEK_FUDGE	2

static char inbuf[BSIZE];

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t video_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int fd = -1;
static av_stc_t seek_stc;
static int seek_seconds;
static volatile int seeking = 0;
static volatile int jumping = 0;
static volatile int seek_bps;
static volatile int seek_attempts;
static volatile int gop_seek_attempts;
static volatile off_t seek_start_pos;
static volatile int seek_start_seconds;
static volatile int audio_type = 0;
static volatile int pcm_decoded = 0;
static int zoomed = 0;
static int display_on = 0;

int fd_audio, fd_video;
demux_handle_t *handle;

static stream_type_t audio_output = STREAM_MPEG;

static unsigned char ac3buf[1024*32];
static volatile int ac3len = 0, ac3more = 0;

static volatile int video_reopen = 0;
static volatile int video_playing = 0;

void video_play(mvp_widget_t *widget);

static void
sighandler(int sig)
{
	/*
	 * The signal handler is here simply to allow the threads writing to
	 * the hardware devices to be interrupted, and break out of the write()
	 * system call.
	 */
}

static void
video_show_widgets(void)
{
	mvpw_show(file_browser);
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
	if (demux_spu_get_id(handle) >= 0)
		mvpw_set_timer(root, video_subtitle_display, 250);
	else
		mvpw_set_timer(root, video_subtitle_check, 1000);

	if (! (mvpw_visible(file_browser) ||
	       mvpw_visible(mythtv_browser) ||
	       mvpw_visible(replaytv_browser))) {
		av_move(0, 0, 0);
	}
}

void
video_play(mvp_widget_t *widget)
{
	mvpw_set_idle(NULL);

	if (demux_spu_get_id(handle) < 0)
		mvpw_set_timer(root, video_subtitle_check, 1000);
	else
		mvpw_set_timer(root, video_subtitle_display, 250);

	video_reopen = 1;
	video_playing = 1;
	pthread_cond_broadcast(&video_cond);
}

void
video_clear(void)
{
	close(fd);
	fd = -1;
	video_playing = 0;
	pthread_kill(video_write_thread, SIGURG);
	pthread_kill(audio_write_thread, SIGURG);
	av_reset();
	pthread_cond_broadcast(&video_cond);
}

void
video_progress(mvp_widget_t *widget)
{
	struct stat64 sb;
	long long offset;
	int off;
	char buf[32];

	fstat64(fd, &sb);
	offset = lseek(fd, 0, SEEK_CUR);
	off = (int)((double)(offset/1000) /
		    (double)(sb.st_size/1000) * 100.0);
	snprintf(buf, sizeof(buf), "%d%%", off);
	mvpw_set_text_str(offset_widget, buf);

	mvpw_set_graph_current(offset_bar, off);

	mvpw_expose(offset_widget);
	mvpw_expose(offset_bar);
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
	mvpw_set_text_str(time_widget, buf);

	mvpw_expose(time_widget);
}

void
video_bitrate(mvp_widget_t *widget)
{
	demux_attr_t *attr;
	av_stc_t stc;
	char buf[32];
	int mb;

	attr = demux_get_attr(handle);
	av_current_stc(&stc);
	mb = (attr->bps * 8) / (1024 * 1024);
	snprintf(buf, sizeof(buf), "%d.%.2d mbps",
		 mb, (attr->bps * 8) / (1024 * 1024 / 100) - (mb * 100));
	mvpw_set_text_str(bps_widget, buf);

	mvpw_expose(bps_widget);
}

void
video_clock(mvp_widget_t *widget)
{
	time_t t;
	struct tm *tm;
	char buf[64];

	t = time(NULL);
	tm = localtime(&t);

	sprintf(buf, "%.2d:%.2d:%.2d", tm->tm_hour, tm->tm_min, tm->tm_sec);

	mvpw_set_text_str(widget, buf);
	mvpw_expose(widget);
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
	long long offset;
	int off, mb;
	char buf[32];
	av_stc_t stc;
	demux_attr_t *attr;

	fstat64(fd, &sb);
	offset = lseek(fd, 0, SEEK_CUR);
	off = (int)((double)(offset/1000) /
		    (double)(sb.st_size/1000) * 100.0);
	snprintf(buf, sizeof(buf), "%d", off);
	mvpw_set_text_str(offset_widget, buf);

	mvpw_set_graph_current(offset_bar, off);

	attr = demux_get_attr(handle);
	av_current_stc(&stc);
	mb = (attr->bps * 8) / (1024 * 1024);
	snprintf(buf, sizeof(buf), "mbps: %d.%.2d   Time: %.2d:%.2d:%.2d",
		 mb, (attr->bps * 8) / (1024 * 1024 / 100) - (mb * 100),
		 stc.hour, stc.minute, stc.second);
	mvpw_set_text_str(bps_widget, buf);

	mvpw_expose(bps_widget);
	mvpw_expose(widget);
	mvpw_expose(offset_widget);
	mvpw_expose(offset_bar);
}

static void
seek_by(int seconds)
{
	demux_attr_t *attr = demux_get_attr(handle);
	int delta;
	int stc_time, gop_time;

	if ( !attr->gop_valid )
		return; /* Don't know where to start from */

	seeking = 1;

	pthread_kill(video_write_thread, SIGURG);
	pthread_kill(audio_write_thread, SIGURG);

	av_current_stc(&seek_stc);

	gop_time = (attr->gop.hour*60 + attr->gop.minute)*60 +
		attr->gop.second;
	stc_time = (seek_stc.hour*60 + seek_stc.minute)*60 + seek_stc.second;

	/*
	 * If the STC and GOP timestamps are close, use the STC timestamp as
	 * the starting point, because it is the timestamp for the frame the
	 * hardware is currently playing.
	 */
	if (abs(gop_time - stc_time) > 10) {
		printf("GOP SEEK from: %.2d:%.2d:%.2d\n",
		       attr->gop.hour, attr->gop.minute, attr->gop.second);

		seek_start_seconds = (attr->gop.hour*60 +
				      attr->gop.minute)*60 + attr->gop.second;
	} else {
		printf("STC SEEK from: %.2d:%.2d:%.2d\n",
		       seek_stc.hour, seek_stc.minute, seek_stc.second);

		seek_start_seconds = (seek_stc.hour*60 +
				      seek_stc.minute)*60 + seek_stc.second;
	}

	seek_seconds = seek_start_seconds + seconds;
	delta = attr->bps * seconds;
	seek_start_pos = lseek(fd, 0, SEEK_CUR);

	printf("%d bps, currently 0x%llx + 0x%x\n",
	       attr->bps, seek_start_pos, delta);

	lseek(fd, delta, SEEK_CUR);

	printf("-> 0x%llx\n", lseek(fd, 0, SEEK_CUR));

	if (attr->bps == 0)
		seek_bps = ((1024*1024) * 4) / 8;  /* 4 megabits per second */
	else
		seek_bps = attr->bps;
	seek_attempts = 8;
	gop_seek_attempts = 4;

	pthread_cond_broadcast(&video_cond);
}

static void
disable_osd(void)
{
	set_osd_callback(OSD_BITRATE, NULL);
	set_osd_callback(OSD_CLOCK, NULL);
	set_osd_callback(OSD_DEMUX, NULL);
	set_osd_callback(OSD_PROGRESS, NULL);
	set_osd_callback(OSD_PROGRAM, NULL);
	set_osd_callback(OSD_TIMECODE, NULL);
}

void
video_callback(mvp_widget_t *widget, char key)
{
	struct stat64 sb;
	long long offset;
	int jump;

	switch (key) {
	case '.':
	case 'E':
		disable_osd();
		if (si.rows == 480)
			av_move(475, si.rows-60, 4);
		else
			av_move(475, si.rows-113, 4);
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
		if (running_mythtv) {
			mvpw_show(mythtv_logo);
			mvpw_show(mythtv_channel);
			mvpw_show(mythtv_date);
			mvpw_show(mythtv_description);
			mvpw_show(mythtv_browser);
			mvpw_focus(mythtv_browser);
		} else if (running_replaytv) {
			mvpw_show(replaytv_browser);
			mvpw_focus(replaytv_browser);
		} else {
			mvpw_show(file_browser);
			mvpw_focus(file_browser);
		}
		mvpw_expose(root);
		break;
	case ',':
		if (av_pause()) {
			mvpw_show(pause_widget);
		} else {
			mvpw_hide(pause_widget);
			mvpw_hide(mute_widget);
		}
		break;
	case '(':
		seek_by(-30);
		break;
	case '{':
		seek_by(-10);
		break;
	case ')':
		seek_by(30);
		break;
	case '}':
		if (av_ffwd() == 0) {
			demux_flush(handle);
			demux_seek(handle);
			av_stop();
			av_play();
			mvpw_hide(ffwd_widget);
			mvpw_hide(mute_widget);
		} else {
			av_mute();
			mvpw_show(ffwd_widget);
		}
		break;
	case '0' ... '9':
		jumping = 1;
		pthread_kill(video_write_thread, SIGURG);
		pthread_kill(audio_write_thread, SIGURG);
		jump = key - '0';
		fstat64(fd, &sb);
		offset = sb.st_size * (jump / 10.0);
		lseek(fd, offset, SEEK_SET);
		pthread_cond_broadcast(&video_cond);
		break;
	case 'M':
		mvpw_show(popup_menu);
		mvpw_focus(popup_menu);
		break;
	case 'Q':
		if (av_mute() == 1)
			mvpw_show(mute_widget);
		else
			mvpw_hide(mute_widget);
		break;
	case ' ':
		if (display_on) {
			disable_osd();
			mvpw_expose(root);
		} else {
			set_osd_callback(OSD_PROGRESS, video_progress);
			set_osd_callback(OSD_TIMECODE, video_timecode);
			set_osd_callback(OSD_BITRATE, video_bitrate);
			set_osd_callback(OSD_CLOCK, video_clock);
			set_osd_callback(OSD_DEMUX, video_demux);
			if (running_mythtv)
				set_osd_callback(OSD_PROGRAM, mythtv_program);
		}
		display_on = !display_on;
		break;
	case 'L':
		av_set_video_aspect(1-av_get_video_aspect());
		break;
	case 'P':
		power_toggle();
		break;
	case '\n':
		av_move(0, 0, 0);
		pthread_cond_broadcast(&video_cond);
		break;
	default:
		printf("char '%c'\n", key);
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

void
audio_switch_stream(mvp_widget_t *widget, int stream)
{
	demux_attr_t *attr;

	attr = demux_get_attr(handle);

	if (attr->audio.current != stream) {
		stream_type_t type;
		int old;

		old = attr->audio.current;

		if ((type=demux_set_audio_stream(handle, stream)) < 0)
			return;

		mvpw_check_menu_item(widget, (void*)old, 0);
		mvpw_check_menu_item(widget, (void*)stream, 1);

		if (type == STREAM_MPEG)
			av_set_audio_output(AV_AUDIO_MPEG);
		else
			av_set_audio_output(AV_AUDIO_PCM);

		fd_audio = av_audio_fd();

		printf("switched from audio stream 0x%x to 0x%x\n",
		       old, stream);

		if (type != audio_output) {
			printf("switching audio output types\n");
			audio_output = type;
		}
	}
}

void
video_switch_stream(mvp_widget_t *widget, int stream)
{
	demux_attr_t *attr;

	attr = demux_get_attr(handle);

	if (attr->video.current != stream) {
		int old;
		stream_type_t type;

		old = attr->video.current;

		if ((type=demux_set_video_stream(handle, stream)) < 0)
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

	attr = demux_get_attr(handle);

	if ((seek_attempts <= 0) && seeking) {
		seeking = 0;
		printf("SEEK ABORTED\n");
	}

	if (seeking && !attr->gop_valid) {
		if ( --gop_seek_attempts > 0 ) {
			printf("GOP retry\n");
			return -1;
		}
		seek_attempts--;
		printf("SEEK RETRY due to lack of GOP\n");
		demux_flush(handle);
		demux_seek(handle);
		return -1;
	} else {
		gop_seek_attempts = 4;
	}

	if (seeking) {
		int seconds, new_seek_bps;

		seconds = (attr->gop.hour * 3600) + (attr->gop.minute * 60) +
			attr->gop.second;

		/*
		 * Recompute bps from actual time and position differences
		 * provided the time difference is big enough
		 */
		if ( abs(seconds - seek_start_seconds) > SEEK_FUDGE ) {
			new_seek_bps =
				(lseek(fd, 0, SEEK_CUR) - seek_start_pos) /
				(seconds - seek_start_seconds);
			if ( new_seek_bps > 10000 ) /* Sanity check */
				seek_bps = new_seek_bps;
		}

		printf("New BPS %d\n", seek_bps);

		if ( abs(seconds - seek_seconds) <= SEEK_FUDGE ) {
			seeking = 0;
			printf("SEEK DONE: to %d at %d\n",
			       seek_seconds, seconds);
		} else {
			printf("RESEEK: From 0x%llx + 0x%x\n",
			       lseek(fd, 0, SEEK_CUR),
			       seek_bps * (seek_seconds-seconds));
			lseek(fd, seek_bps * (seek_seconds-seconds), SEEK_CUR);
			demux_flush(handle);
			demux_seek(handle);
			seek_attempts--;
			printf("SEEKING 1: %d/%d 0x%llx\n",
			       seconds, seek_seconds, lseek(fd, 0, SEEK_CUR));
			return -1;
		}
	}

	return 0;
}

static void
open_file(void)
{
	seeking = 1;

	close(fd);

	pthread_kill(video_write_thread, SIGURG);
	pthread_kill(audio_write_thread, SIGURG);

	if ((fd=open(current, O_RDONLY|O_LARGEFILE)) < 0) {
		printf("failed to open %s\n", current);
		video_reopen = 0;
		return;
	}
	printf("opened %s\n", current);

	av_set_audio_output(AV_AUDIO_MPEG);
	fd_audio = av_audio_fd();

	av_play();

	demux_reset(handle);
	demux_attr_reset(handle);
	demux_seek(handle);

	if (si.rows == 480)
		av_move(475, si.rows-60, 4);
	else
		av_move(475, si.rows-113, 4);
	
	av_play();

	zoomed = 0;
	display_on = 0;

	seeking = 0;
	jumping = 0;
	audio_type = 0;
	pcm_decoded = 0;
	ac3len = 0;

	audio_clear();

	video_reopen = 0;

	pthread_cond_broadcast(&video_cond);

	printf("write threads released\n");
}
void*
video_read_start(void *arg)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	int ret;
	int n = 0, len = 0, reset = 1;
	demux_attr_t *attr;
	video_info_t *vi;
	int set_aspect = 1;

	pthread_mutex_lock(&mutex);

	printf("mpeg read thread started (pid %d)\n", getpid());
	pthread_cond_wait(&video_cond, &mutex);

	while (1) {
		while (!video_playing) {
			if (!running_replaytv) {
				demux_reset(handle);
				demux_seek(handle);
				printf("mpeg read thread sleeping...\n");
			} else {
				/*
				 * avoid using too much cpu when not needed
				 */
				sleep(1);
			}
			pthread_cond_wait(&video_cond, &mutex);
		}

		if (video_reopen) {
			open_file();
			len = 0;
			reset = 1;
			set_aspect = 1;
		}

		if ((seeking && reset) || jumping) {
			demux_reset(handle);
			demux_seek(handle);
			av_reset();
			if (seeking)
				reset = 0;
			pcm_decoded = 0;
			ac3len = 0;
			audio_clear();
			jumping = 0;
		}

		if (len == 0) {
			len = read(fd, inbuf, sizeof(inbuf));
			n = 0;
		}

		ret = demux_put(handle, inbuf+n, len-n);

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
		}

		if (set_aspect) {
			if (vi->aspect == 3) {
				av_set_video_aspect(1);
				printf("video aspect ratio: 16:9\n");
			} else {
				av_set_video_aspect(0);
				printf("video aspect ratio: 4:3\n");
			}
			set_aspect = 0;
		}
	}

	return NULL;
}

void*
video_write_start(void *arg)
{
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
		while (seeking || jumping)
			pthread_cond_wait(&video_cond, &mutex);
		if ((video_playing || running_replaytv) &&
		    (len=demux_write_video(handle, fd_video)) > 0)
			pthread_cond_broadcast(&video_cond);
		else
			pthread_cond_wait(&video_cond, &mutex);
	}

	return NULL;
}

void*
audio_write_start(void *arg)
{
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

		while (!video_playing && !running_replaytv) {
			pcm_decoded = 0;
			empty_ac3();
			pthread_cond_wait(&video_cond, &mutex);
		}

		if (running_replaytv) {
			attr = demux_get_attr(handle);
			vi = &attr->video.stats.info.video;
			if (attr->audio.type != audio_type) {
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
			if ((len=demux_write_audio(handle, fd_audio)) > 0)
				pthread_cond_broadcast(&video_cond);
			else
				pthread_cond_wait(&video_cond, &mutex);
			break;
		case AUDIO_MODE_PCM:
			/*
			 * XXX: PCM audio does not work yet
			 */
			pthread_cond_wait(&video_cond, &mutex);
			break;
		case AUDIO_MODE_AC3:
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
