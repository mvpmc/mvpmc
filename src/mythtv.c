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
#include <string.h>
#include <pthread.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <cmyth.h>

#include "mvpmc.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

#define BSIZE	(512*1024)

static volatile cmyth_file_t file;
extern demux_handle_t *handle;
extern int fd_audio, fd_video;

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static cmyth_conn_t control;
static cmyth_proginfo_t current_prog;
static cmyth_proglist_t episode_plist;
static cmyth_proglist_t pending_plist;
static cmyth_proginfo_t episode_prog;
static cmyth_proginfo_t pending_prog;
static char *pathname = NULL;
static char program_name[256];

volatile int mythtv_level = 0;

static int show_count, episode_count;

int running_mythtv = 0;
int mythtv_debug = 0;

static volatile int playing_via_mythtv = 0, reset_mythtv = 1;
static volatile int close_mythtv = 0;

static pthread_t control_thread;

static void mythtv_play(mvp_widget_t *widget);

static int
string_compare(const void *a, const void *b)
{
	const char *x, *y;

	x = *((const char**)a);
	y = *((const char**)b);

	return strcmp(x, y);
}

void
mythtv_show_widgets(void)
{
	mvpw_show(mythtv_logo);
	mvpw_show(mythtv_browser);
	mvpw_show(mythtv_date);
	mvpw_show(mythtv_description);
	mvpw_show(mythtv_channel);
}

static void
mythtv_close_file(void)
{
	if (playing_via_mythtv) {
		cmyth_file_release(control, file);
		file = NULL;
		reset_mythtv = 1;
	}

	close_mythtv = 0;
}

static void
mythtv_player(void)
{
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
	static int audio_type = 0;

	PRINTF("%s(): line %d\n", __FUNCTION__, __LINE__);

#if 0
	if (reset) {
		n = 0;
		nput = 0;
		resize = 0;
		set_aspect = 1;
		gettimeofday(&start, NULL);
	}
#endif

	attr = demux_get_attr(handle);
	vi = &attr->video.stats.info.video;
	if (attr->audio.type != audio_type) {
		audio_type = attr->audio.type;
		av_set_audio_type(audio_type);
	}
	if (set_aspect) {
		if (vi->aspect == 3) {
			av_set_video_aspect(1);
			PRINTF("video aspect ratio: 16:9\n");
		} else {
			av_set_video_aspect(0);
			PRINTF("video aspect ratio: 4:3\n");
		}
		set_aspect = 0;
	}

#if 0
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
#endif

	PRINTF("%s(): line %d\n", __FUNCTION__, __LINE__);
	alen = demux_write_audio(handle, fd_audio);
	vlen = demux_write_video(handle, fd_video);
	PRINTF("%s(): line %d\n", __FUNCTION__, __LINE__);

	if ((alen == 0) || (vlen == 0)) {
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, mythtv_play, 100);
	}

#if 0
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
#endif

	if ((alen == 0) && (vlen == 0) && (n == 0) && demux_empty(handle)) {
		if (count++ > 10) {
#if 0
			close(fd);
			fd = -1;
			if (running_mythtv)
				mythtv_show_widgets();
			else
				video_show_widgets();
#endif
		}
	} else
		count = 0;
	PRINTF("%s(): line %d\n", __FUNCTION__, __LINE__);
}

static void
mythtv_idle(void)
{
	static char buf[BSIZE];
	static int len, nput = 0, n;
	static int tot = 0, recent = 0;

	if (close_mythtv) {
		nput = 0;
		cmyth_file_get_block(file, buf, BSIZE);
		return;
	}

	if (reset_mythtv) {
		nput = 0;
		reset_mythtv = 0;
	}

	if (nput == 0) {
		len = cmyth_file_get_block(file, buf, BSIZE);
	}

	while (nput < len) {
		if ((n=demux_put(handle, buf+nput, len-nput)) > 0)
			nput += n;
		else
			goto out;
	}

	nput = 0;

	recent += len;
	if (recent < (1024*256))
		return;
	recent = 0;

 out:
	if (tot == BSIZE)
		tot = 0;

	mythtv_player();
}

static void
mythtv_play(mvp_widget_t *widget)
{
	playing_via_mythtv = 1;

	pthread_cond_signal(&cond);

	mvpw_set_idle(mythtv_idle);
	mvpw_set_timer(root, NULL, 0);
}

static void
hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	const char *description, *channame, *title, *subtitle;

	mvpw_hide(shows_widget);
	mvpw_hide(episodes_widget);
	mvpw_hide(freespace_widget);

	if (hilite) {
		cmyth_timestamp_t ts;
		char start[256], end[256], str[256], *ptr;

		mvpw_show(mythtv_channel);
		mvpw_show(mythtv_date);
		mvpw_show(mythtv_description);

		if (current_prog)
			cmyth_proginfo_release(current_prog);

		current_prog = cmyth_proglist_get_item(episode_plist,
						       (int)key);

		channame = cmyth_proginfo_channame(current_prog);
		title = cmyth_proginfo_title(current_prog);
		subtitle = cmyth_proginfo_subtitle(current_prog);
		if (channame) {
			mvpw_set_text_str(mythtv_channel, channame);
		} else {
			printf("program channel name not found!\n");
			mvpw_set_text_str(mythtv_channel, "");
		}
		mvpw_expose(mythtv_channel);

		description = cmyth_proginfo_description(current_prog);
		if (description) {
			mvpw_set_text_str(mythtv_description, description);
		} else {
			printf("program description not found!\n");
			mvpw_set_text_str(mythtv_description, "");
		}
		mvpw_expose(mythtv_description);

		ts = cmyth_proginfo_rec_start(current_prog);
		cmyth_timestamp_to_string(start, ts);
		ts = cmyth_proginfo_rec_end(current_prog);
		cmyth_timestamp_to_string(end, ts);
		
		pathname = cmyth_proginfo_pathname(current_prog);

		if (current)
			free(current);

		if (mythtv_recdir) {
			current = malloc(strlen(mythtv_recdir)+
					 strlen(pathname)+1);
			sprintf(current, "%s%s", mythtv_recdir, pathname);
			mvpw_set_timer(root, video_play, 500);
		} else {
			current = malloc(strlen(pathname)+1);
			sprintf(current, "%s", pathname);

			if ((file=cmyth_conn_connect_file(current_prog,
							  BSIZE)) == NULL) {
				fprintf(stderr, "cannot connect to file\n");
			}

			printf("starting mythtv file transfer\n");
			demux_reset(handle);
			demux_attr_reset(handle);
			if (si.rows == 480)
				av_move(475, si.rows-60, 4);
			else
				av_move(475, si.rows-113, 4);
			av_play();
			mvpw_set_timer(root, mythtv_play, 500);
		}

		ptr = strchr(start, 'T');
		*ptr = '\0';
		sprintf(str, "%s %s - ", start, ptr+1);
		ptr = strchr(end, 'T');
		*ptr = '\0';
		strcat(str, ptr+1);
		mvpw_set_text_str(mythtv_date, str);
		mvpw_expose(mythtv_date);

		snprintf(program_name, sizeof(program_name), "%s - %s",
			 title, subtitle);
	} else {
		mvpw_set_text_str(mythtv_channel, "");
		mvpw_expose(mythtv_channel);
		mvpw_set_text_str(mythtv_date, "");
		mvpw_expose(mythtv_date);
		mvpw_set_text_str(mythtv_description, "");
		mvpw_expose(mythtv_description);
		video_clear();
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);

		if (file) {
			close_mythtv = 1;
			printf("closing myth file, line %d\n", __LINE__);
			while (close_mythtv)
				usleep(1000);
		}
	}
}

static void
show_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	mvpw_hide(mythtv_logo);
	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);
	mvpw_hide(mythtv_record);
	mvpw_hide(shows_widget);
	mvpw_hide(episodes_widget);
	mvpw_hide(freespace_widget);

	mvpw_hide(widget);
	av_move(0, 0, 0);
	mvpw_show(root);
	mvpw_expose(root);
	mvpw_focus(root);

	printf("fullscreen video mode\n");
}

static void
select_callback(mvp_widget_t *widget, char *item, void *key)
{
	const char *title, *subtitle;
	int err, count, i, n = 0, episodes = 0;
	char buf[256];

	mythtv_level = 1;

	item_attr.select = show_select_callback;
	item_attr.hilite = hilite_callback;

	mvpw_clear_menu(widget);

	if (episode_plist)
		cmyth_proglist_release(episode_plist);

	if ((episode_plist=cmyth_proglist_create()) == NULL) {
		fprintf(stderr, "cannot get program list\n");
		return;
	}

	if ((err=cmyth_proglist_get_all_recorded(control, episode_plist)) < 0) {
		fprintf(stderr, "get recorded failed, err %d\n", err);
		return;
	}

	count = cmyth_proglist_get_count(episode_plist);
	for (i = 0; i < count; ++i) {
		char full[256];

		if (episode_prog)
			cmyth_proginfo_release(episode_prog);

		if (strcmp(item, "All - Newest first") == 0)
			episode_prog = cmyth_proglist_get_item(episode_plist,
							       count-i-1);
		else
			episode_prog = cmyth_proglist_get_item(episode_plist,
							       i);

		title = cmyth_proginfo_title(episode_prog);
		subtitle = cmyth_proginfo_subtitle(episode_prog);

		if (strcmp(title, item) == 0) {
			if (strcmp(subtitle, " ") == 0)
				subtitle = "<no subtitle>";
			mvpw_add_menu_item(widget, subtitle, (void*)n,
					   &item_attr);
			episodes++;
		} else if (strcmp(item, "All - Oldest first") == 0) {
			snprintf(full, sizeof(full), "%s - %s",
				 title, subtitle);
			mvpw_add_menu_item(widget, full, (void*)n,
					   &item_attr);
			episodes++;
		} else if (strcmp(item, "All - Newest first") == 0) {
			snprintf(full, sizeof(full), "%s - %s",
				 title, subtitle);
			mvpw_add_menu_item(widget, full, (void*)count-n-1,
					   &item_attr);
			episodes++;
		}
		n++;
	}

	snprintf(buf, sizeof(buf), "%s - %d episodes", item, episodes);
	mvpw_set_menu_title(widget, buf);
}

static void
add_shows(mvp_widget_t *widget)
{
	cmyth_proglist_t plist;
	cmyth_proginfo_t prog;
	int err, count;
	int i, j, n = 0;
	const char *title;
	char *titles[1024];

	item_attr.select = select_callback;
	item_attr.hilite = NULL;

	if ((plist=cmyth_proglist_create()) == NULL) {
		fprintf(stderr, "cannot get program list\n");
		return;
	}

	if ((err=cmyth_proglist_get_all_recorded(control, plist)) < 0) {
		fprintf(stderr, "get recorded failed, err %d\n", err);
		goto out;
	}

	count = cmyth_proglist_get_count(plist);
	for (i = 0; i < count; ++i) {
		prog = cmyth_proglist_get_item(plist, i);
		title = cmyth_proginfo_title(prog);
		for (j=0; j<n; j++)
			if (strcmp(title, titles[j]) == 0)
				break;
		if (j == n) {
			titles[n] = title;
			n++;
		}
		cmyth_proginfo_release(prog);
	}

	episode_count = count;
	show_count = n;

	qsort(titles, n, sizeof(char*), string_compare);

	mvpw_add_menu_item(widget, "All - Newest first", (void*)0, &item_attr);
	mvpw_add_menu_item(widget, "All - Oldest first", (void*)1, &item_attr);

	for (i=0; i<n; i++)
		mvpw_add_menu_item(widget, titles[i], (void*)n+2, &item_attr);

 out:
	cmyth_proglist_release(plist);
}

int
mythtv_update(mvp_widget_t *widget)
{
	char buf[64];
	unsigned int total, used;

	running_mythtv = 1;

	mvpw_show(root);
	mvpw_expose(root);

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);

	if (control == NULL)
		mythtv_init(mythtv_server, -1);

	add_osd_widget(mythtv_program_widget, OSD_PROGRAM, 1, NULL);

	mvpw_set_menu_title(widget, "MythTV");
	mvpw_clear_menu(widget);
	add_shows(widget);

	snprintf(buf, sizeof(buf), "Total shows: %d", show_count);
	mvpw_set_text_str(shows_widget, buf);
	snprintf(buf, sizeof(buf), "Total episodes: %d", episode_count);
	mvpw_set_text_str(episodes_widget, buf);

	if (cmyth_conn_get_freespace(control, &total, &used) == 0) {
		snprintf(buf, sizeof(buf), "Diskspace: %d/%d  %5.2f%%",
			 used, total, ((float)used/total)*100.0);
		mvpw_set_text_str(freespace_widget, buf);
	}

	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);

	mvpw_show(shows_widget);
	mvpw_show(episodes_widget);
	mvpw_show(freespace_widget);

	return 0;
}

int
mythtv_back(mvp_widget_t *widget)
{
	mvpw_set_text_str(mythtv_channel, "");
	mvpw_set_text_str(mythtv_date, "");
	mvpw_set_text_str(mythtv_description, "");

	mvpw_expose(mythtv_channel);
	mvpw_expose(mythtv_date);
	mvpw_expose(mythtv_description);

	mvpw_show(shows_widget);
	mvpw_show(episodes_widget);
	mvpw_show(freespace_widget);

	if (file) {
		close_mythtv = 1;
		printf("closing myth file, line %d\n", __LINE__);
		while (close_mythtv)
			usleep(1000);
	}

	if (mythtv_level == 0) {
		running_mythtv = 0;
		return 0;
	}

	mythtv_level = 0;
	mythtv_update(widget);

	return -1;
}

static void
pending_hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	int n = (int)key;

	if (hilite) {
		char start[256], end[256], str[256];
		char *description, *channame, *ptr;
		cmyth_timestamp_t ts;
		cmyth_proginfo_rec_status_t status;

		if (pending_prog)
			cmyth_proginfo_release(pending_prog);

		pending_prog = cmyth_proglist_get_item(pending_plist, n);

		status = cmyth_proginfo_rec_status(pending_prog);
		description = cmyth_proginfo_description(pending_prog);
		channame = cmyth_proginfo_channame(pending_prog);
		ts = cmyth_proginfo_rec_start(pending_prog);
		cmyth_timestamp_to_string(start, ts);
		ts = cmyth_proginfo_rec_end(pending_prog);
		cmyth_timestamp_to_string(end, ts);

		ptr = strchr(start, 'T');
		*ptr = '\0';
		sprintf(str, "%s %s - ", start, ptr+1);
		ptr = strchr(end, 'T');
		*ptr = '\0';
		strcat(str, ptr+1);

		switch (status) {
		case RS_RECORDING:
			ptr = "Recording";
			break;
		case RS_WILL_RECORD:
			ptr = "Will Record";
			break;
		case RS_CONFLICT:
			ptr = "Conflict";
			break;
		case RS_DONT_RECORD:
			ptr = "Don't Record";
			break;
		case RS_TOO_MANY_RECORDINGS:
			ptr = "Too Many Recordings";
			break;
		case RS_PREVIOUS_RECORDING:
			ptr = "Previous Recording";
			break;
		default:
			ptr = "";
			break;
		}

		mvpw_set_text_str(mythtv_description, description);
		mvpw_set_text_str(mythtv_channel, channame);
		mvpw_set_text_str(mythtv_date, str);
		mvpw_set_text_str(mythtv_record, ptr);

		mvpw_expose(mythtv_description);
		mvpw_expose(mythtv_channel);
		mvpw_expose(mythtv_date);
		mvpw_expose(mythtv_record);
	}
}

int
mythtv_pending(mvp_widget_t *widget)
{
	int err, i, count;
	char *title, *subtitle;

	mvpw_set_text_str(mythtv_channel, "");
	mvpw_set_text_str(mythtv_date, "");
	mvpw_set_text_str(mythtv_description, "");

	mvpw_show(mythtv_channel);
	mvpw_show(mythtv_date);
	mvpw_show(mythtv_description);
	mvpw_show(mythtv_record);

	mvpw_set_menu_title(widget, "Recording Schedule");
	mvpw_clear_menu(widget);

	if (pending_plist)
		cmyth_proglist_release(pending_plist);

	if ((pending_plist=cmyth_proglist_create()) == NULL) {
		fprintf(stderr, "cannot get program list\n");
		return -1;
	}

	if ((err=cmyth_proglist_get_all_pending(control, pending_plist)) < 0) {
		fprintf(stderr, "get pending failed, err %d\n", err);
		return -1;
	}

	item_attr.select = NULL;
	item_attr.hilite = pending_hilite_callback;

	item_attr.bg = MVPW_BLACK;

	count = cmyth_proglist_get_count(pending_plist);
	printf("found %d pending recordings\n", count);
	for (i = 0; i < count; ++i) {
		cmyth_timestamp_t ts;
		cmyth_proginfo_rec_status_t status;
		int month, day, hour, minute;
		char type;
		char start[256];
		char buf[256];
		char *ptr;

		/*
		 * XXX: probably should not display pending recordings which
		 *      are in the past
		 */

		if (pending_prog)
			cmyth_proginfo_release(pending_prog);

		pending_prog = cmyth_proglist_get_item(pending_plist, i);
		title = cmyth_proginfo_title(pending_prog);
		subtitle = cmyth_proginfo_subtitle(pending_prog);

		ts = cmyth_proginfo_rec_start(pending_prog);
		cmyth_timestamp_to_string(start, ts);

		status = cmyth_proginfo_rec_status(pending_prog);

		switch (status) {
		case RS_RECORDING:
			item_attr.fg = MVPW_ORANGE;
			type = '1';
			break;
		case RS_WILL_RECORD:
			item_attr.fg = MVPW_GREEN;
			type = '1';
			break;
		case RS_CONFLICT:
			item_attr.fg = MVPW_YELLOW;
			type = 'C';
			break;
		case RS_DONT_RECORD:
			item_attr.fg = MVPW_LIGHTGREY;
			type = 'X';
			break;
		case RS_TOO_MANY_RECORDINGS:
			item_attr.fg = MVPW_LIGHTGREY;
			type = 'T';
			break;
		case RS_PREVIOUS_RECORDING:
			item_attr.fg = MVPW_LIGHTGREY;
			type = 'P';
			break;
		default:
			item_attr.fg = MVPW_LIGHTGREY;
			type = '?';
			break;
		}

		ptr = strchr(start, '-');
		month = atoi(++ptr);
		ptr = strchr(ptr, '-');
		day = atoi(++ptr);
		ptr = strchr(ptr, 'T');
		hour = atoi(++ptr);
		ptr = strchr(ptr, ':');
		minute = atoi(++ptr);

		snprintf(buf, sizeof(buf),
			 "%.2d/%.2d  %.2d:%.2d   %c   %s  -  %s",
			 month, day, hour, minute, type, title, subtitle);

		mvpw_add_menu_item(widget, buf, (void*)i, &item_attr);
	}

	item_attr.fg = MVPW_BLACK;
	item_attr.bg = MVPW_LIGHTGREY;

	return 0;
}

static void*
control_start(void *arg)
{
	int len;

	while (1) {
		pthread_mutex_lock(&mutex);
		printf("mythtv control thread sleeping...\n");
		pthread_cond_wait(&cond, &mutex);
		pthread_mutex_unlock(&mutex);

		printf("mythtv control thread starting...\n");

		do {
			len = cmyth_file_request_block(control, file, BSIZE);
		} while ((len > 0) && (playing_via_mythtv == 1) && (!close_mythtv));

		mythtv_close_file();
	}

	return NULL;
}

int
mythtv_init(char *server_name, int portnum)
{
	char *server = "127.0.0.1";
	int port = 6543;

	if (mythtv_debug)
		cmyth_dbg_all();

	if (server_name)
		server = server_name;
	if (portnum > 0)
		port = portnum;

	if ((control=cmyth_conn_connect_ctrl(server, port, 16*1024)) == NULL) {
		fprintf(stderr, "cannot conntect to mythtv server %s\n",
			server);
		return -1;
	}

	pthread_create(&control_thread, NULL, control_start, NULL);

	return 0;
}

void
mythtv_program(mvp_widget_t *widget)
{
	char *description;

	description = mvpw_get_text_str(mythtv_description);

	mvpw_set_text_str(mythtv_osd_description, description);
	mvpw_expose(mythtv_osd_description);

	mvpw_set_text_str(mythtv_osd_program, program_name);
	mvpw_expose(mythtv_osd_program);
}

int
mythtv_delete(void)
{
	int ret;

	ret = cmyth_proginfo_delete_recording(control, current_prog);

	return ret;
}

int
mythtv_forget(void)
{
	int ret;

	ret = cmyth_proginfo_forget_recording(control, current_prog);

	return ret;
}

int
mythtv_proginfo(char *buf, int size)
{
	cmyth_timestamp_t ts;
	char airdate[256], start[256], end[256];
	char *ptr;

	ts = cmyth_proginfo_originalairdate(current_prog);
	cmyth_timestamp_to_string(airdate, ts);
	ts = cmyth_proginfo_rec_start(current_prog);
	cmyth_timestamp_to_string(start, ts);
	ts = cmyth_proginfo_rec_end(current_prog);
	cmyth_timestamp_to_string(end, ts);

	if ((ptr=strchr(airdate, 'T')) != NULL)
		*ptr = '\0';
	if ((ptr=strchr(start, 'T')) != NULL)
		*ptr = ' ';
	if ((ptr=strchr(end, 'T')) != NULL)
		*ptr = ' ';

	snprintf(buf, size,
		 "Title: %s\n"
		 "Subtitle: %s\n"
		 "Description: %s\n"
		 "Start: %s\n"
		 "End: %s\n"
		 "Category: %s\n"
		 "Channel: %s\n"
		 "Series ID: %s\n"
		 "Program ID: %s\n"
		 "Stars: %s\n",
		 cmyth_proginfo_title(current_prog),
		 cmyth_proginfo_subtitle(current_prog),
		 cmyth_proginfo_description(current_prog),
		 start,
		 end,
		 cmyth_proginfo_category(current_prog),
		 cmyth_proginfo_channame(current_prog),
		 cmyth_proginfo_seriesid(current_prog),
		 cmyth_proginfo_programid(current_prog),
		 cmyth_proginfo_stars(current_prog));

	return 0;
}

void
mythtv_cleanup(void)
{
	printf("cleanup mythtv data structures\n");

	if (pending_plist) {
		cmyth_proglist_release(pending_plist);
		pending_plist = NULL;
	}
	if (episode_plist) {
		cmyth_proglist_release(episode_plist);
		episode_plist = NULL;
	}
}
