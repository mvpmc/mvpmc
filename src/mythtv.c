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
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

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

extern mvpw_menu_attr_t mythtv_attr;

#define BSIZE	(256*1024)

static volatile cmyth_file_t file;
extern demux_handle_t *handle;
extern int fd_audio, fd_video;

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_WHITE,
	.bg = MVPW_BLACK,
	.checkbox_fg = MVPW_GREEN,
};

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t seek_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t myth_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile cmyth_conn_t control;		/* master backend */
static volatile cmyth_conn_t control_slave;	/* slave backend */
static volatile cmyth_proginfo_t current_prog;	/* program currently being played */
static cmyth_proginfo_t hilite_prog;	/* program currently hilighted */
static cmyth_proglist_t episode_plist;
static cmyth_proglist_t pending_plist;
static cmyth_proginfo_t episode_prog;
static cmyth_proginfo_t pending_prog;
static char *pathname = NULL;
static volatile cmyth_recorder_t recorder;

static volatile int current_livetv;

volatile mythtv_state_t mythtv_state = MYTHTV_STATE_MAIN;

static int show_count, episode_count;
static volatile int list_all = 0;

int running_mythtv = 0;
int mythtv_main_menu = 0;
int mythtv_debug = 0;

static volatile int playing_via_mythtv = 0, reset_mythtv = 1;
static volatile int close_mythtv = 0;
static volatile int changing_channel = 0;
static volatile int video_reading = 0;

static pthread_t control_thread, wd_thread;

#define MAX_TUNER	16
struct livetv_proginfo {
	int rec_id;
	char *chan;
	char *channame;
};
struct livetv_prog {
	char *title;
	char *subtitle;
	char *description;
	char *start;
	char *end;
	int count;
	struct livetv_proginfo pi[MAX_TUNER];
};
static struct livetv_prog *livetv_list = NULL;
static int livetv_count = 0;

static int get_livetv_programs(void);

static int mythtv_open(void);
static int mythtv_read(char*, int);
static long long mythtv_seek(long long, int);
static long long mythtv_size(void);
static int livetv_open(void);
static long long livetv_size(void);
static void livetv_select_callback(mvp_widget_t*, char*, void*);

static volatile int myth_seeking = 0;

static char *hilite_path = NULL;
char *mythtv_recdir_tosplit = NULL;
char *recdir_token = NULL;
char *test_path = NULL;
FILE *test_file;

static video_callback_t mythtv_functions = {
	.open      = mythtv_open,
	.read      = mythtv_read,
	.read_dynb = NULL,
	.seek      = mythtv_seek,
	.size      = mythtv_size,
	.notify    = NULL,
	.key       = NULL,
};

static video_callback_t livetv_functions = {
	.open      = livetv_open,
	.read      = mythtv_read,
	.read_dynb = NULL,
	.seek      = mythtv_seek,
	.size      = livetv_size,
	.notify    = NULL,
	.key       = NULL,
};

mythtv_color_t mythtv_colors = {
	.livetv_current		= MVPW_GREEN,
	.pending_recording	= 0xff4fa5ff,
	.pending_will_record	= MVPW_GREEN,
	.pending_conflict	= MVPW_YELLOW,
	.pending_other		= MVPW_LIGHTGREY,
};

static int
string_compare(const void *a, const void *b)
{
	const char *x, *y;

	x = *((const char**)a);
	y = *((const char**)b);

	return strcmp(x, y);
}

static int
livetv_compare(const void *a, const void *b)
{
	const struct livetv_prog *x, *y;
	int X, Y;

	x = ((const struct livetv_prog*)a);
	y = ((const struct livetv_prog*)b);
	X = atoi(x->pi[0].chan);
	Y = atoi(y->pi[0].chan);

	if (X < Y)
		return -1;
	else if (X > Y)
		return 1;
	else
		return 0;
}

void
mythtv_show_widgets(void)
{
	if (mythtv_state == MYTHTV_STATE_PENDING) {
		mvpw_show(mythtv_browser);
		mvpw_show(mythtv_channel);
		mvpw_show(mythtv_date);
		mvpw_show(mythtv_description);
		mvpw_show(mythtv_record);
		mvpw_show(mythtv_logo);
	} else if (mythtv_state == MYTHTV_STATE_PROGRAMS) {
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_show(shows_widget);
		mvpw_show(episodes_widget);
		mvpw_show(freespace_widget);
	} else {
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_show(mythtv_date);
		mvpw_show(mythtv_description);
		mvpw_show(mythtv_channel);
	}
}

static void
mythtv_fullscreen(void)
{
	mvpw_hide(mythtv_browser);
	mvpw_hide(mythtv_logo);
	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);
	mvpw_hide(mythtv_record);
	mvpw_hide(shows_widget);
	mvpw_hide(episodes_widget);
	mvpw_hide(freespace_widget);

	av_move(0, 0, 0);
	mvpw_show(root);
	mvpw_expose(root);
	mvpw_focus(root);

	printf("fullscreen video mode\n");
}

static int
mythtv_verify(void)
{
	char buf[128];

	if (control == NULL) {
		if (mythtv_init(mythtv_server, -1) < 0) {
			snprintf(buf, sizeof(buf),
				 "Connect to mythtv server %s failed!",
				 mythtv_server ? mythtv_server : "127.0.0.1");
			gui_error(buf);
			return -1;
		}
	}

	return 0;
}

static void
mythtv_close(void)
{
	if (control_slave && file) {
		cmyth_file_t f = file;
		file = NULL;
		cmyth_file_release(control_slave, f);
	}
	if (pending_prog) {
		cmyth_proginfo_release(pending_prog);
		pending_prog = NULL;
	}
	if (current_prog) {
		cmyth_proginfo_release(current_prog);
		current_prog = NULL;
	}
	if (hilite_prog) {
		cmyth_proginfo_release(hilite_prog);
		hilite_prog = NULL;
	}
	if (episode_prog) {
		cmyth_proginfo_release(episode_prog);
		episode_prog = NULL;
	}
	if (episode_plist) {
		cmyth_proglist_release(episode_plist);
		episode_plist = NULL;
	}
	if (pending_plist) {
		cmyth_proglist_release(pending_plist);
		pending_plist = NULL;
	}
	if (control) {
		cmyth_conn_release(control);
		control = NULL;
	}
	if (control_slave) {
		cmyth_conn_t c = control_slave;
		control_slave = NULL;
		cmyth_conn_release(c);
	}
}

static void
mythtv_shutdown(void)
{
	printf("%s(): closing mythtv connection\n", __FUNCTION__);

	mythtv_close();

	mvpw_set_bg(root, MVPW_BLACK);

	mvpw_hide(mythtv_browser);
	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);
	mvpw_hide(mythtv_record);
	mvpw_hide(shows_widget);
	mvpw_hide(episodes_widget);
	mvpw_hide(freespace_widget);

	mvpw_show(mythtv_logo);
	mvpw_show(mythtv_menu);
	mvpw_focus(mythtv_menu);

	mythtv_main_menu = 1;
	reset_mythtv = 1;
	close_mythtv = 0;
	mythtv_state = MYTHTV_STATE_MAIN;

	gui_error("MythTV connection lost!");
}

static void
mythtv_close_file(void)
{
	printf("%s(): closing file\n", __FUNCTION__);
	if (playing_via_mythtv && file) {
		if (file) {
			cmyth_conn_t c = control_slave;
			cmyth_file_t f = file;
			control_slave = NULL;
			file = NULL;
			printf("%s(): releasing file\n", __FUNCTION__);
			cmyth_file_release(c, f);
			cmyth_conn_release(c);
		}
		if (current_prog) {
			cmyth_proginfo_release(current_prog);
			current_prog = NULL;
		}
		reset_mythtv = 1;
	}

	close_mythtv = 0;

	pthread_kill(control_thread, SIGURG);
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

		if (hilite_prog)
			cmyth_proginfo_release(hilite_prog);

		hilite_prog = cmyth_proglist_get_item(episode_plist, (int)key);

		channame = cmyth_proginfo_channame(hilite_prog);
		title = cmyth_proginfo_title(hilite_prog);
		subtitle = cmyth_proginfo_subtitle(hilite_prog);
		if (channame) {
			mvpw_set_text_str(mythtv_channel, channame);
		} else {
			printf("program channel name not found!\n");
			mvpw_set_text_str(mythtv_channel, "");
		}
		mvpw_expose(mythtv_channel);

		description = cmyth_proginfo_description(hilite_prog);
		if (description) {
			mvpw_set_text_str(mythtv_description, description);
		} else {
			printf("program description not found!\n");
			mvpw_set_text_str(mythtv_description, "");
		}
		mvpw_expose(mythtv_description);

		ts = cmyth_proginfo_rec_start(hilite_prog);
		cmyth_timestamp_to_string(start, ts);
		cmyth_timestamp_release(ts);
		ts = cmyth_proginfo_rec_end(hilite_prog);
		cmyth_timestamp_to_string(end, ts);
		cmyth_timestamp_release(ts);
		
		pathname = cmyth_proginfo_pathname(hilite_prog);

		if (hilite_path)
			free(hilite_path);

		if (mythtv_recdir) {
			mythtv_recdir_tosplit = strdup(mythtv_recdir);
			recdir_token = strtok(mythtv_recdir_tosplit,":");
			while (recdir_token != NULL)
			{
				if (test_path) free(test_path);
				test_path = malloc(strlen(recdir_token)+strlen(pathname)+1);
				sprintf(test_path,"%s%s",recdir_token, pathname);
				if ((test_file=fopen(test_path,"r")) != NULL)
				{
					hilite_path = strdup(test_path);
					fclose(test_file);
				}
				recdir_token = strtok(NULL,":");
			}
		} else {
			hilite_path = malloc(strlen(pathname)+1);
			sprintf(hilite_path, "%s", pathname);
		}

		ptr = strchr(start, 'T');
		*ptr = '\0';
		sprintf(str, "%s %s - ", start, ptr+1);
		ptr = strchr(end, 'T');
		*ptr = '\0';
		strcat(str, ptr+1);
		mvpw_set_text_str(mythtv_date, str);
		mvpw_expose(mythtv_date);
	} else {
		mvpw_set_text_str(mythtv_channel, "");
		mvpw_expose(mythtv_channel);
		mvpw_set_text_str(mythtv_date, "");
		mvpw_expose(mythtv_date);
		mvpw_set_text_str(mythtv_description, "");
		mvpw_expose(mythtv_description);
	}
}

static void
show_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	mythtv_fullscreen();

	if (cmyth_proginfo_compare(hilite_prog, current_prog) != 0) {
		if (current)
			free(current);

		current = malloc(strlen(hilite_path) + 1);
		strcpy(current, hilite_path);

		if (mythtv_livetv) {
			mythtv_livetv_stop();
			running_mythtv = 1;
			if (mythtv_recdir)
				video_functions = &file_functions;
			else
				video_functions = &mythtv_functions;
		} else {
			pthread_mutex_lock(&myth_mutex);
			mythtv_close_file();
			pthread_mutex_unlock(&myth_mutex);
		}

		while (video_reading)
			;

		if (current_prog)
			cmyth_proginfo_release(current_prog);
		current_prog = cmyth_proginfo_hold(hilite_prog);

		demux_reset(handle);
		demux_attr_reset(handle);
		av_move(0, 0, 0);
		av_play();
		video_play(root);
	}
}

void
mythtv_start_thumbnail(void)
{
	if (mythtv_state == MYTHTV_STATE_LIVETV) {
		printf("trying to start livetv thumbnail...\n");
		livetv_select_callback(NULL, NULL, (void*)current_livetv);

		if (si.rows == 480)
			av_move(475, si.rows-60, 4);
		else
			av_move(475, si.rows-113, 4);

		printf("thumbnail video mode\n");

		return;
	}

	if (cmyth_proginfo_compare(hilite_prog, current_prog) != 0) {
		if (si.rows == 480)
			av_move(475, si.rows-60, 4);
		else
			av_move(475, si.rows-113, 4);

		printf("thumbnail video mode\n");

		pthread_mutex_lock(&myth_mutex);
		mythtv_close_file();
		pthread_mutex_unlock(&myth_mutex);

		while (video_reading)
			;

		if (current_prog)
			cmyth_proginfo_release(current_prog);

		current_prog = cmyth_proginfo_hold(hilite_prog);

		if (current)
			free(current);

		current = malloc(strlen(hilite_path) + 1);
		strcpy(current, hilite_path);

		demux_reset(handle);
		demux_seek(handle);
		demux_attr_reset(handle);
		av_reset();
		av_play();
		video_play(root);
	}
}

static int
load_episodes(void)
{
	int err;

	if (episode_plist)
		cmyth_proglist_release(episode_plist);

	if ((episode_plist=cmyth_proglist_create()) == NULL) {
		fprintf(stderr, "cannot get program list\n");
		goto err;
	}

	if ((err=cmyth_proglist_get_all_recorded(control, episode_plist)) < 0) {
		fprintf(stderr, "get recorded failed, err %d\n", err);
		mythtv_shutdown();
		goto err;
	}

	return cmyth_proglist_get_count(episode_plist);

 err:
	return 0;
}

static int
episode_exists(char *title)
{
	int i, count;
	cmyth_proginfo_t prog;
	const char *t;

	if ((episode_plist == NULL) || (title == NULL))
		return 0;

	count = cmyth_proglist_get_count(episode_plist);

	for (i = 0; i < count; ++i) {
		prog = cmyth_proglist_get_item(episode_plist, i);
		t = cmyth_proginfo_title(prog);
		cmyth_proginfo_release(prog);
		if (strcmp(title, t) == 0) {
			return 1;
		}
	}

	return 0;
}

static void
add_episodes(mvp_widget_t *widget, char *item, int load)
{
	const char *title, *subtitle;
	int count, i, n = 0, episodes = 0;
	char buf[256];
	char *prog;

	busy_start();

	pthread_mutex_lock(&myth_mutex);

	mythtv_state = MYTHTV_STATE_EPISODES;

	item_attr.select = show_select_callback;
	item_attr.hilite = hilite_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	prog = strdup(item);
	mvpw_clear_menu(widget);

	if (load)
		count = load_episodes();
	else
		count = cmyth_proglist_get_count(episode_plist);

	for (i = 0; i < count; ++i) {
		char full[256];

		if (episode_prog)
			cmyth_proginfo_release(episode_prog);

		if (strcmp(prog, "All - Newest first") == 0)
			episode_prog = cmyth_proglist_get_item(episode_plist,
							       count-i-1);
		else
			episode_prog = cmyth_proglist_get_item(episode_plist,
							       i);

		title = cmyth_proginfo_title(episode_prog);
		subtitle = cmyth_proginfo_subtitle(episode_prog);

		if (strcmp(title, prog) == 0) {
			list_all = 0;
			if ((strcmp(subtitle, " ") == 0) ||
			    (subtitle[0] == '\0'))
				subtitle = "<no subtitle>";
			mvpw_add_menu_item(widget, subtitle, (void*)n,
					   &item_attr);
			episodes++;
		} else if (strcmp(prog, "All - Oldest first") == 0) {
			list_all = 1;
			snprintf(full, sizeof(full), "%s - %s",
				 title, subtitle);
			mvpw_add_menu_item(widget, full, (void*)n,
					   &item_attr);
			episodes++;
		} else if (strcmp(prog, "All - Newest first") == 0) {
			list_all = 2;
			snprintf(full, sizeof(full), "%s - %s",
				 title, subtitle);
			mvpw_add_menu_item(widget, full, (void*)count-n-1,
					   &item_attr);
			episodes++;
		}
		n++;
	}

	snprintf(buf, sizeof(buf), "%s - %d episode", prog, episodes);
	if (episodes > 1)
		strcat(buf, "s");
	mvpw_set_menu_title(widget, buf);
	free(prog);

	pthread_mutex_unlock(&myth_mutex);

	busy_end();
}

static void
select_callback(mvp_widget_t *widget, char *item, void *key)
{
	add_episodes(widget, item, 1);
}

static void
add_shows(mvp_widget_t *widget)
{
	cmyth_proginfo_t prog;
	int count;
	int i, j, n = 0;
	const char *title;
	char *titles[1024];

	item_attr.select = select_callback;
	item_attr.hilite = NULL;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	pthread_mutex_lock(&myth_mutex);

	count = load_episodes();
	for (i = 0; i < count; ++i) {
		prog = cmyth_proglist_get_item(episode_plist, i);
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

	cmyth_proglist_release(episode_plist);
	episode_plist = NULL;
	pthread_mutex_unlock(&myth_mutex);
}

int
mythtv_update(mvp_widget_t *widget)
{
	char buf[64];
	long long total, used;

	if (mythtv_livetv == 0) {
		if (mythtv_recdir)
			video_functions = &file_functions;
		else
			video_functions = &mythtv_functions;
	}

	running_mythtv = 1;

	mvpw_show(root);
	mvpw_expose(root);

	if (mythtv_verify() < 0)
		return -1;

	if (mythtv_state == MYTHTV_STATE_EPISODES) {
		const char *t;
		char *title = NULL;
		int count;

		if ((t=cmyth_proginfo_title(hilite_prog)) != NULL) {
			title = alloca(strlen(t)+1);
			strcpy(title, t);
		}
		if (!list_all)
			printf("checking for more episodes of '%s'\n", title);
		count = load_episodes();
		if ((count > 0) && (list_all || episode_exists(title))) {
			printf("staying in episode menu\n");
			switch (list_all) {
			case 0:
				add_episodes(widget, title, 0);
				break;
			case 1:
				add_episodes(widget, "All - Oldest first", 0);
				break;
			case 2:
				add_episodes(widget, "All - Newest first", 0);
				break;
			}
			return 0;
		}
		printf("returning to program menu\n");
		mythtv_state = MYTHTV_STATE_PROGRAMS;
	}

	list_all = 0;

	add_osd_widget(mythtv_program_widget, OSD_PROGRAM,
		       osd_settings.program, NULL);

	mvpw_set_menu_title(widget, "MythTV");
	mvpw_clear_menu(widget);
	busy_start();
	add_shows(widget);
	busy_end();

	snprintf(buf, sizeof(buf), "Total shows: %d", show_count);
	mvpw_set_text_str(shows_widget, buf);
	snprintf(buf, sizeof(buf), "Total episodes: %d", episode_count);
	mvpw_set_text_str(episodes_widget, buf);

	pthread_mutex_lock(&myth_mutex);

	if (cmyth_conn_get_freespace(control, &total, &used) == 0) {
		snprintf(buf, sizeof(buf),
			 "Diskspace: %5.2f GB (%5.2f%%) free",
			 (total-used)/1024.0,
			 100.0-((float)used/total)*100.0);
		mvpw_set_text_str(freespace_widget, buf);
	}

	pthread_mutex_unlock(&myth_mutex);

	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);

	if (mythtv_state != MYTHTV_STATE_MAIN) {
		mvpw_show(shows_widget);
		mvpw_show(episodes_widget);
		mvpw_show(freespace_widget);
	}

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

	if ((mythtv_state == MYTHTV_STATE_PROGRAMS) ||
	    (mythtv_state == MYTHTV_STATE_PENDING)) {
		if (pending_plist) {
			cmyth_proglist_release(pending_plist);
			pending_plist = NULL;
		}
		return 0;
	}

	mythtv_state = MYTHTV_STATE_PROGRAMS;
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
		cmyth_timestamp_release(ts);
		ts = cmyth_proginfo_rec_end(pending_prog);
		cmyth_timestamp_to_string(end, ts);
		cmyth_timestamp_release(ts);

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
		case RS_LATER_SHOWING:
			ptr = "Later Showing";
			break;
		case RS_EARLIER_RECORDING:
			ptr = "Earlier Recording";
			break;
		case RS_REPEAT:
			ptr = "Repeat";
			break;
		case RS_CURRENT_RECORDING:
			ptr = "Current Recording";
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
	int err, i, count, ret = 0;
	char *title, *subtitle;
	time_t t, rec_t;
	struct tm *tm, rec_tm;

	if (mythtv_verify() < 0)
		return -1;

	pthread_mutex_lock(&myth_mutex);

	t = time(NULL);
	tm = localtime(&t);

	mvpw_set_text_str(mythtv_channel, "");
	mvpw_set_text_str(mythtv_date, "");
	mvpw_set_text_str(mythtv_description, "");
	mvpw_set_text_str(mythtv_record, "");

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
		ret = -1;
		goto out;
	}

	if ((err=cmyth_proglist_get_all_pending(control, pending_plist)) < 0) {
		fprintf(stderr, "get pending failed, err %d\n", err);
		mythtv_shutdown();
		ret = -1;
		goto out;
	}

	item_attr.select = NULL;
	item_attr.hilite = pending_hilite_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	count = cmyth_proglist_get_count(pending_plist);
	printf("found %d pending recordings\n", count);
	for (i = 0; i < count; ++i) {
		cmyth_timestamp_t ts, te;
		cmyth_proginfo_rec_status_t status;
		int year, month, day, hour, minute;
		char type;
		char start[256], end[256];
		char buf[256];
		char *ptr;

		if (pending_prog)
			cmyth_proginfo_release(pending_prog);

		pending_prog = cmyth_proglist_get_item(pending_plist, i);
		title = cmyth_proginfo_title(pending_prog);
		subtitle = cmyth_proginfo_subtitle(pending_prog);

		ts = cmyth_proginfo_rec_start(pending_prog);
		cmyth_timestamp_to_string(start, ts);

		te = cmyth_proginfo_rec_end(pending_prog);
		cmyth_timestamp_to_string(end, te);

		cmyth_timestamp_release(ts);
		cmyth_timestamp_release(te);

		status = cmyth_proginfo_rec_status(pending_prog);

		year = atoi(end);
		ptr = strchr(end, '-');
		month = atoi(++ptr);
		ptr = strchr(ptr, '-');
		day = atoi(++ptr);
		ptr = strchr(ptr, 'T');
		hour = atoi(++ptr);
		ptr = strchr(ptr, ':');
		minute = atoi(++ptr);

		rec_tm.tm_year = year - 1900;
		rec_tm.tm_mon = month - 1;
		rec_tm.tm_mday = day;
		rec_tm.tm_hour = hour;
		rec_tm.tm_min = minute;
		rec_tm.tm_sec = 0;
		rec_tm.tm_wday = 0;
		rec_tm.tm_yday = 0;
		rec_tm.tm_isdst = -1;

		rec_t = mktime(&rec_tm);

		/*
		 * If the recording ends in the past, don't show it.
		 */
		if (rec_t < t)
			continue;

		switch (status) {
		case RS_RECORDING:
			item_attr.fg = mythtv_colors.pending_recording;
			type = '1';
			break;
		case RS_WILL_RECORD:
			item_attr.fg = mythtv_colors.pending_will_record;
			type = '1';
			break;
		case RS_CONFLICT:
			item_attr.fg = mythtv_colors.pending_conflict;
			type = 'C';
			break;
		case RS_DONT_RECORD:
			item_attr.fg = mythtv_colors.pending_other;
			type = 'X';
			break;
		case RS_TOO_MANY_RECORDINGS:
			item_attr.fg = mythtv_colors.pending_other;
			type = 'T';
			break;
		case RS_PREVIOUS_RECORDING:
			item_attr.fg = mythtv_colors.pending_other;
			type = 'P';
			break;
		case RS_LATER_SHOWING:
			item_attr.fg = mythtv_colors.pending_other;
			type = 'L';
			break;
		case RS_EARLIER_RECORDING:
			item_attr.fg = mythtv_colors.pending_other;
			type = 'E';
			break;
		case RS_REPEAT:
			item_attr.fg = mythtv_colors.pending_other;
			type = 'r';
			break;
		case RS_CURRENT_RECORDING:
			item_attr.fg = mythtv_colors.pending_other;
			type = 'R';
			break;
		default:
			item_attr.fg = mythtv_colors.pending_other;
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

 out:
	pthread_mutex_unlock(&myth_mutex);

	return ret;
}

static void*
wd_start(void *arg)
{
	int state = 0, old = 0;
	char err[] = "MythTV backend connection is not responding.";
	char ok[] = "MythTV backend connection has been restored.";

	printf("myth watchdog thread started (pid %d)\n", getpid());

	while (1) {
		if ((state=cmyth_conn_hung(control)) == 1) {
			if (old == 0) {
				gui_error(err);
			}
		} else {
			if (old == 1) {
				fprintf(stderr, "%s\n", ok);
				gui_error_clear();
			}
		}
		old = state;
		sleep(1);
	}

	return NULL;
}

static void
sighandler(int sig)
{
	/*
	 * The signal handler is here simply to allow the threads reading from
	 * the mythbackend socket to be interrupted, and break out of the
	 * system call they are in.
	 */
}

static void*
control_start(void *arg)
{
	int len = 0;
	int size = BSIZE;
	demux_attr_t *attr;
	pid_t pid;

	pid = getpid();
	signal(SIGURG, sighandler);

	while (1) {
		int count = 0;
		cmyth_conn_t c = NULL;
		cmyth_file_t f = NULL;
		cmyth_recorder_t r = NULL;
		int audio_selected, audio_checks;

		pthread_mutex_lock(&mutex);
		printf("mythtv control thread sleeping...(pid %d)\n", pid);
		while ((file == NULL) && (recorder == NULL))
			pthread_cond_wait(&cond, &mutex);
		pthread_mutex_unlock(&mutex);

		printf("mythtv control thread starting...(pid %d)\n", pid);

		attr = demux_get_attr(handle);

		if (mythtv_livetv) {
			if ((c=cmyth_conn_hold(control)) == NULL)
				goto end;
			if ((r=cmyth_recorder_hold(recorder)) == NULL)
				goto end;
		} else {
			if ((c=cmyth_conn_hold(control_slave)) == NULL)
				goto end;
			if ((f=cmyth_file_hold(file)) == NULL)
				goto end;
		}

		audio_selected = 0;
		audio_checks = 0;
		video_reading = 1;

		do {
			if (seeking || jumping) {
				size = 1024*96;
			} else {
				if ((attr->video.bufsz -
				     attr->video.stats.cur_bytes) < BSIZE) {
					size = attr->video.bufsz -
						attr->video.stats.cur_bytes -
						1024;
				} else {
					size = BSIZE;
				}

				if (((file == NULL) && (recorder == NULL)) ||
				    close_mythtv)
					break;
			
				if (size < 2048) {
					usleep(1000);
					continue;
				}
			}

			if (changing_channel) {
				usleep(1000);
				continue;
			}

			if (mythtv_livetv)
				len = cmyth_ringbuf_request_block(c, r, size);
			else
				len = cmyth_file_request_block(c, f, size);

			/*
			 * Will block if another command is executing
			 */
			pthread_mutex_lock(&myth_mutex);
			pthread_mutex_unlock(&myth_mutex);

			/*
			 * If the user has paused playback, the request block
			 * call will fail.
			 */
			if ((len < 0) && paused) {
				printf("%s(): waiting to unpause...\n",
				       __FUNCTION__);
				while (paused && (file || recorder) &&
				       !close_mythtv)
					usleep(1000);
				len = 1;
				continue;
			}

			/*
			 * The following errors mean that the myth connection
			 * has been lost.
			 */
			if ((len == -EPIPE) || (len == -ECONNRESET) ||
			    (len == -EBADF)) {
				fprintf(stderr,
					"request block connection failed %d\n",
					len);
				break;
			}

			/*
			 * If the request block call returns another error,
			 * then retry a few times before giving up.
			 */
			if (len <= 0) {
				if (count++ > 4) {
					fprintf(stderr,
						"request block failed %d\n",
						len);
					break;
				} else {
					fprintf(stderr,
						"request block failed\n");
					len = 1;
					continue;
				}
			}

			count = 0;

			/*
			 * Force myth recordings to start with the numerically
			 * lowest audio stream, rather than the first audio
			 * stream seen in the file.
			 */
			if (!audio_selected) {
				if (audio_switch_stream(NULL, 0xc0) == 0) {
					printf("selected audio stream 0xc0\n");
					audio_selected = 1;
				} else if (audio_checks++ == 4) {
					printf("audio stream 0xc0 not found\n");
					audio_selected = 1;
				}
			}
		} while ((file || recorder) && (len > 0) &&
			 (playing_via_mythtv == 1) && (!close_mythtv));

	end:
		video_reading = 0;

		printf("%s(): len %d playing_via_mythtv %d close_mythtv %d\n",
		       __FUNCTION__, len, playing_via_mythtv, close_mythtv);

		if (mythtv_livetv)
			cmyth_recorder_release(r);
		else
			cmyth_file_release(c, f);
		cmyth_conn_release(c);
		if (!mythtv_livetv)
			mythtv_close_file();

		if ((len == -EPIPE) || (len == -ECONNRESET) ||
		    (len == -EBADF)) {
			mythtv_shutdown();
			continue;
		}

	}

	return NULL;
}

int
mythtv_init(char *server_name, int portnum)
{
	char *server = "127.0.0.1";
	int port = 6543;
	static int thread = 0;

	if (mythtv_debug)
		cmyth_dbg_all();

	if (server_name)
		server = server_name;
	if (portnum > 0)
		port = portnum;

	if ((control=cmyth_conn_connect_ctrl(server, port, 16*1024)) == NULL) {
		fprintf(stderr, "cannot connect to mythtv server %s\n",
			server);
		return -1;
	}

	if (!thread) {
		thread = 1;
		pthread_create(&control_thread, &thread_attr_small,
			       control_start, NULL);
		pthread_create(&wd_thread, &thread_attr_small, wd_start, NULL);
	}

	return 0;
}

void
mythtv_program(mvp_widget_t *widget)
{
	char *description, *program;
	char *title, *subtitle;
	cmyth_timestamp_t ts;
	char start[256], end[256], str[256], *ptr, *chansign;
		
	if (current_prog) {
		title = (char *) cmyth_proginfo_title(current_prog);
		subtitle = (char *) cmyth_proginfo_subtitle(current_prog);
		
		if (mythtv_livetv) {
			chansign = (char*)cmyth_proginfo_chansign(current_prog);
			
			ts = cmyth_proginfo_start(current_prog);
			cmyth_timestamp_to_string(start, ts);
			cmyth_timestamp_release(ts);
			ts = cmyth_proginfo_end(current_prog);
			cmyth_timestamp_to_string(end, ts);
			cmyth_timestamp_release(ts);
		
			ptr = strchr(start, 'T');
			*ptr = '\0';
			sprintf(str, "%5.5s - ", ptr+1);
			ptr = strchr(end, 'T');
			*ptr = '\0';
			strncat(str, ptr+1,5);
		
			program = alloca(strlen(chansign) + strlen(str) +
					 strlen(title) + strlen(subtitle) + 16);
			sprintf(program, "[%s] %s %s - %s",
				chansign, str, title, subtitle);
		} else {					
			program = alloca(strlen(title) +
					 strlen(subtitle) + 16);
			sprintf(program, "%s - %s", title, subtitle);
		}
		
		description = (char *)cmyth_proginfo_description(current_prog);

		mvpw_set_text_str(mythtv_osd_description, description);
		mvpw_set_text_str(mythtv_osd_program, program);
	}
}

void
mythtv_stop(void)
{
	pthread_mutex_lock(&myth_mutex);
	mythtv_close_file();
	pthread_mutex_unlock(&myth_mutex);

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);
}

int
mythtv_delete(void)
{
	int ret;

	pthread_mutex_lock(&myth_mutex);
	ret = cmyth_proginfo_delete_recording(control, hilite_prog);
	if (cmyth_proginfo_compare(hilite_prog, current_prog) == 0) {
		mythtv_close_file();
		video_clear();
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
	}
	pthread_mutex_unlock(&myth_mutex);

	return ret;
}

int
mythtv_forget(void)
{
	int ret;

	pthread_mutex_lock(&myth_mutex);
	ret = cmyth_proginfo_forget_recording(control, hilite_prog);
	if (cmyth_proginfo_compare(hilite_prog, current_prog) == 0) {
		mythtv_close_file();
		video_clear();
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
	}
	pthread_mutex_unlock(&myth_mutex);

	return ret;
}

int
mythtv_proginfo(char *buf, int size)
{
	cmyth_timestamp_t ts;
	char airdate[256], start[256], end[256];
	char *ptr;

	ts = cmyth_proginfo_originalairdate(hilite_prog);
	cmyth_timestamp_to_string(airdate, ts);
	cmyth_timestamp_release(ts);
	ts = cmyth_proginfo_rec_start(hilite_prog);
	cmyth_timestamp_to_string(start, ts);
	cmyth_timestamp_release(ts);
	ts = cmyth_proginfo_rec_end(hilite_prog);
	cmyth_timestamp_to_string(end, ts);
	cmyth_timestamp_release(ts);

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
		 cmyth_proginfo_title(hilite_prog),
		 cmyth_proginfo_subtitle(hilite_prog),
		 cmyth_proginfo_description(hilite_prog),
		 start,
		 end,
		 cmyth_proginfo_category(hilite_prog),
		 cmyth_proginfo_channame(hilite_prog),
		 cmyth_proginfo_seriesid(hilite_prog),
		 cmyth_proginfo_programid(hilite_prog),
		 cmyth_proginfo_stars(hilite_prog));

	return 0;
}

void
mythtv_cleanup(void)
{
	printf("stopping all video playback...\n");

	if (file) {
		close_mythtv = 1;
		while (close_mythtv)
			usleep(1000);
	}

	if (mythtv_livetv) {
		mythtv_livetv_stop();
		mythtv_livetv = 0;
	}

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);

	printf("cleanup mythtv data structures\n");

	if (pending_plist) {
		cmyth_proglist_release(pending_plist);
		pending_plist = NULL;
	}
	if (episode_plist) {
		cmyth_proglist_release(episode_plist);
		episode_plist = NULL;
	}

	running_mythtv = 0;
}

static int
mythtv_open(void)
{
	char *host;
	int port = 6543;
	char buf[256];

	if ((host=cmyth_proginfo_host(current_prog)) == NULL) {
		fprintf(stderr, "unknown myth backend\n");
		return -1;
	}

	if (control_slave) {
		cmyth_conn_t c = control_slave;
		control_slave = NULL;
		cmyth_conn_release(c);
	}
	printf("connecting to mythtv (slave) backend %s\n", host);
	if ((control_slave=cmyth_conn_connect_ctrl(host, port,
						   1024)) == NULL) {
		mythtv_shutdown();
		return -1;
	}

	playing_via_mythtv = 1;

	if ((file=cmyth_conn_connect_file(current_prog,
					  BSIZE)) == NULL) {
		video_clear();
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
		gui_error("Cannot connect to file!");
		return -1;
	}

	printf("starting mythtv file transfer\n");

	pthread_cond_signal(&cond);

	return 0;
}

static long long
mythtv_seek(long long offset, int whence)
{
	struct timeval to;
	int count = 0;
	long long seek_pos = -1, size;
	cmyth_file_t f = NULL;
	cmyth_conn_t c = NULL;
	cmyth_recorder_t r = NULL;

	if (mythtv_livetv) {
		if ((c=cmyth_conn_hold(control)) == NULL)
			goto out;
		if ((r=cmyth_recorder_hold(recorder)) == NULL)
			goto out;
	} else {
		if ((c=cmyth_conn_hold(control_slave)) == NULL)
			goto out;
		if ((f=cmyth_file_hold(file)) == NULL)
			goto out;
	}

	if (mythtv_livetv)
		seek_pos = cmyth_ringbuf_seek(c, r, 0, SEEK_CUR);
	else
		seek_pos = cmyth_file_seek(c, f, 0, SEEK_CUR);
	if ((offset == 0) && (whence == SEEK_CUR)) {
		goto out;
	}

	if (!mythtv_livetv) {
		size = mythtv_size();
		if (size < 0) {
			fprintf(stderr, "seek failed, file size unknown\n");
			goto out;
		}
		if (((size < offset) && (whence == SEEK_SET)) ||
		    ((size < offset + seek_pos) && (whence == SEEK_CUR))) {
			fprintf(stderr, "cannot seek past end of file\n");
			goto out;
		}
	}

	pthread_mutex_lock(&seek_mutex);
	pthread_mutex_lock(&myth_mutex);

	while (1) {
		char buf[4096];
		int len;

		to.tv_sec = 0;
		to.tv_usec = 10;
		len = 0;
		if (mythtv_livetv) {
			if (cmyth_ringbuf_select(r, &to) > 0) {
				len = cmyth_ringbuf_get_block(r, buf, sizeof(buf));
			}
		} else {
			if (cmyth_file_select(f, &to) > 0) {
				PRINTF("%s(): reading...\n", __FUNCTION__);
				len = cmyth_file_get_block(f, buf,
							   sizeof(buf));
				PRINTF("%s(): read returned %d\n",
				       __FUNCTION__, len);
			}
		}

		if (len < 0)
			break;

		if (len == 0) {
			if (count++ > 4)
				break;
			else
				usleep(1000);
		}

		if (len > 0) {
			count = 0;
			PRINTF("%s(): read %d bytes\n", __FUNCTION__, len);
		}
	}

	if (mythtv_livetv)
		seek_pos = cmyth_ringbuf_seek(c, r, offset, whence);
	else
		seek_pos = cmyth_file_seek(c, f, offset, whence);

	PRINTF("%s(): pos %lld\n", __FUNCTION__, seek_pos);

	pthread_mutex_unlock(&myth_mutex);
	pthread_mutex_unlock(&seek_mutex);

 out:
	if (mythtv_livetv)
		cmyth_recorder_release(r);
	else
		cmyth_file_release(c, f);
	cmyth_conn_release(c);

	return seek_pos;
}

static int
mythtv_read(char *buf, int len)
{
	int ret;
	int tot = 0;
	cmyth_file_t f = NULL;
	cmyth_recorder_t r = NULL;
	cmyth_conn_t c = NULL;

	if ((file == NULL) && (recorder == NULL))
		return -EINVAL;

	pthread_mutex_lock(&seek_mutex);

	pthread_cond_signal(&cond);

	PRINTF("cmyth getting block of size %d\n", len);

	if (mythtv_livetv) {
		if ((c=cmyth_conn_hold(control)) == NULL)
			goto out;
		if ((r=cmyth_recorder_hold(recorder)) == NULL)
			goto out;
	} else {
		if ((c=cmyth_conn_hold(control_slave)) == NULL)
			goto out;
		if ((f=cmyth_file_hold(file)) == NULL)
			goto out;
	}

	while ((file || recorder) && (tot < len) && !myth_seeking) {
		struct timeval to;

		to.tv_sec = 0;
		to.tv_usec = 10;
		if (mythtv_livetv) {
			if (cmyth_ringbuf_select(r, &to) <= 0)
				break;
			ret = cmyth_ringbuf_get_block(r, buf+tot, len-tot);
		} else {
			if (cmyth_file_select(f, &to) <= 0)
				break;
			ret = cmyth_file_get_block(f, buf+tot, len-tot);
		}
		if (ret <= 0)
			break;
		tot += ret;
	}
	PRINTF("cmyth got block of size %d (out of %d)\n", tot, len);

 out:
	if (mythtv_livetv)
		cmyth_recorder_release(r);
	else
		cmyth_file_release(c, f);
	cmyth_conn_release(c);

	pthread_mutex_unlock(&seek_mutex);

	return tot;
}

static long long
mythtv_size(void)
{
	static struct timeval last = { 0, 0 };
	static cmyth_proginfo_t prog = NULL;
	struct timeval now;
	long long ret;
	static volatile int failed = 0;

	gettimeofday(&now, NULL);

	/*
	 * This is expensive, so use the cached value if less than 30 seconds
	 * have elapsed since the last try, and we are still playing the
	 * same recording.
	 */
	if ((prog == current_prog) && ((now.tv_sec - last.tv_sec) < 30)) {
		ret = cmyth_proginfo_length(current_prog);
		goto out;
	}

	pthread_mutex_lock(&myth_mutex);

	/*
	 * If the refill fails, use the value in the file structure.
	 */
	if (cmyth_proginfo_fill(control, current_prog) < 0) {
		if (!failed) {
			fprintf(stderr,
				"%s(): cmyth_proginfo_fill() failed!\n",
				__FUNCTION__);
			failed = 1;
		}
		ret = cmyth_file_length(file);
		goto unlock;
	}

	failed = 0;

	ret = cmyth_proginfo_length(current_prog);

	memcpy(&last, &now, sizeof(last));
	prog = current_prog;

 unlock:
	pthread_mutex_unlock(&myth_mutex);

 out:
	return ret;
}

static int
mythtv_livetv_start(int *tuner)
{
	double rate;
	char *rb_file;
	char *msg = NULL, buf[256];
	int c, i, id = 0;

	if (playing_via_mythtv && file)
		mythtv_stop();

	if (mythtv_livetv) {
		printf("Live TV already active\n");
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_focus(mythtv_browser);
		pthread_mutex_lock(&myth_mutex);
		get_livetv_programs();
		pthread_mutex_unlock(&myth_mutex);
		return 0;
	}

	printf("Starting Live TV...\n");

	if (mythtv_verify() < 0)
		return -1;

	pthread_mutex_lock(&myth_mutex);

	mythtv_livetv = 1;
	playing_via_mythtv = 1;
	if (mythtv_ringbuf) {
		video_functions = &file_functions;
	} else {
		video_functions = &livetv_functions;
	}

	if ((c=cmyth_conn_get_free_recorder_count(control)) < 0) {
		mythtv_shutdown();
		goto err;
	}

	printf("Found %d recorders\n", c);

	if (tuner[0]) {
		for (i=0; i<MAX_TUNER && tuner[i]; i++) {
			printf("Looking for tuner %d\n", tuner[i]);
			if ((recorder=cmyth_conn_get_recorder_from_num(control,
								       tuner[i])) == NULL) {
				continue;
			}
			if(cmyth_recorder_is_recording(control, recorder) == 1) {
				cmyth_recorder_release(recorder);
				continue;
			}
			id = tuner[i];
			break;
		}
		if (id == 0) {
			msg = "No tuner available for that show.";
			goto err;
		}
	} else {
		if ((recorder=cmyth_conn_get_free_recorder(control)) == NULL) {
			msg = "Failed to get free recorder.";
			goto err;
		}
	}

	if (cmyth_ringbuf_setup(control, recorder) != 0) {
		msg = "Failed to setup ringbuffer.";
		goto err;
	}

	if (cmyth_conn_connect_ring(recorder, 16*1024) != 0) {
		msg = "Cannot connect to mythtv ringbuffer.";
		goto err;
	}

	if (cmyth_recorder_spawn_livetv(control, recorder) != 0) {
		msg = "Spawn livetv failed.";
		goto err;
	}

	if (cmyth_recorder_is_recording(control, recorder) != 1) {
		msg = "LiveTV not recording.";
		goto err;
	}

	if (cmyth_recorder_get_framerate(control, recorder, &rate) != 0) {
		msg = "Get framerate failed.";
		goto err;
	}

	printf("recorder framerate is %5.2f\n", rate);

	if (current)
		free(current);
	current = malloc(1024);

	rb_file = (char *) cmyth_recorder_get_filename(recorder);
	if (mythtv_ringbuf)
		sprintf(current, "%s/%s", mythtv_ringbuf, rb_file);
	else
		sprintf(current, "%s", rb_file);

	// get the information about the current programme
	// we assume last used structure is cleared already...
	//
	if (current_prog)
		cmyth_proginfo_release(current_prog);
	current_prog = NULL;
	current_prog = cmyth_proginfo_create();
	cmyth_proginfo_hold(current_prog);

	cmyth_recorder_get_program_info(control, recorder, current_prog);

	get_livetv_programs();

	mvpw_show(mythtv_browser);
	mvpw_focus(mythtv_browser);

	demux_reset(handle);
	demux_attr_reset(handle);
	video_thumbnail(1);
	av_play();
	video_play(root);

	// enable program info widget
	//
	add_osd_widget(mythtv_program_widget, OSD_PROGRAM,
		       osd_settings.program, NULL);
	mvpw_hide(mythtv_description);
	running_mythtv = 1;

	pthread_mutex_unlock(&myth_mutex);

	return 0;

 err:
	pthread_mutex_unlock(&myth_mutex);

	mythtv_livetv = 0;
	if (msg)
		gui_error(msg);

	return -1;
}

int
mythtv_livetv_stop(void)
{
	int ret = -1;

	if (!mythtv_livetv)
		return -1;

	printf("Stopping Live TV\n");

	busy_start();

	if (!mythtv_ringbuf) {
		playing_via_mythtv = 0;
		running_mythtv = 0;
		close_mythtv = 0;
		reset_mythtv = 1;
	}

	mythtv_livetv = 0;

	pthread_mutex_lock(&myth_mutex);

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);

	if (cmyth_recorder_stop_livetv(control, recorder) != 0) {
		fprintf(stderr, "stop livetv failed\n");
		goto fail;
	}

	if (cmyth_recorder_done_ringbuf(control, recorder) != 0) {
		fprintf(stderr, "done ringbuf failed\n");
		goto fail;
	}

	cmyth_recorder_release(recorder);
	recorder = NULL;
	if (current_prog)
		cmyth_proginfo_release(current_prog);

	ret = 0;

 fail:
	busy_end();

	pthread_mutex_unlock(&myth_mutex);

	return ret;
}

int __change_channel(direction)
{
	int ret = 0;

	changing_channel = 1;

	busy_start();
	video_clear();
	pthread_mutex_lock(&myth_mutex);

	if (cmyth_recorder_pause(control, recorder) < 0) {
		fprintf(stderr, "channel change (pause) failed\n");
		ret = -1;
		goto out;
	}

	if (cmyth_recorder_change_channel(control, recorder,
					  direction) < 0) {
		fprintf(stderr, "channel change failed\n");
		ret = -1;
		goto out;
	}

	/*
	 * Force myth to delete the ringbuffer if we are playing via NFS,
	 * so that we don't risk getting old file data.
	 */
	if (mythtv_ringbuf) {
#if 0
		if (cmyth_recorder_stop_livetv(control, recorder) != 0) {
			fprintf(stderr, "stop livetv failed\n");
			ret = -1;
			goto out;
		}

		/*
		 * XXX: How do we restart live tv?  Tearing down the connection
		 *      and recreating it seems like overkill, and for all I
		 *      know you might end up on a different tuner.
		 */
#endif
		sleep(6);
	}

	if (current_prog)
		cmyth_recorder_get_program_info(control, recorder,
						current_prog);

	// we need to reset the ringbuffer reader to the start of the file
	// since the backend always resets the pointer.
	// but we must be sure there is correct data on the buffer.
	//
	demux_reset(handle);
	demux_attr_reset(handle);
	av_move(0, 0, 0);
	av_play();
	video_play(root);

 out:
	changing_channel = 0;
	busy_end();
	pthread_mutex_unlock(&myth_mutex);

        return ret;
}

int
mythtv_channel_up(void)
{
	return __change_channel(CHANNEL_DIRECTION_UP);
}

int
mythtv_channel_down(void)
{
	return __change_channel(CHANNEL_DIRECTION_DOWN);
}

void
mythtv_atexit(void)
{
	printf("%s(): start exit processing...\n", __FUNCTION__);

	pthread_mutex_lock(&seek_mutex);
	pthread_mutex_lock(&myth_mutex);

	if (mythtv_livetv) {
		mythtv_livetv_stop();
		mythtv_livetv = 0;
	}

	mythtv_close();

	printf("%s(): end exit processing...\n", __FUNCTION__);
}

int
mythtv_program_runtime(void)
{
	int seconds;

	seconds = cmyth_proginfo_length_sec(current_prog);

	return seconds;
}

static long long
livetv_size(void)
{
	long long seek_pos;

	/*
	 * XXX: How do we get the program size for live tv?
	 */

	pthread_mutex_lock(&myth_mutex);
	seek_pos = cmyth_ringbuf_seek(control, recorder, 0, SEEK_CUR);
	PRINTF("%s(): pos %lld\n", __FUNCTION__, seek_pos);
	pthread_mutex_unlock(&myth_mutex);

	return (seek_pos+(1024*1024*500));
}

static int
livetv_open(void)
{
	if (playing_via_mythtv == 0) {
		playing_via_mythtv = 1;
		printf("starting mythtv live tv transfer\n");
	}

	pthread_cond_signal(&cond);

	return 0;
}

static void
livetv_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	char *channame = NULL;
	int i, prog = (int)key;
	int id = -1;
	int tuner_change = 1, tuner[MAX_TUNER] = { 0 };
	struct livetv_proginfo *pi;

	if (mythtv_livetv) {
		id = cmyth_recorder_get_recorder_id(recorder);
		for (i=0; i<livetv_list[prog].count; i++) {
			pi = &livetv_list[prog].pi[i];
			if (id == pi->rec_id) {
				tuner_change = 0;
				channame = strdup(pi->chan);
				break;
			}
		}
	} else {
		channame = strdup(livetv_list[prog].pi[0].chan);
		for (i=0; i<livetv_list[prog].count; i++)
			tuner[i] = livetv_list[prog].pi[i].rec_id;
		tuner_change = 0;
		printf("enable livetv tuner %d chan '%s'\n",
		       tuner[0], channame);
	}

	if (tuner_change && (id != -1)) {
		for (i=0; i<livetv_list[prog].count; i++)
			tuner[i] = livetv_list[prog].pi[i].rec_id;
		channame = strdup(livetv_list[prog].pi[0].chan);
		printf("switch from tuner %d to %d\n", id, tuner[0]);
		mythtv_livetv_stop();
	}

	if (mythtv_ringbuf)
		mythtv_livetv_stop();

	if (mythtv_livetv == 0) {
		printf("Live TV not active!\n");
		if (mythtv_livetv_start(tuner) != 0)
			return;
	}

	if (item)
		printf("%s(): change channel '%s' '%s'\n",
		       __FUNCTION__, channame, item);

	changing_channel = 1;

	busy_start();
	pthread_mutex_lock(&myth_mutex);

	if (cmyth_recorder_pause(control, recorder) < 0) {
		fprintf(stderr, "channel change (pause) failed\n");
		goto err;
	}

	if (cmyth_recorder_set_channel(control, recorder, channame) < 0) {
		fprintf(stderr, "channel change failed!\n");
		goto err;
	}

	if (current_prog)
		cmyth_recorder_get_program_info(control, recorder,
						current_prog);

	demux_reset(handle);
	demux_attr_reset(handle);
	av_play();
	video_play(root);

	if (widget)
		mythtv_fullscreen();

	i = 0;
	while (mvpw_menu_set_item_attr(mythtv_browser, i, &item_attr) == 0) {
		i++;
	}
	if (mvpw_menu_get_item_attr(mythtv_browser, key, &item_attr) == 0) {
		uint32_t old_fg = item_attr.fg;
		item_attr.fg = mythtv_colors.livetv_current;
		mvpw_menu_set_item_attr(mythtv_browser, key, &item_attr);
		item_attr.fg = old_fg;
	}
	mvpw_menu_hilite_item(mythtv_browser, key);

 err:
	pthread_mutex_unlock(&myth_mutex);
	busy_end();
	changing_channel = 0;

	if (channame)
		free(channame);
}

static void
livetv_hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	int prog = (int)key;
	char buf[256];

	if (hilite) {
		current_livetv = prog;
		snprintf(buf, sizeof(buf), "%s %s",
			 livetv_list[prog].pi[0].chan,
			 livetv_list[prog].pi[0].channame);
		mvpw_set_text_str(mythtv_channel, buf);
		mvpw_expose(mythtv_channel);
		snprintf(buf, sizeof(buf), "%s - %s",
			 livetv_list[prog].start, livetv_list[prog].end);
		mvpw_set_text_str(mythtv_date, buf);
		mvpw_expose(mythtv_date);
		mvpw_set_text_str(mythtv_description,
				  livetv_list[prog].description);
		mvpw_expose(mythtv_description);
	} else {
		mvpw_set_text_str(mythtv_channel, "");
		mvpw_expose(mythtv_channel);
		mvpw_set_text_str(mythtv_date, "");
		mvpw_expose(mythtv_date);
		mvpw_set_text_str(mythtv_description, "");
		mvpw_expose(mythtv_description);
	}
}

static int
get_livetv_programs_rec(int id, struct livetv_prog **list, int *n, int *p)
{
	cmyth_proginfo_t next_prog, cur;
	cmyth_recorder_t rec = recorder;
	cmyth_timestamp_t ts;
	const char *title, *subtitle, *channame, *start_channame, *chansign;
	const char *description;
	char start[256], end[256], *ptr;
	int cur_id, i; 
	int c = 0;
	struct livetv_proginfo *pi;
	
	cur_id = cmyth_recorder_get_recorder_id(recorder);
	
	
	printf("Getting program listings for recorder %d [%d]\n", id, cur_id);

	next_prog = cmyth_proginfo_create();

	if (cur_id != id)
		if ((rec=cmyth_conn_get_recorder_from_num(control,
							       id)) == NULL) {
			fprintf(stderr,
				"failed to connect to tuner %d!\n", id);
			return -1;
		}

	cur = cmyth_proginfo_create();
	cmyth_proginfo_hold(cur);
	
	if (cmyth_recorder_get_program_info(control, rec, cur) < 0) {
		fprintf(stderr, "get program info failed!\n");
		return -1;
	}
	
	start_channame = (char *) cmyth_proginfo_channame(cur);

	do {
		if (cmyth_recorder_get_next_program_info(control, rec, cur,
							 next_prog, 1) < 0) {
			fprintf(stderr, "get next program info failed!\n");
			break;
		}

		title = (char *) cmyth_proginfo_title(next_prog);
		subtitle = (char *) cmyth_proginfo_subtitle(next_prog);
		description = (char *) cmyth_proginfo_description(next_prog);
		channame = (char *) cmyth_proginfo_channame(next_prog);
		chansign = (char *) cmyth_proginfo_chansign(next_prog);
						
		ts = cmyth_proginfo_start(next_prog);
		if (ts != NULL ) {
			cmyth_timestamp_to_string(start, ts);
			cmyth_timestamp_release(ts);
			ts = cmyth_proginfo_end(next_prog);
			cmyth_timestamp_to_string(end, ts);
			cmyth_timestamp_release(ts);
			ptr = strchr(start, 'T');
			*ptr = '\0';
			memmove(start, ptr+1, strlen(ptr+1)+1);
			ptr = strchr(end, 'T');
			*ptr = '\0';
			memmove(end, ptr+1, strlen(ptr+1)+1);
		}

		cur = next_prog;

		/*
		 * Search for duplicates only if the show has a title.
		 */
		if (title[0]) {
			for (i=0; i<*p; i++) {
				if ((strcmp((*list)[i].title, title) == 0) &&
				    (strcmp((*list)[i].subtitle, subtitle) == 0) &&
				    (strcmp((*list)[i].description, description) == 0) &&
				    (strcmp((*list)[i].start, start) == 0) &&
				    (strcmp((*list)[i].end, end) == 0)) {
					if ((*list)[i].count == MAX_TUNER)
						goto next;
					pi = &((*list)[i].pi[(*list)[i].count]);
					pi->chan = strdup(channame);
					pi->channame = strdup(chansign);
					pi->rec_id = id;
					goto next;
				}
			}
		}

		(*list)[*p].title = strdup(title);
		(*list)[*p].subtitle = strdup(subtitle);
		(*list)[*p].description = strdup(description);
		if (start)
			(*list)[*p].start = strdup(start);
		else
			(*list)[*p].start = NULL;
		if (end)
			(*list)[*p].end = strdup(end);
		else
			(*list)[*p].end = NULL;
		(*list)[*p].count = 1;
		(*list)[*p].pi[0].rec_id = id;
		(*list)[*p].pi[0].chan = strdup(channame);
		(*list)[*p].pi[0].channame = strdup(chansign);
		(*p)++;
		c++;

	next:
		if (*p == *n) {
			*n = *n*2;
			*list = realloc(*list, sizeof(**list)*(*n));
		}
	} while (strcmp(start_channame, channame) != 0);
	if (cur_id != id) {
		cmyth_recorder_release(rec);
	}
	cmyth_proginfo_release(next_prog);

	printf("Found %d shows on recorder %d\n", c, id);

	return c;
}

static int
get_livetv_programs(void)
{
	struct livetv_prog *list;
	char buf[256];
	int i, j, c, n, p, found;
	time_t t;

	if (livetv_list) {
		for (i=0; i<livetv_count; i++) {
			free(livetv_list[i].title);
			free(livetv_list[i].subtitle);
			free(livetv_list[i].description);
			free(livetv_list[i].start);
			free(livetv_list[i].end);
			for (j=0; j<livetv_list[i].count; j++) {
				free(livetv_list[i].pi[j].chan);
				free(livetv_list[i].pi[j].channame);
			}
		}
		free(livetv_list);
		livetv_count = 0;
		livetv_list = NULL;
	}

	n = 32;
	if ((list=(struct livetv_prog*)malloc(sizeof(*list)*n)) == NULL) {
		perror("malloc()");
		return -1;
	}

	if ((c=cmyth_conn_get_free_recorder_count(control)) < 0) {
		fprintf(stderr, "unable to get free recorder\n");
		mythtv_shutdown();
		return -2;
	}

	mvpw_clear_menu(mythtv_browser);

	item_attr.select = livetv_select_callback;
	item_attr.hilite = livetv_hilite_callback;

	p = 0;
	found = 0;
	for (i=0; (i<MAX_TUNER) && (found<c+1); i++) {
		if (get_livetv_programs_rec(i+1, &list, &n, &p) != -1)
			found++;
		if (found == c+1)
			break;
	}

	t = time(NULL);
	printf("Found %d programs on %d tuners at %s\n", p, found, ctime(&t));

	if (p == 0)
		return -1;

	snprintf(buf, sizeof(buf), "Live TV - %d Programs on %d Tuner%s",
		 p, found, (found == 1) ? "" : "s");
	mvpw_set_menu_title(mythtv_browser, buf);

	qsort(list, p, sizeof(*list), livetv_compare);
	livetv_list = list;
	livetv_count = p;

	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;
	for (j=0; j<p; j++) {
		snprintf(buf, sizeof(buf), "%s: %s - %s",
			 list[j].pi[0].chan, list[j].title, list[j].subtitle);
		mvpw_add_menu_item(mythtv_browser, buf, (void*)j, &item_attr);
	}

	return 0;
}

int
mythtv_livetv_menu(void)
{
	int failed = 0;
	int err;

	if (mythtv_verify() < 0)
		return -1;

	printf("Displaying livetv programs\n");

	pthread_mutex_lock(&myth_mutex);
	if ((err=get_livetv_programs()) < 0) {
		if (!mythtv_livetv) {
			if (err == -1)
				gui_error("No tuners available for Live TV.");
			failed = 1;
		}
	}
	pthread_mutex_unlock(&myth_mutex);

	if (!failed) {
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_focus(mythtv_browser);

		mvpw_show(mythtv_channel);
		mvpw_show(mythtv_date);
		mvpw_show(mythtv_description);
	}

	return failed;
}
