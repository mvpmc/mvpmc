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
	int alen, vlen;
	demux_attr_t *attr;
	video_info_t *vi;
	static int count = 0, resize = 0;
	pts_sync_data_t pts;
	static uint64_t first_stc = 0, old_pts = 0;
	static struct timeval start;
	struct timeval now, delta;
	spu_item_t *spu;
	static int set_aspect = 1;

	if (reset) {
		n = 0;
		nput = 0;
		resize = 0;
		set_aspect = 1;
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
		av_set_audio_type(audio_type);
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

	alen = demux_write_audio(handle, fd_audio);
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
		av_play();

		demux_reset(handle);
		av_move(475, si.rows-60, 4);
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

void
video_callback(mvp_widget_t *widget, char key)
{
	struct stat64 sb;
	long long offset;
	int jump, delta;
	demux_attr_t *attr;

	switch (key) {
	case '.':
		av_move(475, si.rows-60, 4);
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
#if 0
	case 'M':
		mvpw_show(popup_menu);
		mvpw_focus(popup_menu);
		break;
#endif
	case 'Q':
		if (av_mute() == 1)
			mvpw_show(mute_widget);
		else
			mvpw_hide(mute_widget);
		break;
	case ' ':
		if (display_on) {
			mvpw_set_timer(osd_widget, NULL, 0);
			mvpw_hide(osd_widget);
			mvpw_expose(root);
		} else {
			osd_callback(osd_widget);
			mvpw_set_timer(osd_widget, osd_callback, 1000);
			mvpw_show(osd_widget);
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
	default:
		printf("char '%c'\n", key);
		break;
	}
}
