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

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int fd = -1;
static av_stc_t seek_stc;
static int seeking;
static int seek_bps;
static int seek_attempts;
static int audio_type = 0;
static int zoomed = 0;
static int display_on = 0;

int fd_audio, fd_video;
demux_handle_t *handle;

static stream_type_t audio_output = STREAM_MPEG;

static unsigned char ac3buf[BSIZE];
static volatile int ac3len = 0, ac3more = 0;

void video_play(mvp_widget_t *widget);

static void
video_show_widgets(void)
{
	mvpw_show(file_browser);
}

void
video_player(int reset)
{
	static char buf[BSIZE];
	static int len = 0, n = 0, nput = 0;
	int alen = 0, vlen = 0;
	demux_attr_t *attr;
	video_info_t *vi;
	static int count = 0, resize = 0;
	pts_sync_data_t pts;
	static uint64_t first_stc = 0, old_pts = 0;
	static struct timeval start;
	struct timeval now, delta;
	spu_item_t *spu;
	static int set_aspect = 1;
	static int pcm_decoded = 0;

	if (reset) {
		n = 0;
		nput = 0;
		resize = 0;
		set_aspect = 1;
		pcm_decoded = 0;
		gettimeofday(&start, NULL);
	}

	if ((len=read(fd, buf+n, BSIZE-n)) > 0)
		n += len;
	nput += demux_put(handle, buf+nput, n-nput);

	if (nput == n) {
		n = 0;
		nput = 0;
	}

	attr = demux_get_attr(handle);
	vi = &attr->video.stats.info.video;
	if (attr->audio.type != audio_type) {
		audio_type = attr->audio.type;
		switch (audio_type) {
		case AUDIO_MODE_AC3:
			pthread_cond_signal(&cond);
		case AUDIO_MODE_PCM:
			audio_output = AV_AUDIO_PCM;
			printf("switching to PCM audio output device\n");
			break;
		default:
			av_set_audio_type(audio_type);
			audio_output = AV_AUDIO_MPEG;
			printf("switching to MPEG audio output device\n");
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

	if ((seek_attempts == 0) && seeking) {
		seeking = 0;
		printf("SEEK ABORTED\n");
	}

	if (seeking && !attr->gop_valid) {
		seek_attempts--;
		printf("SEEK RETRY due to lack of GOP\n");
		n = 0;
		nput = 0;
		demux_flush(handle);
		demux_seek(handle);
		return;
	}

	if (seeking) {
		int seconds, seek_seconds;

		seconds = (attr->gop.hour * 3600) + (attr->gop.minute * 60) +
			attr->gop.second;
		seek_seconds = (seek_stc.hour * 3600) +
			(seek_stc.minute * 60) + seek_stc.second;

		if (seeking > 0) {
			if (seconds >= (seek_seconds - SEEK_FUDGE)) {
				if (seconds > (seek_seconds + SEEK_FUDGE)) {
					n = 0;
					nput = 0;
					lseek(fd,
					      -(seek_bps *
						(seconds-seek_seconds)) / 2,
					      SEEK_CUR);
					demux_flush(handle);
					demux_seek(handle);
					seeking = -1;
					seek_attempts--;
					printf("SEEKING 1: %d/%d 0x%llx\n",
					       seconds, seek_seconds,
					       lseek(fd, 0, SEEK_CUR));
					return;
				}
				seeking = 0;
				printf("SEEK DONE: to %d at %d\n",
				       seek_seconds, seconds);
			} else {
				n = 0;
				nput = 0;
				lseek(fd,
				      (seek_bps*(seek_seconds-seconds)) / 2,
				      SEEK_CUR);
				demux_flush(handle);
				demux_seek(handle);
				seek_attempts--;
				printf("SEEKING 2: %d/%d 0x%llx\n",
				       seconds, seek_seconds,
				       lseek(fd, 0, SEEK_CUR));
				return;
			}
		} else {
			if (seconds > (seek_seconds + SEEK_FUDGE)) {
				n = 0;
				nput = 0;
				lseek(fd,
				      -(seek_bps *
					(seconds-seek_seconds)) / 2,
				      SEEK_CUR);
				demux_flush(handle);
				demux_seek(handle);
				seek_attempts--;
				printf("SEEKING 3: %d/%d 0x%llx\n",
				       seconds, seek_seconds,
				       lseek(fd, 0, SEEK_CUR));
				return;
			} else {
				if (seek_seconds > (seconds + SEEK_FUDGE)) {
					n = 0;
					nput = 0;
					lseek(fd,
					      (seek_bps *
					       (seek_seconds-seconds)) / 2,
					      SEEK_CUR);
					demux_flush(handle);
					demux_seek(handle);
					seeking = 1;
					seek_attempts--;
					printf("SEEKING 4: %d/%d 0x%llx\n",
					       seconds, seek_seconds,
					       lseek(fd, 0, SEEK_CUR));
					return;
				}
				seeking = 0;
				printf("SEEK DONE: to %d at %d\n",
				       seek_seconds, seconds);
			}
		}
	}

	switch (audio_type) {
	    case AUDIO_MODE_MPEG1_PES:
	    case AUDIO_MODE_MPEG2_PES:
	    case AUDIO_MODE_ES:
		    alen = demux_write_audio(handle, fd_audio);
		    break;
	    case AUDIO_MODE_PCM:
		    /*
		     * XXX: this does not work
		     */
		    if (pcm_decoded) {
			    alen = demux_write_audio(handle, fd_audio);
		    } else {
			    unsigned long rate;

			    ac3len = demux_get_audio(handle, ac3buf,
						     sizeof(ac3buf));
			    write(fd, ac3buf, ac3len);
			    rate = 48000;
			    printf("PCM rate %lu\n", rate);
			    av_set_pcm_rate(rate);
			    write(fd_audio, ac3buf, ac3len);
			    pcm_decoded = 1;
		    }
		    break;
	    case AUDIO_MODE_AC3:
		    if (ac3more == 0) {
			    ac3len = demux_get_audio(handle, ac3buf,
						     sizeof(ac3buf));
			    ac3more = 1;
		    }
		    pcm_decoded = 1;
		    break;
	}
	vlen = demux_write_video(handle, fd_video);

	if ((alen == 0) || (vlen == 0)) {
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, video_play, 100);
	}

	gettimeofday(&now, NULL);
	timersub(&now, &start, &delta);
	get_video_sync(&pts);
	if (reset || (first_stc > pts.stc))
		first_stc = pts.stc;
	if (old_pts != pts.stc) {
		PRINTF("HARDWARE: 0x%llx 0x%llx [%d %d] 0x%x\n",
		       pts.stc, first_stc, delta.tv_sec, delta.tv_usec,
		       (unsigned int)(pts.stc /
				      ((float)delta.tv_sec +
				       (delta.tv_usec/1000000.0))));
	}
	old_pts = pts.stc;

#if 0
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
#endif

	if ((alen == 0) && (vlen == 0) && (n == 0) && demux_empty(handle)) {
		if (count++ > 10) {
			close(fd);
			fd = -1;
			if (running_mythtv)
				mythtv_show_widgets();
			else
				video_show_widgets();
		}
	} else
		count = 0;
}

static void
video_idle(void)
{
	int reset = 0;

	if (fd == -1) {
		if ((fd=open(current, O_RDONLY|O_LARGEFILE|O_NDELAY)) < 0)
			return;
		printf("opened %s\n", current);

		av_set_audio_output(AV_AUDIO_MPEG);
		fd_audio = av_audio_fd();

		av_play();

		demux_reset(handle);
		if (si.rows == 480)
			av_move(475, si.rows-60, 4);
		else
			av_move(475, si.rows-113, 4);
		av_play();

#if 0
		spu_stream = -1;
		demux_spu_set_id(handle, spu_stream);
#endif

		reset = 1;
		zoomed = 0;
		display_on = 0;
	}

	video_player(reset);
}

void
video_play(mvp_widget_t *widget)
{
	mvpw_set_idle(video_idle);
	mvpw_set_timer(root, NULL, 0);
}

void
video_clear(void)
{
	fd = -1;
	av_reset();
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
	int jump, delta;
	demux_attr_t *attr;

	switch (key) {
	case '.':
		disable_osd();
		if (si.rows == 480)
			av_move(475, si.rows-60, 4);
		else
			av_move(475, si.rows-113, 4);
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
	case '{':
		seeking = 1;
		av_current_stc(&seek_stc);
		seek_stc.second -= 30;
		if (seek_stc.second >= 60) {
			seek_stc.second = seek_stc.second % 60;
			if (++seek_stc.minute == 60) {
				seek_stc.minute = 0;
				seek_stc.hour++;
			}
		}
		PRINTF("SEEK: %.2d:%.2d:%.2d\n",
		       seek_stc.hour, seek_stc.minute, seek_stc.second);
		attr = demux_get_attr(handle);
		delta = attr->bps * 30;
		lseek(fd, -delta, SEEK_CUR);
		seek_bps = attr->bps;
		seek_attempts = 4;
		demux_flush(handle);
		demux_seek(handle);
		av_reset();
		break;
	case ')':
		seeking = 1;
		av_current_stc(&seek_stc);
		seek_stc.second += 30;
		if (seek_stc.second >= 60) {
			seek_stc.second = seek_stc.second % 60;
			if (++seek_stc.minute == 60) {
				seek_stc.minute = 0;
				seek_stc.hour++;
			}
		}
		printf("SEEK: %.2d:%.2d:%.2d\n",
		       seek_stc.hour, seek_stc.minute, seek_stc.second);
		attr = demux_get_attr(handle);
		delta = attr->bps * 30;
		lseek(fd, delta, SEEK_CUR);
		seek_bps = attr->bps;
		seek_attempts = 4;
		demux_flush(handle);
		demux_seek(handle);
		av_reset();
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
		jump = key - '0';
		fstat64(fd, &sb);
		offset = sb.st_size * (jump / 10.0);
		lseek(fd, offset, SEEK_SET);
		demux_flush(handle);
		demux_seek(handle);
		av_reset();
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
		if (zoomed) {
			mvpw_hide(zoom_widget);
			av_move(0, 0, 0);
		} else {
			mvpw_show(zoom_widget);
			av_move(0, 0, 5);
		}
		zoomed = !zoomed;
		break;
	case 'P':
		power_toggle();
		break;
	case '\n':
		av_move(0, 0, 0);
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

void*
ac3_start(void *arg)
{
	int pcm_decoded = 0;

	pthread_mutex_lock(&mutex);
	printf("ac3 thread sleeping...\n");
	pthread_cond_wait(&cond, &mutex);

	while (1) {
		if (ac3more == -1)
			ac3more = a52_decode_data(NULL, NULL, pcm_decoded);
		if (ac3more == 1)
			ac3more = a52_decode_data(ac3buf, ac3buf + ac3len,
						  pcm_decoded);
		if (ac3more == 0)
			usleep(1000);
		pcm_decoded = 1;
	}

	return NULL;
}
