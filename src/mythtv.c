/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
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
#include <time.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <cmyth.h>

#include "mvpmc.h"
#include "mythtv.h"
#include "config.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

#define BSIZE   (256*1024)

volatile cmyth_file_t mythtv_file;
extern demux_handle_t *handle;
extern int fd_audio, fd_video;
extern int protocol_version;

extern mvp_widget_t *mythtv_prog_finder_1;
extern mvp_widget_t *mythtv_prog_finder_2;
extern mvp_widget_t *mythtv_prog_finder_3;

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_WHITE,
	.bg = MVPW_BLACK,
	.checkbox_fg = MVPW_GREEN,
};

extern int insert_into_record_mysql(char *, char *, char *, char *, char *, char *, char *, char * , char *, char *, char *);
extern int get_prog_finder_char_title_mysql(struct program **prog, char *starttime, char *program_name, char *dbhost, char *dbuser, char *dbpass, char *db);
extern int get_prog_finder_time_mysql(struct program **prog, char *starttime, char *program_name, char *dbhost, char *dbuser, char *dbpass, char *db);
extern int get_guide_mysql(struct program **, char *, char *, char*, char*, char*, char*);
extern int cmyth_schedule_recording(cmyth_conn_t, char *);
extern int get_myth_version(cmyth_conn_t);
extern char * mysql_escape_chars(char *, char *, char *, char *, char *);
extern int myth_get_total_channels(char *, char *, char *, char *);

pthread_cond_t myth_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t seek_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t myth_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_cond = PTHREAD_COND_INITIALIZER;

volatile cmyth_conn_t control;		/* master backend */
static volatile cmyth_conn_t event;		/* master backend */
volatile cmyth_proginfo_t current_prog;	/* program currently being played */
static volatile cmyth_proginfo_t hilite_prog;	/* program currently hilighted */
static volatile cmyth_proglist_t episode_plist;
static volatile cmyth_proglist_t pending_plist;

struct program *sqlprog=NULL;

volatile cmyth_recorder_t mythtv_recorder;
volatile cmyth_database_t mythtv_database;


static volatile int pending_dirty = 0;
static volatile int episode_dirty = 0;

volatile mythtv_state_t mythtv_state = MYTHTV_STATE_MAIN;

static int show_count, episode_count;
static volatile int list_all = 0;
static time_t curtime;

int playing_file = 0;
int running_mythtv = 0;
int mythtv_main_menu = 0;
int mythtv_debug = 0;

volatile int playing_via_mythtv = 0;
volatile int close_mythtv = 0;
volatile int changing_channel = 0;
static volatile int video_reading = 0;

static pthread_t control_thread, wd_thread, event_thread;

static volatile int myth_seeking = 0;

static char *hilite_path = NULL;

int mythtv_tcp_control = 4096;
int mythtv_tcp_program = 0;
int mythtv_sort = 0;
int mythtv_sort_dirty = 1;

show_sort_t show_sort = SHOW_TITLE;

static video_callback_t mythtv_functions = {
	.open      = mythtv_open,
	.read      = mythtv_read,
	.read_dynb = NULL,
	.seek      = mythtv_seek,
	.size      = mythtv_size,
	.notify    = NULL,
	.key       = NULL,
	.halt_stream = NULL,
};

mythtv_color_t mythtv_colors = {
	.livetv_current		= MVPW_GREEN,
	.pending_recording	= 0xff4fa5ff,
	.pending_will_record	= MVPW_GREEN,
	.pending_conflict	= MVPW_YELLOW,
	.pending_other		= MVPW_BLACK,
	.menu_item		= MVPW_BLUE,
};

int chan_Total_rows=0;


static void
add_recgroup(char *recgroup)
{
	int i;

	for (i=0; i<MYTHTV_RG_MAX; i++) {
		if (strcmp(config->mythtv_recgroup[i].label, recgroup) == 0) {
			return;
		}
	}

	for (i=0; i<MYTHTV_RG_MAX; i++) {
		if (config->mythtv_recgroup[i].label[0] == '\0') {
			printf("Add recgroup: %s\n", recgroup);
			snprintf(config->mythtv_recgroup[i].label,
				 sizeof(config->mythtv_recgroup[i].label),
				 recgroup);
			config->bitmask |= CONFIG_MYTHTV_RECGROUP;
			return;
		}
	}
}

static int
string_compare(const void *a, const void *b)
{
	const char *x, *y;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	x = *((const char**)a);
	y = *((const char**)b);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return strcmp(x, y);
}

void
mythtv_show_widgets(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
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
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

void
mythtv_fullscreen(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
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

	fprintf(stderr, "fullscreen video mode\n");

	screensaver_disable();
}

int
mythtv_verify(void)
{
	char buf[128];

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (control == NULL) {
		if (mythtv_init(mythtv_server, -1) < 0) {
			snprintf(buf, sizeof(buf),
				 "Connect to mythtv server %s failed!",
				 mythtv_server ? mythtv_server : "127.0.0.1");
			gui_error(buf);
			cmyth_dbg(CMYTH_DBG_DEBUG,
				    "%s [%s:%d]: (trace) -1}\n",
				    __FUNCTION__, __FILE__, __LINE__);
			return -1;
		}
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);

	return 0;
}

static void
mythtv_close(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	CHANGE_GLOBAL_REF(mythtv_file, NULL);
	CHANGE_GLOBAL_REF(mythtv_recorder, NULL);
	CHANGE_GLOBAL_REF(current_prog, NULL);
	CHANGE_GLOBAL_REF(hilite_prog, NULL);
	CHANGE_GLOBAL_REF(episode_plist, NULL);
	CHANGE_GLOBAL_REF(pending_plist, NULL);
	CHANGE_GLOBAL_REF(control, NULL);
	CHANGE_GLOBAL_REF(event, NULL);
}

void
mythtv_shutdown(int display)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	fprintf(stderr, "%s(): closing mythtv connection\n", __FUNCTION__);

	mythtv_close();

	if (gui_state == MVPMC_STATE_MYTHTV) {
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
	}

	mythtv_main_menu = 1;
	close_mythtv = 0;
	mythtv_state = MYTHTV_STATE_MAIN;
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);

	if (display) {
		if ((gui_state == MVPMC_STATE_MYTHTV) ||
		    (hw_state == MVPMC_STATE_MYTHTV))
			gui_error("MythTV connection lost!");
	}

	cmyth_alloc_show();
}

static void
mythtv_close_file(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (playing_via_mythtv && mythtv_file) {
		fprintf(stderr, "%s(): closing file\n", __FUNCTION__);
		CHANGE_GLOBAL_REF(mythtv_file, NULL);
	}

	if (current_prog) {
		fprintf(stderr, "%s(): releasing current prog\n",
			__FUNCTION__);
		CHANGE_GLOBAL_REF(current_prog, NULL);
	}

	close_mythtv = 0;
	playing_file = 0;

	/*
	 * Wakeup anybody that is sleeping in a system call.
	 */
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
	pthread_kill(control_thread, SIGURG);
}

static void
hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	char *description, *channame;
	char *pathname = NULL;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	mvpw_hide(shows_widget);
	mvpw_hide(episodes_widget);
	mvpw_hide(freespace_widget);

	if (hilite) {
		cmyth_timestamp_t ts;
		char start[256], end[256], str[256], *ptr;
		cmyth_proginfo_t hi_prog = cmyth_hold(hilite_prog);
		cmyth_proglist_t ep_list = cmyth_hold(episode_plist);

		mvpw_show(mythtv_channel);
		mvpw_show(mythtv_date);
		mvpw_show(mythtv_description);

		cmyth_release(ep_list);
		cmyth_release(hi_prog);
		hi_prog = cmyth_proglist_get_item(ep_list, (int)key);
		CHANGE_GLOBAL_REF(hilite_prog, hi_prog);

/*  SAMPLE CODE TO USE BOOKMARK FUNCTIONS -- not in correct place, I know,
*		but this is where I tested it....
*	To Retrieve bookmark:
*		long long bookmark;
*		cmyth_conn_t ctrl=cmyth_hold(control);
*		bookmark = cmyth_get_bookmark(ctrl, hi_prog);
*		cmyth_dbg(CMYTH_DBG_DEBUG, "retrieved bookmark, value: %qd \n",
*			bookmark);
*		cmyth_release(ctrl);
*	To set bookmark:
*		bookmark = 9999;
*		int msgret;
*		msgret = cmyth_set_bookmark(ctrl, hi_prog, bookmark);
*/
		channame = cmyth_proginfo_channame(hi_prog);
		if (channame) {
			mvpw_set_text_str(mythtv_channel, channame);
			cmyth_release(channame);
		} else {
			fprintf(stderr, "program channel name not found!\n");
			mvpw_set_text_str(mythtv_channel, "");
		}
		mvpw_expose(mythtv_channel);

		description = cmyth_proginfo_description(hi_prog);
		if (description) {
			mvpw_set_text_str(mythtv_description, description);
			cmyth_release(description);
		} else {
			fprintf(stderr, "program description not found!\n");
			mvpw_set_text_str(mythtv_description, "");
		}
		mvpw_expose(mythtv_description);

		ts = cmyth_proginfo_rec_start(hi_prog);
		cmyth_timestamp_to_string(start, ts);
		cmyth_release(ts);
		ts = cmyth_proginfo_rec_end(hi_prog);
		cmyth_timestamp_to_string(end, ts);
		cmyth_release(ts);
		
		pathname = cmyth_proginfo_pathname(hi_prog);
		if (!pathname) {
			printf("NULL Pathname for HILITE PROG!!!!!!!!!!!!!\n");
		}
		cmyth_release(hi_prog);
		CHANGE_GLOBAL_REF(hilite_path, NULL);

		if (mythtv_recdir) {
			char *mythtv_recdir_tosplit = 
				cmyth_strdup(mythtv_recdir);
			char *recdir_token =
				strtok(mythtv_recdir_tosplit,":");

			while (recdir_token != NULL)
			{
				FILE *test_file;
				char *test_path =
					cmyth_allocate(strlen(recdir_token) +
						       strlen(pathname) + 1);
				sprintf(test_path,"%s%s",
					recdir_token, pathname);
				if ((test_file=fopen(test_path, "r")) != NULL)
				{
					char *path = cmyth_hold(hilite_path);
					cmyth_release(hilite_path);
					hilite_path = cmyth_hold(test_path);
					cmyth_release(path);
					fclose(test_file);
				}
				cmyth_release(test_path);
				recdir_token = strtok(NULL,":");
			}
			cmyth_release(mythtv_recdir_tosplit);
		} else {
			CHANGE_GLOBAL_REF(hilite_path, pathname);
		}
		cmyth_release(pathname);
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
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static void
show_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	cmyth_proginfo_t loc_prog = cmyth_hold(current_prog);
	cmyth_proginfo_t hi_prog = cmyth_hold(hilite_prog);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	switch_hw_state(MVPMC_STATE_MYTHTV);

	if (mythtv_recdir) {
		video_functions = &file_functions;
	} else {
		video_functions = &mythtv_functions;
	}

	mythtv_fullscreen();

	if (cmyth_proginfo_compare(hi_prog, loc_prog) != 0) {
		/*
		 * Change current.  The thing about 'current' is that
		 * it is a global that is not allocated as reference
		 * counted and it is allocated and freed outside this
		 * file.  If / when it turns into reference counted
		 * space, it will be much cleaner and safer to hold it
		 * in a local, release and change it and then release
		 * the local.  For now we take a chance on a non-held
		 * reference being destroyed.
		 */
		char *path = current;
		current = strdup(hilite_path);
		free(path);

		if (mythtv_livetv) {
			mythtv_livetv_stop();
			running_mythtv = 1;
		} else {
			pthread_mutex_lock(&myth_mutex);
			mythtv_close_file();
			pthread_mutex_unlock(&myth_mutex);
		}

		while (video_reading)
			;

		CHANGE_GLOBAL_REF(current_prog, hi_prog);

		demux_reset(handle);
		demux_attr_reset(handle);
		av_move(0, 0, 0);
		av_play();
		video_play(root);
	}
	cmyth_release(hi_prog);
	cmyth_release(loc_prog);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

void
mythtv_start_thumbnail(void)
{
	cmyth_proginfo_t loc_prog = cmyth_hold(current_prog);
	cmyth_proginfo_t hi_prog = cmyth_hold(hilite_prog);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	busy_start();

	switch_hw_state(MVPMC_STATE_MYTHTV);

	if (mythtv_recdir) {
		video_functions = &file_functions;
	} else {
		video_functions = &mythtv_functions;
	}

	if (mythtv_state == MYTHTV_STATE_LIVETV) {
		fprintf(stderr, "trying to start livetv thumbnail...\n");
		livetv_select_callback(NULL, NULL, (void*)current_livetv);

		if (si.rows == 480)
			av_move(475, si.rows-60, 4);
		else
			av_move(475, si.rows-113, 4);

		fprintf(stderr, "thumbnail video mode\n");

		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
			    __FUNCTION__, __FILE__, __LINE__);
		goto out;
	}

	if (cmyth_proginfo_compare(hi_prog, loc_prog) != 0) {
		char *path;

		if (si.rows == 480)
			av_move(475, si.rows-60, 4);
		else
			av_move(475, si.rows-113, 4);

		fprintf(stderr, "thumbnail video mode\n");

		pthread_mutex_lock(&myth_mutex);
		mythtv_close_file();
		pthread_mutex_unlock(&myth_mutex);

		while (video_reading)
			;

		CHANGE_GLOBAL_REF(current_prog, hi_prog);

		/*
		 * Change current.  The thing about 'current' is that
		 * it is a global that is not allocated as reference
		 * counted and it is allocated and freed outside this
		 * file.  If / when it turns into reference counted
		 * space, it will be much cleaner and safer to hold it
		 * in a local, release and change it and then release
		 * the local.  For now we take a chance on a non-held
		 * reference being destroyed.
		 */
		path = current;
		current = strdup(hilite_path);
		free(path);

		demux_reset(handle);
		demux_seek(handle);
		demux_attr_reset(handle);
		av_reset();
		av_play();
		video_play(root);
	}
 out:
	cmyth_release(hi_prog);
	cmyth_release(loc_prog);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
	busy_end();
}

static int
load_episodes(void)
{
	cmyth_proglist_t ep_list = cmyth_hold(episode_plist);
	cmyth_conn_t ctrl = cmyth_hold(control);
	int ret = 0;
	int count = 0;
	int i;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if ((ep_list == NULL) || episode_dirty) {
		if (ep_list)
			cmyth_release(ep_list);
		ep_list = cmyth_proglist_get_all_recorded(ctrl);
		CHANGE_GLOBAL_REF(episode_plist, ep_list);

		if (ep_list == NULL) {
			fprintf(stderr, "get recorded failed\n");
			mythtv_shutdown(1);
			goto err;
		}
		episode_dirty = 0;
		mythtv_sort_dirty = 1;
	} else {
		fprintf(stderr, "Using cached episode data\n");
	}
	fprintf(stderr, "'cmyth_proglist_get_all_recorded' worked\n");

	count = cmyth_proglist_get_count(ep_list);

	/* Sort on first load and when setting changes or list update makes the sort dirty */
	if(mythtv_sort_dirty) {
		printf("Sort for Dirty List\n");
		cmyth_proglist_sort(ep_list, count, mythtv_sort);
		mythtv_sort_dirty = 0;
	}

	/*
	 * Save all known recording groups
	 */
	for (i=0; i<count; i++) {
		char *recgroup;
		cmyth_proginfo_t prog;

		prog = cmyth_proglist_get_item(ep_list, i);
		recgroup = cmyth_proginfo_recgroup(prog);

		add_recgroup(recgroup);

		cmyth_release(prog);
		cmyth_release(recgroup);
	}

	cmyth_release(ep_list);
	cmyth_release(ctrl);

	return count;

    err:
	cmyth_release(ep_list);
	cmyth_release(ctrl);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return ret;
}

static int
episode_exists(char *title)
{
	cmyth_proglist_t ep_list = cmyth_hold(episode_plist);
	int i, count;
	cmyth_proginfo_t prog;
	char *t;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if ((episode_plist == NULL) || (title == NULL)) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return 0;
	}

	count = cmyth_proglist_get_count(ep_list);

	for (i = 0; i < count; ++i) {
		prog = cmyth_proglist_get_item(ep_list, i);
		switch (show_sort) {
		case SHOW_TITLE:
			t = cmyth_proginfo_title(prog);
			break;
		case SHOW_CATEGORY:
			t = cmyth_proginfo_category(prog);
			break;
		case SHOW_RECGROUP:
			t = cmyth_proginfo_recgroup(prog);
			break;
		default:
			t = NULL;
			break;
		}
		cmyth_release(prog);
		if (strcmp(title, t) == 0) {
			cmyth_dbg(CMYTH_DBG_DEBUG,
				    "%s [%s:%d]: (trace) 1}\n",
				    __FUNCTION__, __FILE__, __LINE__);
			return 1;
		}
		cmyth_release(t);
	}

	cmyth_release(ep_list);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

static int
episode_index(cmyth_proginfo_t episode)
{
	int i, count;
	cmyth_proginfo_t prog;

	if ((episode_plist == NULL) || (episode == NULL))
		return -1;

	count = cmyth_proglist_get_count(episode_plist);

	for (i = 0; i < count; ++i) {
		prog = cmyth_proglist_get_item(episode_plist, i);
		if (cmyth_proginfo_compare(prog, episode) == 0) {
			cmyth_release(prog);
			return i;
		}
		cmyth_release(prog);
	}

	return -1;
}

static void
add_episodes(mvp_widget_t *widget, char *item, int load)
{
	char *name, *title, *subtitle;
	int count, i, n = 0, episodes = 0;
	char buf[256];
	char *prog;
	cmyth_proglist_t ep_list = cmyth_hold(episode_plist);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	busy_start();

	mythtv_state = MYTHTV_STATE_EPISODES;

	item_attr.select = show_select_callback;
	item_attr.hilite = hilite_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	prog = cmyth_strdup(item);
	mvpw_clear_menu(widget);

	if (load) {
		count = load_episodes();
		cmyth_release(ep_list);
		ep_list = cmyth_hold(episode_plist);
	} else {
		count = cmyth_proglist_get_count(ep_list);
	}

	if ((strcmp(prog, "All - Newest first") == 0) || 
		(strcmp(prog, "All - Oldest first") == 0)) 
	{
		printf("Sort for Newest/Oldest First\n");
		cmyth_proglist_sort(episode_plist, count, 
			MYTHTV_SORT_DATE_RECORDED);
		mythtv_sort_dirty = 1;
	}
	/* else the dirty flag causes the sort in load_episodes to 
	 * be triggered when returning to the episode list.  There 
	 * is no need to sort again here.  The side affect is that 
         * moving from Oldest to Newest causes two sorts in a row */
		
	for (i = 0; i < count; ++i) {
		char full[256], *recgroup;
		cmyth_proginfo_t ep_prog = NULL;
		int j, hide = 0;

		if (strcmp(prog, "All - Newest first") == 0) { 
			ep_prog = cmyth_proglist_get_item(episode_plist,
							       count-i-1);
		} else { 
			ep_prog = cmyth_proglist_get_item(episode_plist, i);
		}

		recgroup = cmyth_proginfo_recgroup(ep_prog);
		for (j=0; j<MYTHTV_RG_MAX; j++) {
			if (strcmp(recgroup,
				   config->mythtv_recgroup[j].label) == 0) {
				hide = config->mythtv_recgroup[j].hide;
			}
		}
		cmyth_release(recgroup);

		if (hide) {
			cmyth_release(ep_prog);
			continue;
		}

		title = cmyth_proginfo_title(ep_prog);

		switch (show_sort) {
		case SHOW_TITLE:
			name = cmyth_hold(title);
			break;
		case SHOW_CATEGORY:
			name = cmyth_proginfo_category(ep_prog);
			break;
		case SHOW_RECGROUP:
			name = cmyth_proginfo_recgroup(ep_prog);
			break;
		default:
			name = NULL;
			break;
		}

		subtitle = cmyth_proginfo_subtitle(ep_prog);

		if ((name == title) && (strcmp(name, prog) == 0)) {
			list_all = 0;
			if ((strcmp(subtitle, " ") == 0) ||
			    (subtitle[0] == '\0'))
				subtitle = cmyth_strdup("<no subtitle>");
			mvpw_add_menu_item(widget, (char*)subtitle, (void*)i,
					   &item_attr);
			episodes++;
		} else if (strcmp(prog, "All - Newest first") == 0) {
			list_all = 2;
			snprintf(full, sizeof(full), "%s - %s",
				 title, subtitle);
			mvpw_add_menu_item(widget, full, (void*)count-i-1,
					   &item_attr);
			episodes++;
		} else if ((strcmp(prog, "All - Oldest first") == 0) ||
			   ((name != title) && (strcmp(name, prog) == 0))) {
			if (strcmp(prog, "All - Oldest first") == 0)
				list_all = 1;
			else
				list_all = 0;
			snprintf(full, sizeof(full), "%s - %s",
				 title, subtitle);
			mvpw_add_menu_item(widget, full, (void*)i,
					   &item_attr);
			episodes++;
		}
		cmyth_release(name);
		cmyth_release(title);
		cmyth_release(subtitle);
		cmyth_release(ep_prog);
		n++;
	}

	snprintf(buf, sizeof(buf), "%s - %d episode", prog, episodes);
	if (episodes != 1)
		strcat(buf, "s");
	mvpw_set_menu_title(widget, buf);
	cmyth_release(prog);

	busy_end();
	cmyth_release(ep_list);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static void
select_callback(mvp_widget_t *widget, char *item, void *key)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	add_episodes(widget, item, 1);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static void
add_shows(mvp_widget_t *widget)
{
	cmyth_proglist_t ep_list;
	int count;
	int i, j, n = 0;
	char *titles[1024];

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	item_attr.select = select_callback;
	item_attr.hilite = NULL;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	count = load_episodes();
	episode_count = 0;
	ep_list  = cmyth_hold(episode_plist);
	for (i = 0; i < count; ++i) {
		cmyth_proginfo_t prog = cmyth_proglist_get_item(ep_list, i);
		char *title = NULL, *recgroup;
		int hide = 0;

		recgroup = cmyth_proginfo_recgroup(prog);
		for (j=0; j<MYTHTV_RG_MAX; j++) {
			if (strcmp(recgroup,
				   config->mythtv_recgroup[j].label) == 0) {
				hide = config->mythtv_recgroup[j].hide;
			}
		}
		cmyth_release(recgroup);

		if (!hide) {
			switch (show_sort) {
			case SHOW_TITLE:
				title = cmyth_proginfo_title(prog);
				break;
			case SHOW_CATEGORY:
				title = cmyth_proginfo_category(prog);
				break;
			case SHOW_RECGROUP:
				title = cmyth_proginfo_recgroup(prog);
				break;
			default:
				title = NULL;
				break;
			}
			
			for (j=0; j<n; j++)
				if (strcmp(title, titles[j]) == 0)
					break;
			if (j == n) {
				titles[n] = cmyth_hold(title);
				n++;
			}
			episode_count++;
		}

		cmyth_release(prog);
		cmyth_release(title);
	}
	cmyth_release(ep_list);
	show_count = n;

	qsort(titles, n, sizeof(char*), string_compare);

	mvpw_add_menu_item(widget, "All - Newest first", (void*)0, &item_attr);
	mvpw_add_menu_item(widget, "All - Oldest first", (void*)1, &item_attr);

	for (i=0; i<n; i++) {
		mvpw_add_menu_item(widget, titles[i], (void*)n+2, &item_attr);
		cmyth_release(titles[i]);
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

int
mythtv_update(mvp_widget_t *widget)
{
	char buf[64];
	long long total, used;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	running_mythtv = 1;

	mvpw_show(root);
	mvpw_expose(root);

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	busy_start();
	pthread_mutex_lock(&myth_mutex);

	if (mythtv_state == MYTHTV_STATE_EPISODES) {
		cmyth_proginfo_t hi_prog = cmyth_hold(hilite_prog);
		char *title = NULL;
		int count;

		switch (show_sort) {
		case SHOW_TITLE:
			title=cmyth_proginfo_title(hi_prog);
			break;
		case SHOW_CATEGORY:
			title=cmyth_proginfo_category(hi_prog);
			break;
		case SHOW_RECGROUP:
			title=cmyth_proginfo_recgroup(hi_prog);
			break;
		}
		cmyth_release(hi_prog);
		if (!list_all)
			fprintf(stderr, "checking for more episodes of '%s'\n",
				title);
		count = load_episodes();
		if ((count > 0) && (list_all || episode_exists(title))) {
			fprintf(stderr, "staying in episode menu\n");
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
			goto out;
		}
		cmyth_release(title);
		fprintf(stderr, "returning to program menu\n");
		mythtv_state = MYTHTV_STATE_PROGRAMS;
	}

	list_all = 0;

	add_osd_widget(mythtv_program_widget, OSD_PROGRAM,
		       osd_settings.program, NULL);

	mvpw_set_menu_title(widget, "MythTV");
	mvpw_clear_menu(widget);
	add_shows(widget);

	switch (show_sort) {
	case SHOW_TITLE:
		snprintf(buf, sizeof(buf), "Total shows: %d", show_count);
		break;
	case SHOW_CATEGORY:
		snprintf(buf, sizeof(buf), "Total categories: %d", show_count);
		break;
	case SHOW_RECGROUP:
		snprintf(buf, sizeof(buf), "Total recording groups: %d",
			 show_count);
		break;
	}
	mvpw_set_text_str(shows_widget, buf);
	snprintf(buf, sizeof(buf), "Total episodes: %d", episode_count);
	mvpw_set_text_str(episodes_widget, buf);

	if (cmyth_conn_get_freespace(control, &total, &used) == 0) {
		snprintf(buf, sizeof(buf),
			 "Diskspace: %5.2f GB (%5.2f%%) free",
			 (total-used)/1024.0,
			 100.0-((float)used/total)*100.0);
		mvpw_set_text_str(freespace_widget, buf);
	}

	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);

	if (mythtv_state != MYTHTV_STATE_MAIN) {
		mvpw_show(shows_widget);
		mvpw_show(episodes_widget);
		mvpw_show(freespace_widget);
	}

 out:
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	pthread_mutex_unlock(&myth_mutex);
	busy_end();

	return 0;
}

int
mythtv_back(mvp_widget_t *widget)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
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
	    (mythtv_state == MYTHTV_STATE_PENDING) || (mythtv_state == MYTHTV_STATE_SCHEDULE) ) {
		return 0;
	}

	if (hilite_prog) {
		cmyth_release(hilite_prog);
		hilite_prog = NULL;
	}

	mythtv_state = MYTHTV_STATE_PROGRAMS;
	mythtv_update(widget);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return -1;
}

static void
pending_hilite_callback(mvp_widget_t *widget,
			char *item,
			void *key, int hilite)
{
	int n = (int)key;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (hilite) {
		char start[256], end[256], str[256];
		char *description, *channame, *ptr;
		cmyth_timestamp_t ts;
		cmyth_proginfo_rec_status_t status;
		cmyth_proginfo_t prog = NULL;
		cmyth_proglist_t pnd_list = cmyth_hold(pending_plist);

		prog = cmyth_proglist_get_item(pnd_list, n);
		CHANGE_GLOBAL_REF(hilite_prog, prog);

		status = cmyth_proginfo_rec_status(prog);
		description = (char*)cmyth_proginfo_description(prog);
		channame = (char*)cmyth_proginfo_channame(prog);
		ts = cmyth_proginfo_rec_start(prog);
		cmyth_timestamp_to_string(start, ts);
		cmyth_release(ts);
		ts = cmyth_proginfo_rec_end(prog);
		cmyth_timestamp_to_string(end, ts);
		cmyth_release(ts);

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
		cmyth_release(description);
		mvpw_set_text_str(mythtv_channel, channame);
		cmyth_release(channame);
		mvpw_set_text_str(mythtv_date, str);
		mvpw_set_text_str(mythtv_record, ptr);

		mvpw_expose(mythtv_description);
		mvpw_expose(mythtv_channel);
		mvpw_expose(mythtv_date);
		mvpw_expose(mythtv_record);
		cmyth_release(pnd_list);
		cmyth_release(prog);
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

int
mythtv_pending(mvp_widget_t *widget)
{
	return mythtv_pending_filter(widget, mythtv_filter);
}

int
mythtv_pending_filter(mvp_widget_t *widget, mythtv_filter_t filter)
{
	cmyth_conn_t ctrl;
	cmyth_proglist_t pnd_list;
	int i, count, ret = 0;
	int days = 0, last_day = 0, displayed = 0;
	cmyth_proginfo_t prog = NULL;
	time_t t, rec_t, last_t = 0;
	struct tm *tm, rec_tm;
	char buf[64];
	char *filter_title = NULL;

	switch (filter) {
	case MYTHTV_FILTER_NONE:
		printf("do not filter pending schedule\n");
		break;
	case MYTHTV_FILTER_TITLE:
		printf("title filter pending schedule\n");
		filter_title = (char*)cmyth_proginfo_title(hilite_prog);
		break;
	case MYTHTV_FILTER_RECORD:
		printf("recording filter pending schedule\n");
		break;
	case MYTHTV_FILTER_RECORD_CONFLICT:
		printf("recording/conflict filter pending schedule\n");
		break;
	}

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}
	ctrl = cmyth_hold(control);

	busy_start();
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

	snprintf(buf, sizeof(buf), "Recording Schedule - %d day%s",
		 days, (days == 1) ? "" : "s");
	mvpw_set_menu_title(widget, buf);
	mvpw_clear_menu(widget);


	if ((pending_plist == NULL) || pending_dirty) {
		pnd_list = cmyth_proglist_get_all_pending(ctrl);
		CHANGE_GLOBAL_REF(pending_plist, pnd_list);
		if (pnd_list == NULL) {
			fprintf(stderr, "get pending failed\n");
			mythtv_shutdown(1);
			ret = -1;
			goto out;
		}
		pending_dirty = 0;
	} else {
		printf("Using cached pending data\n");
		pnd_list = cmyth_hold(pending_plist);
	}

	item_attr.select = NULL;
	item_attr.hilite = pending_hilite_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	count = cmyth_proglist_get_count(pnd_list);
	fprintf(stderr, "found %d pending recordings\n", count);
	for (i = 0; i < count; ++i) {
		char *title, *subtitle;
		cmyth_timestamp_t ts, te;
		cmyth_proginfo_rec_status_t status;
		int year, month, day, hour, minute;
		long card_id;
		char *type;
		char start[256], end[256];
		char buf[256], card[16];
		char *ptr;
		int display = 0;

		cmyth_release(prog);
		prog = cmyth_proglist_get_item(pending_plist, i);

		title = (char*)cmyth_proginfo_title(prog);
		subtitle = (char*)cmyth_proginfo_subtitle(prog);

		ts = cmyth_proginfo_rec_start(prog);
		cmyth_timestamp_to_string(start, ts);

		te = cmyth_proginfo_rec_end(prog);
		cmyth_timestamp_to_string(end, te);

		cmyth_release(ts);
		cmyth_release(te);

		status = cmyth_proginfo_rec_status(prog);

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
			goto release;

		card_id = cmyth_proginfo_card_id(prog);
		snprintf(card, sizeof(card), "%ld", card_id);

		switch (status) {
		case RS_RECORDING:
			item_attr.fg = mythtv_colors.pending_recording;
			type = card;
			break;
		case RS_WILL_RECORD:
			item_attr.fg = mythtv_colors.pending_will_record;
			type = card;
			break;
		case RS_CONFLICT:
			item_attr.fg = mythtv_colors.pending_conflict;
			type = "C";
			break;
		case RS_DONT_RECORD:
			item_attr.fg = mythtv_colors.pending_other;
			type = "X";
			break;
		case RS_TOO_MANY_RECORDINGS:
			item_attr.fg = mythtv_colors.pending_other;
			type = "T";
			break;
		case RS_PREVIOUS_RECORDING:
			item_attr.fg = mythtv_colors.pending_other;
			type = "P";
			break;
		case RS_LATER_SHOWING:
			item_attr.fg = mythtv_colors.pending_other;
			type = "L";
			break;
		case RS_EARLIER_RECORDING:
			item_attr.fg = mythtv_colors.pending_other;
			type = "E";
			break;
		case RS_REPEAT:
			item_attr.fg = mythtv_colors.pending_other;
			type = "r";
			break;
		case RS_CURRENT_RECORDING:
			item_attr.fg = mythtv_colors.pending_other;
			type = "R";
			break;
		default:
			item_attr.fg = mythtv_colors.pending_other;
			type = "?";
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

		switch (filter) {
		case MYTHTV_FILTER_NONE:
			display = 1;
			break;
		case MYTHTV_FILTER_TITLE:
			if (strcmp(title, filter_title) == 0)
				display = 1;
			break;
		case MYTHTV_FILTER_RECORD_CONFLICT:
			if (status == RS_CONFLICT)
				display = 1;
			/* fall through */
		case MYTHTV_FILTER_RECORD:
			if ((status == RS_RECORDING) ||
			    (status == RS_WILL_RECORD))
				display = 1;
			break;
		}

		if (display) {
			snprintf(buf, sizeof(buf),
				 "%.2d/%.2d  %.2d:%.2d   %s   %s  -  %s",
				 month, day, hour, minute, type,
				 title, subtitle);
			mvpw_add_menu_item(widget, buf, (void*)i, &item_attr);
			displayed++;
		}

		if ((rec_t > last_t) && (rec_tm.tm_mday != last_day)) {
			days++;
			last_day = rec_tm.tm_mday;
			last_t = rec_t;
		}

	release:
		cmyth_release(subtitle);
		cmyth_release(title);
	}
	cmyth_release(prog);
	if (filter_title)
		cmyth_release(filter_title);

	snprintf(buf, sizeof(buf),
		 "Recording Schedule - %d shows over %d day%s",
		 displayed, days, (days == 1) ? "" : "s");
	mvpw_set_menu_title(widget, buf);
 out:
	cmyth_release(ctrl);
	cmyth_release(pnd_list);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);
	pthread_mutex_unlock(&myth_mutex);
	busy_end();

	return ret;
}

static void*
wd_start(void *arg)
{
	cmyth_conn_t ctrl = cmyth_hold(control);
	int state = 0, old = 0;
	char err[] = "MythTV backend connection is not responding.";
	char ok[] = "MythTV backend connection has been restored.";

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	fprintf(stderr, "myth watchdog thread started (pid %d)\n", getpid());

	while (1) {
		if ((state=cmyth_conn_hung(ctrl)) == 1) {
			if (old == 0) {
				if ((gui_state == MVPMC_STATE_MYTHTV) ||
				    (hw_state == MVPMC_STATE_MYTHTV))
					gui_error(err);
			}
		} else {
			if (old == 1) {
				fprintf(stderr, "%s\n", ok);
				if ((gui_state == MVPMC_STATE_MYTHTV) ||
				    (hw_state == MVPMC_STATE_MYTHTV))
					gui_error_clear();
			}
		}
		old = state;
		sleep(1);
	}

	cmyth_release(ctrl);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) NULL}\n",
		    __FUNCTION__, __FILE__, __LINE__);
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
event_start(void *arg)
{
	char buf[128];
	cmyth_event_t next;
	pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;

	printf("event thread started (pid %d)\n", getpid());

	pthread_mutex_lock(&event_mutex);

	while (1) {
		while (event == NULL) {
			pthread_cond_wait(&event_cond, &event_mutex);
		}
		next = cmyth_event_get(event, buf, 128);
		switch (next) {
		case CMYTH_EVENT_UNKNOWN:
			printf("MythTV unknown event (error?)\n");
			sleep(1);
			break;
		case CMYTH_EVENT_CLOSE:
			printf("Event socket closed, shutting down myth\n");
			mythtv_shutdown(1);
			break;
		case CMYTH_EVENT_RECORDING_LIST_CHANGE:
			printf("MythTV event RECORDING_LIST_CHANGE\n");
			episode_dirty = 1;
			mvpw_expose(mythtv_browser);
			break;
		case CMYTH_EVENT_SCHEDULE_CHANGE:
			printf("MythTV event SCHEDULE_CHANGE\n");
			pending_dirty = 1;
			mvpw_expose(mythtv_browser);
			break;
		case CMYTH_EVENT_DONE_RECORDING:
			printf("MythTV event DONE_RECORDING\n");
			break;
		case CMYTH_EVENT_QUIT_LIVETV:
			printf("MythTV event QUIT_LIVETV\n");
			mythtv_livetv_stop();
			mythtv_shutdown(0);
			gui_error("Stopping LiveTV to start new recording.");
			break;
		case CMYTH_EVENT_LIVETV_CHAIN_UPDATE:
			printf("MythTV event %s\n",buf);
			mythtv_livetv_chain_update(buf);
			break;
		}
	}

	return NULL;
}

static void*
control_start(void *arg)
{
	int len = 0;
	int size = BSIZE;
	demux_attr_t *attr;
	pid_t pid;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	pid = getpid();
	signal(SIGURG, sighandler);

	while (1) {
		int count = 0;
		int audio_selected, audio_checks;

		pthread_mutex_lock(&mutex);
		fprintf(stderr, "mythtv control thread sleeping...(pid %d)\n",
			pid);
		while ((mythtv_file == NULL) && (mythtv_recorder == NULL)) {
			pthread_cond_wait(&myth_cond, &mutex);
		}
		fprintf(stderr, "Got stream recorder = %p, file = %p)\n",
			mythtv_recorder, mythtv_file);
		if (mythtv_file)
			printf("%s(): starting file playback\n", __FUNCTION__);
		if (mythtv_recorder)
			printf("%s(): starting rec playback\n", __FUNCTION__);

		pthread_mutex_unlock(&mutex);

		fprintf(stderr, "mythtv control thread starting...(pid %d)\n",
			pid);

		attr = demux_get_attr(handle);

		audio_selected = 0;
		audio_checks = 0;
		video_reading = 1;

		do {
			if (seeking || jumping) {
				size = 1024*96;
			} else {
				if ((attr->video.bufsz -
				     attr->video.stats.cur_bytes) < BSIZE) {
					if (paused) {
						usleep(1000);
						continue;
					}
					size = attr->video.bufsz -
						attr->video.stats.cur_bytes -
						1024;
				} else {
					size = BSIZE;
				}

				if (((mythtv_file == NULL) &&
				     (mythtv_recorder == NULL)) ||
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

			if (mythtv_livetv) {
				if(protocol_version >= 26)
					len = cmyth_livetv_request_block(mythtv_recorder,
								  	size);
				else
					len = cmyth_ringbuf_request_block(mythtv_recorder,
								  	size);
			}
			else
				len = cmyth_file_request_block(mythtv_file,
							       size);

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
				fprintf(stderr,
					"%s(): waiting to unpause...\n",
					__FUNCTION__);
				while (paused &&
				       (mythtv_file || mythtv_recorder) &&
				       !close_mythtv) {
					usleep(1000);
				}
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
					fprintf(stderr, "selected audio "
						"stream 0xc0\n");
					audio_selected = 1;
				} else if (audio_checks++ == 4) {
					fprintf(stderr, "audio stream "
						"0xc0 not found\n");
					audio_selected = 1;
				}
			}
		} while ((mythtv_file || mythtv_recorder) && (len > 0) &&
			 (playing_via_mythtv == 1) && (!close_mythtv));

		video_reading = 0;

		fprintf(stderr,
			"%s(): len %d playing_via_mythtv %d close_mythtv %d\n",
		       __FUNCTION__, len, playing_via_mythtv, close_mythtv);

		if (close_mythtv) {
			if (mythtv_recorder) {
				mythtv_livetv_stop();
			}
			if (mythtv_file) {
				mythtv_close_file();
			}
		}

		if ((len == -EPIPE) || (len == -ECONNRESET) ||
		    (len == -EBADF)) {
			mythtv_shutdown(1);
			continue;
		}

	}

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) NULL}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return NULL;
}

int
mythtv_init(char *server_name, int portnum)
{
	char *server = "127.0.0.1";
	int port = 6543;
	static int thread = 0;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_debug) {
		fprintf(stderr, "Turn on libcmyth debugging\n");
		cmyth_dbg_all();
	}

	if (server_name)
		server = server_name;
	if (portnum > 0)
		port = portnum;

	if ((control = cmyth_conn_connect_ctrl(server, port, 16*1024,
					       mythtv_tcp_control)) == NULL) {
		fprintf(stderr, "cannot connect to mythtv server %s\n",
			server);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	if ((event = cmyth_conn_connect_event(server, port, 16*1024,
					      mythtv_tcp_control)) == NULL) {
		fprintf(stderr, "cannot connect to mythtv server %s\n",
			server);
		return -1;
	}

	/* Setup the ever increasingly important mythconverg database */
	mythtv_database = cmyth_database_create();
	if(mythtv_database) {
		if(cmyth_database_set_host(mythtv_database, mysqlptr->host) == 0
		|| cmyth_database_set_user(mythtv_database, mysqlptr->user) == 0
		|| cmyth_database_set_pass(mythtv_database, mysqlptr->pass) == 0
		|| cmyth_database_set_name(mythtv_database, mysqlptr->db) == 0) {

			fprintf(stderr, "cannot allocate database structure contents\n");
			return -1;
		}
	}
	else {
		fprintf(stderr, "cannot allocate database structure\n");
		return -1;
	}

	pthread_cond_signal(&event_cond);

	if (!thread) {
		thread = 1;
		pthread_create(&control_thread, &thread_attr_small,
			       control_start, NULL);
		pthread_create(&wd_thread, &thread_attr_small, wd_start, NULL);
		pthread_create(&event_thread, &thread_attr_small,
			       event_start, NULL);
	}

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

void
mythtv_program(mvp_widget_t *widget)
{
	char *program;
	cmyth_timestamp_t ts;
	char start[256], end[256], str[256], *ptr, *chansign;
	cmyth_proginfo_t loc_prog = cmyth_hold(current_prog);
		
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (loc_prog) {
		char *description =
			(char *)cmyth_proginfo_description(loc_prog);;
		char *title = (char *) cmyth_proginfo_title(loc_prog);
		char *subtitle = (char *) cmyth_proginfo_subtitle(loc_prog);
		
		if (mythtv_livetv) {
			chansign = (char*)cmyth_proginfo_chansign(loc_prog);
			
			ts = cmyth_proginfo_start(loc_prog);
			cmyth_timestamp_to_string(start, ts);
			cmyth_release(ts);
			ts = cmyth_proginfo_end(loc_prog);
			cmyth_timestamp_to_string(end, ts);
			cmyth_release(ts);
		
			ptr = strchr(start, 'T');
			*ptr = '\0';
			sprintf(str, "%5.5s - ", ptr+1);
			ptr = strchr(end, 'T');
			*ptr = '\0';
			strncat(str, ptr+1,5);
		
			program = alloca(strlen(chansign) + strlen(str) +
					 strlen(title) + strlen(subtitle) +
					 16);
			sprintf(program, "[%s] %s %s - %s",
				chansign, str, title, subtitle);
			cmyth_release(chansign);
		} else {					
			program = alloca(strlen(title) +
					 strlen(subtitle) + 16);
			sprintf(program, "%s - %s", title, subtitle);
		}
		
		mvpw_set_text_str(mythtv_osd_description, description);
		mvpw_set_text_str(mythtv_osd_program, program);
		cmyth_release(title);
		cmyth_release(subtitle);
		cmyth_release(description);
	}
	cmyth_release(loc_prog);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

void
mythtv_stop(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	busy_start();

	pthread_mutex_lock(&myth_mutex);
	mythtv_close_file();
	pthread_mutex_unlock(&myth_mutex);

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
	busy_end();
}

static int
mythtv_delete_prog(int forget)
{
	int ret;
	cmyth_conn_t ctrl = cmyth_hold(control);
	cmyth_proginfo_t hi_prog = cmyth_hold(hilite_prog);
	cmyth_proginfo_t loc_prog = cmyth_hold(current_prog);	

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	busy_start();

	pthread_mutex_lock(&myth_mutex);
	cmyth_proglist_delete_item(episode_plist, hi_prog);
	if (forget)
		ret = cmyth_proginfo_forget_recording(ctrl, hi_prog);
	else
		ret = cmyth_proginfo_delete_recording(ctrl, hi_prog);
	if (cmyth_proginfo_compare(hi_prog, loc_prog) == 0) {
		mythtv_close_file();
		video_clear();
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
	}
	pthread_mutex_unlock(&myth_mutex);

	cmyth_release(ctrl);
	cmyth_release(hi_prog);
	cmyth_release(loc_prog);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
	busy_end();

	return ret;
}

int
mythtv_delete(void)
{
	return mythtv_delete_prog(0);
}

int
mythtv_forget(void)
{
	return mythtv_delete_prog(1);
}

int
mythtv_proginfo(char *buf, int size)
{
	cmyth_timestamp_t ts;
	char airdate[256], start[256], end[256];
	char *ptr;
	char *title, *subtitle, *description, *category, *channame;
	char *seriesid, *programid, *stars, *recgroup;
	cmyth_proginfo_t hi_prog = cmyth_hold(hilite_prog);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	ts = cmyth_proginfo_originalairdate(hi_prog);
	cmyth_timestamp_to_string(airdate, ts);
	cmyth_release(ts);
	ts = cmyth_proginfo_rec_start(hi_prog);
	cmyth_timestamp_to_string(start, ts);
	cmyth_release(ts);
	ts = cmyth_proginfo_rec_end(hi_prog);
	cmyth_timestamp_to_string(end, ts);
	cmyth_release(ts);

	if ((ptr=strchr(airdate, 'T')) != NULL)
		*ptr = '\0';
	if ((ptr=strchr(start, 'T')) != NULL)
		*ptr = ' ';
	if ((ptr=strchr(end, 'T')) != NULL)
		*ptr = ' ';

	title = cmyth_proginfo_title(hi_prog);
	subtitle = cmyth_proginfo_subtitle(hi_prog);
	description = cmyth_proginfo_description(hi_prog);
	category = cmyth_proginfo_category(hi_prog);
	channame = cmyth_proginfo_channame(hi_prog);
	seriesid = cmyth_proginfo_seriesid(hi_prog);
	programid = cmyth_proginfo_programid(hi_prog);
	stars = cmyth_proginfo_stars(hi_prog);
	recgroup = cmyth_proginfo_recgroup(hi_prog);
	snprintf(buf, size,
		 "Title: %s\n"
		 "Subtitle: %s\n"
		 "Description: %s\n"
		 "Start: %s\n"
		 "End: %s\n"
		 "Original Air Date: %s\n"
		 "Category: %s\n"
		 "Recording Group: %s\n"
		 "Channel: %s\n"
		 "Series ID: %s\n"
		 "Program ID: %s\n"
		 "Stars: %s\n",
		 title,
		 subtitle,
		 description,
		 start,
		 end,
		 airdate,
		 category,
		 recgroup,
		 channame,
		 seriesid,
		 programid,
		 stars);
	cmyth_release(title);
	cmyth_release(subtitle);
	cmyth_release(description);
	cmyth_release(category);
	cmyth_release(channame);
	cmyth_release(seriesid);
	cmyth_release(programid);
	cmyth_release(stars);
	cmyth_release(recgroup);
	cmyth_release(hi_prog);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

void
mythtv_cleanup(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	fprintf(stderr, "stopping all video playback...\n");
	
	if (mythtv_file) {
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

	fprintf(stderr, "cleanup mythtv data structures\n");

	CHANGE_GLOBAL_REF(pending_plist, NULL);
	CHANGE_GLOBAL_REF(episode_plist, NULL);

	running_mythtv = 0;
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

int
mythtv_open(void)
{
	char *host;
	int port = 6543;
	cmyth_proginfo_t loc_prog = cmyth_hold(current_prog);
	cmyth_conn_t c = NULL;
	cmyth_file_t f = NULL;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (loc_prog == NULL) {
		fprintf(stderr, "%s: no program\n", __FUNCTION__);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	if ((host = cmyth_proginfo_host(loc_prog)) == NULL) {
		fprintf(stderr, "unknown myth backend\n");
		cmyth_release(loc_prog);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	printf("connecting to mythtv (slave) backend %s\n", host);
	CHANGE_GLOBAL_REF(mythtv_file, NULL);
	if ((c = cmyth_conn_connect_ctrl(host, port, 1024, mythtv_tcp_control))
	    == NULL) {
		cmyth_release(loc_prog);
		cmyth_release(host);
		mythtv_shutdown(1);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}
	cmyth_release(host);

	playing_via_mythtv = 1;

	if ((f = cmyth_conn_connect_file(loc_prog, c, BSIZE,
					    mythtv_tcp_program)) == NULL) {
		cmyth_release(loc_prog);
		video_clear();
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
		gui_error("Cannot connect to file!");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}
	CHANGE_GLOBAL_REF(mythtv_file, f);
	cmyth_release(f);
	cmyth_release(c);
	cmyth_release(loc_prog);

	fprintf(stderr, "starting mythtv file transfer\n");

	playing_file = 1;

	pthread_cond_signal(&myth_cond);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

long long
mythtv_seek(long long offset, int whence)
{
	struct timeval to;
	int count = 0;
	long long seek_pos = -1, size;
	cmyth_file_t f = cmyth_hold(mythtv_file);
	cmyth_recorder_t r = cmyth_hold(mythtv_recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_livetv) {
		if(protocol_version >= 26)
			seek_pos = cmyth_livetv_seek(r, 0, SEEK_CUR);
		else
			seek_pos = cmyth_ringbuf_seek(r, 0, SEEK_CUR);
	}
	else
		seek_pos = cmyth_file_seek(f, 0, SEEK_CUR);
	if ((offset == 0) && (whence == SEEK_CUR)) {
		goto out;
	}

	if (!mythtv_livetv) {
		size = mythtv_size();
		if (size < 0) {
			fprintf(stderr, "seek failed, stream size unknown\n");
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
			if(protocol_version >= 26) {
				if (cmyth_livetv_select(r, &to) > 0) {
					PRINTF("%s(): reading...\n", __FUNCTION__);
					len = cmyth_livetv_get_block(r, buf,
							     	sizeof(buf));
					PRINTF("%s(): read returned %d\n",
				       	__FUNCTION__, len);
				}
			}
			else {
				if (cmyth_ringbuf_select(r, &to) > 0) {
					PRINTF("%s(): reading...\n", __FUNCTION__);
					len = cmyth_ringbuf_get_block(r, buf,
							     	sizeof(buf));
					PRINTF("%s(): read returned %d\n",
				       	__FUNCTION__, len);
				}
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
		if(protocol_version >= 26)
			seek_pos = cmyth_livetv_seek(r, offset, whence);
		else
			seek_pos = cmyth_ringbuf_seek(r, offset, whence);
	else
		seek_pos = cmyth_file_seek(f, offset, whence);

	PRINTF("%s(): pos %lld\n", __FUNCTION__, seek_pos);

	pthread_mutex_unlock(&myth_mutex);
	pthread_mutex_unlock(&seek_mutex);

 out:
	cmyth_release(r);
	cmyth_release(f);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %lld}\n",
		  __FUNCTION__, __FILE__, __LINE__, seek_pos);
	return seek_pos;
}

int
mythtv_read(char *buf, int len)
{
	int ret = -EBADF;
	int tot = 0;
	cmyth_file_t f = cmyth_hold(mythtv_file);
	cmyth_recorder_t r = cmyth_hold(mythtv_recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);

	pthread_mutex_lock(&seek_mutex);
	pthread_cond_signal(&myth_cond);

	PRINTF("cmyth getting block of size %d\n", len);
	while ((f || r) && (tot < len) && !myth_seeking) {
		struct timeval to;

		to.tv_sec = 0;
		to.tv_usec = 10;
		ret = -EBADF;
		if (mythtv_livetv) {
			if(protocol_version >= 26) {
				if (cmyth_livetv_select(r, &to) <= 0) {
					break;
				}
				ret = cmyth_livetv_get_block(r, buf+tot, len-tot);
			}
			else {
				if (cmyth_ringbuf_select(r, &to) <= 0) {
					break;
				}
				ret = cmyth_ringbuf_get_block(r, buf+tot, len-tot);
			}
		} else {
			if (cmyth_file_select(f, &to) <= 0) {
				break;
			}
			ret = cmyth_file_get_block(f, buf+tot, len-tot);
		}
		if (ret <= 0) {
			if (tot == 0)
				tot = ret;
			fprintf(stderr, "%s: get block failed\n",
				__FUNCTION__);
			break;
		}
		tot += ret;
	}

	cmyth_release(f);
	cmyth_release(r);
	PRINTF("cmyth got block of size %d (out of %d)\n", tot, len);

	pthread_mutex_unlock(&seek_mutex);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, tot);
	return tot;
}

long long
mythtv_size(void)
{
	static int unchanged = 0;
	static long long size = 0;
	static struct timeval last = { 0, 0 };
	static cmyth_proginfo_t prog = NULL;
	struct timeval now;
	long long ret;
	static volatile int failed = 0;
	cmyth_proginfo_t loc_prog = cmyth_hold(current_prog), new_prog = NULL;
	cmyth_conn_t ctrl = cmyth_hold(control);

	gettimeofday(&now, NULL);

	/*
	 * This is expensive, so use the cached value if less than 30 seconds
	 * have elapsed since the last try, and we are still playing the
	 * same recording.
	 */
	if ((prog == loc_prog) && ((now.tv_sec - last.tv_sec) < 30)) {
		ret = cmyth_proginfo_length(loc_prog);
		goto out;
	}
	/*
	 * If the size value is not changing, then the recording is not
	 * still in-progress, and we can simply believe the current value.
	 */
	if ((prog == loc_prog) && unchanged) {
		ret = cmyth_proginfo_length(loc_prog);
		goto out;
	}
	if (prog != loc_prog) {
		size = 0;
		unchanged = 0;
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	pthread_mutex_lock(&myth_mutex);

	new_prog = cmyth_proginfo_get_detail(ctrl, loc_prog);
	/*
	 * If get detail fails, use the value in the file structure.
	 */
	if (new_prog == NULL) {
		if (!failed) {
			fprintf(stderr,
				"%s(): cmyth_proginfo_get_detail() failed!\n",
				__FUNCTION__);
			failed = 1;
		}
		ret = cmyth_file_length(mythtv_file);
		goto unlock;
	}

	failed = 0;

	ret = cmyth_proginfo_length(new_prog);

	if (ret == size) {
		unchanged = 1;
	} else {
		size = ret;
	}

	memcpy(&last, &now, sizeof(last));
	CHANGE_GLOBAL_REF(current_prog, new_prog);
	CHANGE_GLOBAL_REF(prog, current_prog);

 unlock:
	pthread_mutex_unlock(&myth_mutex);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %lld}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);

 out:
	cmyth_release(new_prog);
	cmyth_release(loc_prog);
	cmyth_release(ctrl);

	return ret;
}

void
mythtv_atexit(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	fprintf(stderr, "%s(): start exit processing...\n", __FUNCTION__);

	pthread_mutex_lock(&seek_mutex);
	pthread_mutex_lock(&myth_mutex);

	if (mythtv_livetv) {
		mythtv_livetv_stop();
		mythtv_livetv = 0;
	}

	mythtv_close();

	fprintf(stderr, "%s(): end exit processing...\n", __FUNCTION__);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);

	cmyth_alloc_show();
}

int
mythtv_program_runtime(void)
{
	int seconds;
	cmyth_proginfo_t loc_cur = cmyth_hold(current_prog);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	seconds = cmyth_proginfo_length_sec(loc_cur);
	cmyth_release(loc_cur);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, seconds);

	return seconds;
}

void
mythtv_exit(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_livetv) {
		mythtv_livetv_stop();
	} else {
		mythtv_stop();
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

void
mythtv_test_exit(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (!playing_file)
		mythtv_close();
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

void 
mythtv_schedule_recording(mvp_widget_t *widget, char *item , void *key, int type)
{
	int which = (int) mvpw_menu_get_hilite(widget);
	char buf[32];
	char query[700];
	char query1[700];
	char query2[500];
	char starttime[10];
	char startdate[10];
	char endtime[10];
	char enddate[10];
	char *n;
	char cp[25];
	char msg[45];
	char guierrormsg[45];
	const char delimiters[] = " ";
	int err=0;
	int rec_id=0;
	cmyth_conn_t ctrl = cmyth_hold(control);
	char *string;
	unsigned int len;
	int rcrd = sqlprog[which].recording;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);
	fprintf(stderr,"DB version = %d\n",mysqlptr->version);
	switch (which) {
		default:
			fprintf(stderr, "Recording status = %d\n", sqlprog[which].recording);
			if(rcrd == 1 || rcrd == 2 || rcrd == 10 ) {
				fprintf(stderr, "MythTV - Episode already scheduled or recorded, do not duplicate\n");
				break;
			}
			strcpy(cp,sqlprog[which].starttime);
			n =  strtok (cp,delimiters);
			strcpy(startdate, n);
			n =  strtok (NULL,delimiters);
			strcpy(starttime, n);

			strcpy(cp,sqlprog[which].endtime);
			n =  strtok (cp,delimiters);
			strcpy(enddate, n);
			n =  strtok (NULL,delimiters);
			strcpy(endtime, n);
			
		       	len = strlen(sqlprog[which].seriesid);
			string=malloc(len * sizeof(char));

			string = mysql_escape_chars(sqlprog[which].seriesid,mysqlptr->host,mysqlptr->user,mysqlptr->pass, mysqlptr->db);
 
			fprintf(stderr, "Seriesid = %s\n",string);

			switch (mysqlptr->version) {

			case 15:
				sprintf(query, "REPLACE INTO record ( \
					recordid,type,chanid,starttime,startdate,endtime, \
					enddate,\
					title,\
					subtitle, \
					description, \
					category,profile,recpriority,autoexpire,maxepisodes, \
					maxnewest, startoffset,endoffset,recgroup,dupmethod,dupin, \
					station,\
					seriesid,programid,search, autotranscode,autocommflag,autouserjob1,autouserjob2,autouserjob3, \
					autouserjob4, findday,findtime,findid, inactive, parentid) values \
					(NULL,'%d','%ld','%s','%s','%s', \
					'%s',", \
					type, sqlprog[which].chanid,starttime,startdate,endtime, \
					enddate);
				sprintf(query1, " \
					,'%s','Default','0','0','0', \
					'0','0','0','Default','6','15',", \
					sqlprog[which].category); 
				sprintf(query2,",'%s','%s','0','0','1','0','0','0', \
					'0','5','%s','732800.33333333','0','0')", \
					sqlprog[which].seriesid,sqlprog[which].programid,starttime );
				break;
			case 20:
				sprintf(guierrormsg, "No MythTV SQL support\nMythTV version: %d\n", mysqlptr->version);
				gui_error(guierrormsg);
				break;
			case 26:
				sprintf(query, "REPLACE INTO record ( \
					recordid,type,chanid,starttime,startdate,endtime, \
					enddate,search,\
					title,\
					subtitle, \
					description, \
					profile,recpriority,category,maxnewest,inactive,maxepisodes, \
					autoexpire,startoffset,endoffset,recgroup,dupmethod,dupin, \
					station,\
					seriesid,programid,autocommflag,findday,findtime,findid, \
					autotranscode,transcoder,tsdefault,autouserjob1,autouserjob2,autouserjob3, \
					autouserjob4) values \
					(NULL,'%d','%ld','%s','%s','%s', \
					'%s','',", \
					type, sqlprog[which].chanid,starttime,startdate,endtime, \
					enddate);
				sprintf(query1, " \
					,'Default','0','','0','0','0', \
					'0','0','0','Default','6','15',"); 
				sprintf(query2,",'%s','%s','1','5','%s','732800.33333333', \
					'0','0','1.00','0','0','0', \
					'0')", \
					sqlprog[which].seriesid,sqlprog[which].programid,starttime );
				break;
			default:
				sprintf(guierrormsg, "No MythTV SQL support\nMythTV version: %d\n", mysqlptr->version);
				gui_error(guierrormsg);
				goto out;	
				break;
			}
	                cmyth_dbg(CMYTH_DBG_DEBUG, "%s query = [%s]\n %d", __FUNCTION__, query,err);

			if ((rec_id=insert_into_record_mysql(query,query1,query2,sqlprog[which].title,sqlprog[which].subtitle,sqlprog[which].description,sqlprog[which].callsign,mysqlptr->host,mysqlptr->user,mysqlptr->pass, mysqlptr->db) ) <= 0 ) {
        		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
               			__FUNCTION__, __FILE__, __LINE__);
			goto err;
			} 
			else {
				sprintf(msg,"RESCHEDULE_RECORDINGS %d",rec_id);
				if ((err=cmyth_schedule_recording(ctrl,msg))<0){
					cmyth_dbg(CMYTH_DBG_ERROR,
					"%s: cmyth_sschedule_recording() failed (%d)\n", 
					__FUNCTION__,err);
					fprintf (stderr, "Error scheduling recording : %d\n",err);
					goto err;
				} 
				sqlprog[which].recording = 1;
			} 
			break;
		} /* first switch */

	if ( sqlprog[which].recording ) {
		sprintf(buf, "Recording Scheduled\n");
		mvpw_set_text_str(program_info_widget, buf);
		mvpw_show(program_info_widget);
		item_attr.fg = mythtv_colors.pending_will_record;	
		mvpw_menu_set_item_attr(widget, key, &item_attr);
	}
	out:
		cmyth_release(ctrl);
		return;

	err:
		sprintf(buf, "ERROR-Recording NOT Scheduled\n");
		mvpw_set_text_str(program_info_widget, buf);
		mvpw_show(program_info_widget);
		cmyth_release(ctrl);
		return;
}

extern void mythtv_schedule_recording(mvp_widget_t*, char *item , void *key, int type);

static void
schedule_recording_callback(mvp_widget_t *widget, char *item , void *key)
{
	int which = (int)key;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);
	fprintf(stderr,"DB version = %d\n",mysqlptr->version);
	switch (which) {
		case 0:
			mythtv_guide_menu_next(widget);
			item_attr.fg = mythtv_colors.menu_item;	
			break;
		case 1:
			mythtv_guide_menu_previous(widget);
			item_attr.fg = mythtv_colors.menu_item;	
			break;
		default:
			/* Record only this showing */
			mythtv_schedule_recording(widget, item , key, 1);
		break;
		} /* first switch */
	return;
}

static void
hilite_schedule_recording_callback(mvp_widget_t *widget, char *item , void *key, int hilite)
{
	int which = (int)key;
	char buf[550];
	char record_message[25];
	char startstring[50];
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace)  which =%d\n",
		__FUNCTION__, __FILE__, __LINE__, which);

	switch (which) {
		case 0:
			break;
		case 1:
			break;
		default:
			switch (sqlprog[which].recording) {
				case 1:
					sprintf(record_message, "Recording");
					break;
				case 2:
					sprintf(record_message, "Scheduled to record");
					break;
				case 3:
					sprintf(record_message, "Conflict");
					break;
				case 4:
					sprintf(record_message, "Do not record");
					break;
				case 5:
					sprintf(record_message, "Too many recordings");
					break;
				case 6:
					sprintf(record_message, "Previous recording");
					break;
				case 7:
					sprintf(record_message, "Later showing");
					break;
				case 8:
					sprintf(record_message, "Earlier recording");
					break;
				case 9:
					sprintf(record_message, "Repeat");
					break;
				case 10:
					sprintf(record_message, "Current recording");
					break;
				default:
					sprintf(record_message, "Not Scheduled to record");
			}

			sprintf(startstring, "%s - %s",sqlprog[which].starttime,sqlprog[which].endtime);
			sprintf(buf, "%s\n%s\n%d - %s  - Tuner %d\n%s",record_message,startstring,sqlprog[which].channum,sqlprog[which].callsign,sqlprog[which].sourceid,sqlprog[which].description);
			mvpw_set_text_str(program_info_widget, buf);
			mvpw_show(program_info_widget);
			break;
	}
}

int 
myth_sql_program_info(time_t now, int sqlcount, int all)
{
	int i, j,count;
        time_t rec_t, aheadtime;
	char starttime[25];
        struct tm rec_tm;
        cmyth_proginfo_t prog = NULL;
        cmyth_proglist_t pnd_list;

        cmyth_conn_t ctrl;

        ctrl = cmyth_hold(control);

	cmyth_dbg(CMYTH_DBG_DEBUG,"FUNCTION:%s  sqlcount=%d\n",__FUNCTION__, sqlcount);
	aheadtime = now;
	aheadtime=aheadtime + PROGRAM_ADJUST;

        if ((pending_plist == NULL) || pending_dirty) {
                pnd_list = cmyth_proglist_get_all_pending(ctrl);
                CHANGE_GLOBAL_REF(pending_plist, pnd_list); 
                if (pnd_list == NULL) {
                        fprintf(stderr, "get pending failed\n");
			mythtv_shutdown(1);
			return -1; 
                }
                pending_dirty = 0;
        }
        else {
                /* fprintf(stderr, "Using cached pending data -- %s\n", __FUNCTION__); */
                pnd_list = cmyth_hold(pending_plist);
        }
        count = cmyth_proglist_get_count(pnd_list);

        for (i = 0; i < count; ++i) {
                char *title, *subtitle;
                char *seriesid, *programid;
                long channel_id;
                cmyth_timestamp_t ts, te;
                cmyth_proginfo_rec_status_t status;
                int year, month, day, hour, minute;
                long card_id;
                char start[256], end[256];
                char card[16];
                char *ptr;
                cmyth_release(prog);

                prog = cmyth_proglist_get_item(pending_plist, i);
                title = (char*)cmyth_proginfo_title(prog);
                subtitle = (char*)cmyth_proginfo_subtitle(prog);
                channel_id = (long)cmyth_proginfo_chan_id(prog);
		seriesid = (char*)cmyth_proginfo_seriesid(prog);
		programid = (char*)cmyth_proginfo_programid(prog);

                ts = cmyth_proginfo_rec_start(prog);
                cmyth_timestamp_to_string(start, ts);

                te = cmyth_proginfo_rec_end(prog);
                cmyth_timestamp_to_string(end, te);

                cmyth_release(ts);
                cmyth_release(te);

                status = cmyth_proginfo_rec_status(prog);

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

                if (rec_t < now) { goto release; }
                if (rec_t > aheadtime && ! all) { goto release; } 

		/* Now get start time for program comparison */
                year = atoi(start);
                ptr = strchr(start, '-');
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

		strftime(starttime,25, "%Y-%m-%d %H:%M:%S", &rec_tm);

                card_id = cmyth_proginfo_card_id(prog);
                snprintf(card, sizeof(card), "%ld", card_id);

                for (j=0; j<sqlcount; j++) {
                        if ( (strcmp(sqlprog[j].title,title) == 0) && 
				(sqlprog[j].chanid == channel_id) &&
				(strcmp(sqlprog[j].seriesid, seriesid) ==0) &&
				(strcmp(sqlprog[j].programid, programid) == 0) && 
				(strcmp(sqlprog[j].starttime, starttime) ==0) ) {
				fprintf(stderr, "Title (%d-%d): %s,\t\tStatus: %d\nStartSQL %s\t\tStartMyth %s\n", i, j, title, status, sqlprog[j].starttime, starttime);
                                switch (status) {
                                        case RS_RECORDING:
                                                sqlprog[j].recording=1;
                                                break;
                                        case RS_WILL_RECORD:
                                                sqlprog[j].recording=2;
                                                break;
                                        case RS_CONFLICT:
                                                sqlprog[j].recording=3;
                                                break;
                                        case RS_DONT_RECORD:
                                                sqlprog[j].recording=4;
                                                break;
                                        case RS_TOO_MANY_RECORDINGS:
                                                sqlprog[j].recording=5;
                                                break;
                                        case RS_PREVIOUS_RECORDING:
                                                sqlprog[j].recording=6;
                                                break;
                                        case RS_LATER_SHOWING:
                                                sqlprog[j].recording=7;
                                                break;
                                        case RS_EARLIER_RECORDING:
                                                sqlprog[j].recording=8;
                                                break;
                                        case RS_REPEAT:
                                                sqlprog[j].recording=9;
                                                break;
                                        case RS_CURRENT_RECORDING:
                                                sqlprog[j].recording=10;
                                                break;
                                        default:
                                                sqlprog[j].recording=99;
                                                break;
                                }
                        }
                }

        release:
                cmyth_release(subtitle);
                cmyth_release(title); 

        }
        cmyth_release(pnd_list);
	cmyth_release(ctrl);
        return 0;
}

static int
schedule_compare (const void *a, const void *b) 
{
	const struct program *x, *y;
	int X, Y;
	
	x = a;
	y = b;
	X = x->channum;
	Y = y->channum;

	if (X < Y) {
		return -1;
	} 
	else if (X > Y) {
		return 1;
	}
	else {
		return 0;
	}
}

int
mythtv_guide_menu_previous(mvp_widget_t *widget) 
{
        int count=0,sqlcount=0;
        int i=0;
        char buf[256];
        char starttime[25], endtime[25];
        const char delimiters[] = " :-";
	

        struct tm unixdate;
        char cp[25];
        int year,mon,day,hour;
        time_t t;
        struct tm *loctime_ahead;
        char *n;

        cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
                 __FUNCTION__, __FILE__, __LINE__);

        strcpy(endtime,sqlprog[0].starttime);
        strcpy (starttime,endtime);
        strcpy(cp,starttime);

        n =  strtok (cp, delimiters);
        year = atoi (n);
        n =  strtok (NULL, delimiters);
        mon = atoi (n);
        n  =  strtok (NULL, delimiters);
        day = atoi (n);
        n =  strtok (NULL, delimiters);
        hour = atoi (n);

        memset(&unixdate, 0, sizeof(unixdate));
        unixdate.tm_year = year - 1900;
        unixdate.tm_mon  = mon - 1;
        unixdate.tm_mday = day;
        unixdate.tm_hour = hour;
        unixdate.tm_min  = 0;
        unixdate.tm_sec  = 0;

        t = mktime(&unixdate);
	t=t-(PROGRAM_ADJUST*2);
        loctime_ahead=localtime(&t);
        strftime(starttime,256, "%Y-%m-%d %H:00:00", loctime_ahead);

        item_attr.select = schedule_recording_callback;
        item_attr.hilite = hilite_schedule_recording_callback;
        item_attr.fg = mythtv_attr.fg;
        item_attr.bg = mythtv_attr.bg;

        mvpw_show(root);
        mvpw_expose(root);

        if (mythtv_verify() < 0) {
                cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
                        __FUNCTION__, __FILE__, __LINE__);
                return -1;
        }

        busy_start();
	pthread_mutex_lock(&myth_mutex);
	if (sqlprog != NULL) {
		free (sqlprog);
		sqlprog=NULL;
	}
	if ((sqlprog=malloc(sizeof(struct program)*50))==NULL) {
		perror("malloc");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mallor Error.\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		goto out;
	}

        sqlcount=get_guide_mysql(&sqlprog,starttime,endtime,mysqlptr->host, mysqlptr->user, mysqlptr->pass,mysqlptr->db);
        if (sqlcount < 0) {
                cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
                        __FUNCTION__, __FILE__, __LINE__);
		snprintf(buf, sizeof(buf),"Database Error.  Please check your settings\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		free (sqlprog);
		goto out;
        }
	if (myth_sql_program_info(t,sqlcount, 0) <0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "error returned from %s [#%s] line: %d\n",
			__FUNCTION__ ,count,__LINE__); 
	} 
        add_osd_widget(mythtv_program_widget, OSD_PROGRAM, osd_settings.program, NULL);
        snprintf(buf, sizeof(buf),"Recordings %s - %s",starttime,endtime);
        mvpw_set_menu_title(widget, buf);
        mvpw_clear_menu(widget);
        snprintf(buf, sizeof(buf),"Get Next Hour");
        mvpw_add_menu_item(widget, buf , (void*)0, &item_attr);
        snprintf(buf, sizeof(buf),"Get Previous Hour");
        mvpw_add_menu_item(widget, buf , (void*)1, &item_attr);
	count = sqlcount;
	qsort(sqlprog,sqlcount,sizeof(*sqlprog),schedule_compare);
        for (i=1; i<count; i++) {
		switch (sqlprog[i].recording) {
			case 1:
				item_attr.fg = mythtv_colors.pending_recording;
				break;
			case 2:
				item_attr.fg = mythtv_colors.pending_will_record;
				break;
			case 3:
				item_attr.fg = mythtv_colors.pending_conflict;
				break;
			case 4:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 5:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 6:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 7:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 8:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 9:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 10:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 99:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			default:
				item_attr.fg = mythtv_attr.fg;
		}
		snprintf(buf, sizeof(buf),"%d (%s): %s - %s",sqlprog[i].channum,sqlprog[i].callsign,sqlprog[i].title,sqlprog[i].subtitle); 
                mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
        }
	out:
        	mvpw_show(widget);
		mvpw_focus(widget);
		pthread_mutex_unlock(&myth_mutex);
	        busy_end();
		return 0;
}

int
mythtv_guide_menu_next(mvp_widget_t *widget) 
{
	int sqlcount=0;
	int i=0;
	int count=0;
	char buf[256];
	char starttime[25], endtime[25];
	const char delimiters[] = " :-";


        struct tm unixdate;
	char cp[25];
	int year,mon,day,hour;
	time_t t;
        struct tm *loctime_ahead;
	char *n;

	strcpy(endtime,sqlprog[0].endtime);	
	strcpy (starttime,endtime);
	strcpy(cp,endtime); 

	n =  strtok (cp, delimiters);
	year = atoi (n);
	n =  strtok (NULL, delimiters);
	mon = atoi (n); 
	n  =  strtok (NULL, delimiters);
	day = atoi (n);
	n =  strtok (NULL, delimiters);
	hour = atoi (n);

	memset(&unixdate, 0, sizeof(unixdate));
	unixdate.tm_year = year - 1900;
	unixdate.tm_mon  = mon - 1;
	unixdate.tm_mday = day;
	unixdate.tm_hour = hour;
	unixdate.tm_min  = 0;
	unixdate.tm_sec  = 0;

	t = mktime(&unixdate);
        loctime_ahead=localtime(&t);
        strftime(endtime,256, "%Y-%m-%d %H:00:00", loctime_ahead);

	item_attr.select = schedule_recording_callback;
	item_attr.hilite = hilite_schedule_recording_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	mvpw_show(root);
	mvpw_expose(root);

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		return -1;
	}
	
	busy_start();
	pthread_mutex_lock(&myth_mutex);
	if (sqlprog != NULL) {
		free (sqlprog);
		sqlprog=NULL;
	}
	if ((sqlprog=malloc(sizeof(struct program)*50))==NULL) {
		perror("malloc");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mallor Error.\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		goto out;
	}

        sqlcount=get_guide_mysql(&sqlprog,starttime,endtime,mysqlptr->host, mysqlptr->user, mysqlptr->pass,mysqlptr->db); 

	if (sqlcount < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Database Error.  Please check your settings\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		free (sqlprog);
		goto out;
	}

	if (myth_sql_program_info(t,sqlcount, 0) <0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "error returned from %s [#%s] line: %d\n",
			__FUNCTION__ ,sqlcount,__LINE__); 
	}

	add_osd_widget(mythtv_program_widget, OSD_PROGRAM, osd_settings.program, NULL);
	snprintf(buf, sizeof(buf),"Recordings %s - %s",starttime,endtime);
	mvpw_set_menu_title(widget, buf);
	mvpw_clear_menu(widget);
	snprintf(buf, sizeof(buf),"Get Next Hour");
	mvpw_add_menu_item(widget, buf , (void*)0, &item_attr);
	snprintf(buf, sizeof(buf),"Get Previous Hour");
	mvpw_add_menu_item(widget, buf , (void*)1, &item_attr);
	count = sqlcount;
	qsort(sqlprog,sqlcount,sizeof(*sqlprog),schedule_compare);
	for (i=1; i<sqlcount; i++) {
		switch (sqlprog[i].recording) {
			case 1:
				item_attr.fg = mythtv_colors.pending_recording;
				break;
			case 2:
				item_attr.fg = mythtv_colors.pending_will_record;
				break;
			case 3:
				item_attr.fg = mythtv_colors.pending_conflict;
				break;
			case 4:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 5:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 6:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 7:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 8:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 9:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 10:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 99:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			default:
				item_attr.fg = mythtv_attr.fg;
		}
		snprintf(buf, sizeof(buf),"%d (%s): %s - %s",sqlprog[i].channum,sqlprog[i].callsign,sqlprog[i].title,sqlprog[i].subtitle); 
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
	}
	out:
		mvpw_show(widget);
		mvpw_focus(widget);
		pthread_mutex_unlock(&myth_mutex);
		busy_end();
		return 0;
}

int
myth_check_version(void)
{
	int version=0;
        cmyth_conn_t ctrl = cmyth_hold(control);
	
	version=get_myth_version(ctrl);
	return version;	
}

int
mythtv_guide_menu(mvp_widget_t *widget, mvp_widget_t *widget2) 
{
	int i=0,sqlcount,count;
	char buf[256];
	char starttime[25], endtime[25];
        struct tm *loctime;
        struct tm *loctime_ahead;
        time_t aheadtime;

        curtime=time(NULL);
        loctime=localtime(&curtime);
        strftime(starttime,256, "%Y-%m-%d %H:00:00", loctime);

        aheadtime=time(NULL);
        aheadtime=aheadtime + PROGRAM_ADJUST;
        loctime_ahead=localtime(&aheadtime);
        strftime(endtime,256, "%Y-%m-%d %H:00:00", loctime_ahead);

	item_attr.select = schedule_recording_callback;
	item_attr.hilite = hilite_schedule_recording_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	mvpw_show(root);
	mvpw_expose(root);

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mythtv Server Error\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		goto out;
	}
	busy_start();
	pthread_mutex_lock(&myth_mutex);

	mysqlptr->version = myth_check_version(); 

	if (sqlprog != NULL) { 
		free(sqlprog);
		sqlprog=NULL;
	}
	if ((sqlprog=malloc(sizeof(struct program)*50))==NULL) {
		perror("malloc");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mallor Error.\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		goto out;
	}

        sqlcount=get_guide_mysql(&sqlprog,starttime,endtime,mysqlptr->host, mysqlptr->user, mysqlptr->pass,mysqlptr->db);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1): sqlcount=%d\n",
			__FUNCTION__, __FILE__, __LINE__,sqlcount); 
	if (sqlcount < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"No Guide retuned from Database...\nDatabase Error.  Please check your settings\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		free(sqlprog);
		goto out;
	}
	
	if (myth_sql_program_info(curtime,sqlcount, 0) <0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "error returned from %s [#%s] line: %d\n", __FUNCTION__ ,sqlcount,__LINE__); 
	}

	add_osd_widget(mythtv_program_widget, OSD_PROGRAM, osd_settings.program, NULL);
	snprintf(buf, sizeof(buf),"Recordings %s - %s",starttime,endtime);
	mvpw_set_menu_title(widget, buf);
	mvpw_clear_menu(widget);
	snprintf(buf, sizeof(buf),"Get Next Hour");
	mvpw_add_menu_item(widget, buf , (void*)0, &item_attr);
	snprintf(buf, sizeof(buf),"Get Previous Hour");
	mvpw_add_menu_item(widget, buf , (void*)1, &item_attr);
	count = sqlcount;
	qsort(sqlprog,sqlcount,sizeof(*sqlprog),schedule_compare); 

	for (i=1; i<sqlcount-1; i++) {
		switch (sqlprog[i].recording) {
			case 1:
				item_attr.fg = mythtv_colors.pending_recording;
				break;
			case 2:
				item_attr.fg = mythtv_colors.pending_will_record;
				break;
			case 3:
				item_attr.fg = mythtv_colors.pending_conflict;
				break;
			case 4:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 5:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 6:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 7:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 8:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 9:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 10:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 99:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			default:
				item_attr.fg = mythtv_attr.fg;
		}

		snprintf(buf, sizeof(buf),"%d (%s): %s - %s",sqlprog[i].channum,sqlprog[i].callsign,sqlprog[i].title,sqlprog[i].subtitle); 
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
	}
	out:
		mvpw_show(widget);
		mvpw_focus(widget);
		pthread_mutex_unlock(&myth_mutex);
		busy_end();
		return 0;
}

static void
prog_finder_char_callback(mvp_widget_t *widget, char *item , void *key)
{
	int which = (int)key;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);

 
	switch (which) {
		case 0:
			break;
		default:
			break;
	}

}

static void
hilite_prog_finder_char_callback(mvp_widget_t *widget, char *item , void *key, int hilite)
{
	int which = (int)key;
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);

	sprintf(prog_finder_hilite_char, "%s", item);
	sprintf(prog_finder_hilite_title, "%s", "");

	switch (which) {
		case 0:
			break;
		case 1:
			break;
		default:
			break;
	}
}

static void
prog_finder_title_callback(mvp_widget_t *widget, char *item , void *key)
{
	int which = (int)key;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);

	switch (which) {
		case 0:
			break;
		default:
			break;
	}
}

 
static void
hilite_prog_finder_title_callback(mvp_widget_t *widget, char *item , void *key, int hilite)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);
	if (hilite){
		sprintf(prog_finder_hilite_title, "%s", item);
	}
}

static void
prog_finder_time_callback(mvp_widget_t *widget, char *item , void *key)
{
	int which = (int)key;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);
	fprintf(stderr,"DB version = %d\n",mysqlptr->version);
	switch (which) {
		default:
			/* Record only this showing */
			mythtv_schedule_recording(widget, item , key, 1);
			break;
		} /* first switch */

	return;
}

extern char *strptime(const char *buf, const char *format, struct tm *tm);

static void
hilite_prog_finder_time_callback(mvp_widget_t *widget, char *item , void *key, int hilite)
{
	char buf[550];
	char tmbuf[50];
	char startstring[50];
        char frmttime[25];
        struct tm *loctime;

        curtime=time(NULL);
        loctime=localtime(&curtime);

	int which = (int)key;
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) \n",
		__FUNCTION__, __FILE__, __LINE__);
	if (hilite){
		strptime(sqlprog[which].starttime,"%Y-%m-%d %H:%M:%S", loctime);
		strftime(frmttime,25, "%a %b %d, %I:%M %p", loctime);
		snprintf(tmbuf, sizeof(frmttime),"%s",frmttime); 
		strptime(sqlprog[which].endtime,"%Y-%m-%d %H:%M:%S", loctime);
		strftime(frmttime,25, " - %I:%M %p", loctime);
		strncat(tmbuf, frmttime, sizeof(frmttime));
		snprintf(startstring, sizeof(startstring), "%s", tmbuf);
 		sprintf(buf, "%s\n%d - %s\n%s\n%s",startstring,sqlprog[which].channum,sqlprog[which].callsign,sqlprog[which].subtitle,sqlprog[which].description); 
		cmyth_dbg(CMYTH_DBG_DEBUG, "buf = %s\n",buf);
		mvpw_set_text_str(program_info_widget, buf); 
		mvpw_show(program_info_widget);
	}

}

int
mythtv_prog_finder_char_menu_right (int direction, mvp_widget_t *widget, mvp_widget_t *widget2, mvp_widget_t *widget3) 
{
	int i=0,sqlcount,count;
	char buf[256];
        char starttime[25];
        struct tm *loctime;

        curtime=time(NULL);
        loctime=localtime(&curtime);
        strftime(starttime,256, "%Y-%m-%d %H:00:00", loctime);

	item_attr.select = prog_finder_title_callback;
	item_attr.hilite = hilite_prog_finder_title_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	mvpw_show(root);
	mvpw_expose(root);

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mythtv Server Error\n" );
		mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		goto out;
	}

	busy_start();
	pthread_mutex_lock(&myth_mutex);

	mysqlptr->version = myth_check_version(); 
	if (sqlprog != NULL) { 
		free(sqlprog);
		sqlprog=NULL;
	}
	if ((sqlprog=malloc(sizeof(struct program)*50))==NULL) {
		perror("malloc");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mallor Error.\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		goto out;
	}

        sqlcount=get_prog_finder_char_title_mysql(&sqlprog, starttime, prog_finder_hilite_char, mysqlptr->host, mysqlptr->user, mysqlptr->pass,mysqlptr->db);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1): sqlcount=%d\n",
			__FUNCTION__, __FILE__, __LINE__,sqlcount); 
	if (sqlcount < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Database Error.  Please check your settings\n" );
		mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		goto out;
	}
/*	
	if (myth_sql_program_info(curtime,sqlcount, 1) <0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "error returned from %s [#%s] line: %d\n",
			__FUNCTION__ ,sqlcount,__LINE__); 
	}
*/
	mvpw_clear_menu(widget2);
	count = sqlcount;

	for (i=0; i<sqlcount; i++) {
		snprintf(buf, sizeof(buf),"%s",sqlprog[i].title); 
		if (i <=(sqlcount/2)) { 
			mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		}
		else {
			mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		}
	}
	out:
		mvpw_focus(widget);
		pthread_mutex_unlock(&myth_mutex);
		busy_end();
		return 0;
}

int
mythtv_prog_finder_char_menu(mvp_widget_t *widget, mvp_widget_t *widget2, mvp_widget_t *widget3) 
{
	int i=0;
#ifdef UPDOWN		/* Remove since up/down arrow does not work to change titles */
	int sqlcount,count;  
#endif
	char buf[256];
        char starttime[25];
        struct tm *loctime;

        curtime=time(NULL);
        loctime=localtime(&curtime);
        strftime(starttime,256, "%Y-%m-%d %H:00:00", loctime);

	/* First Character Selector */
	item_attr.select = prog_finder_char_callback;
	item_attr.hilite = hilite_prog_finder_char_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	/* Add letters to widget */
	for(i='A';i<='Z';i++) {
			snprintf(buf, sizeof(buf),"%c", i);
			mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
	}
	/* Add @ sign */
	snprintf(buf, sizeof(buf),"%c", '@');
	mvpw_add_menu_item(widget, buf , (void*)'@', &item_attr);

	/* Add Numbers to widget */
	for(i='0';i<='9';i++) {
			snprintf(buf, sizeof(buf),"%c", i);
			mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
	}

	mvpw_show(root);
	mvpw_expose(root);

	/* Program Title Selector */
/*
	item_attr.select = prog_finder_title_callback;
	item_attr.hilite = hilite_prog_finder_title_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;
*/
#ifdef UPDOWN		/* Remove since up/down arrow does not work to change titles */

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mythtv Server Error\n" );
		mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		goto out;
	}

#endif 	/* Remove since up/down arrow does not work to change titles */

	busy_start();
	pthread_mutex_lock(&myth_mutex);

	mysqlptr->version = myth_check_version(); 

#ifdef UPDOWN		/* Remove since up/down arrow does not work to change titles */
	if (sqlprog != NULL) { 
		free(sqlprog);
		sqlprog=NULL;
	}
	if ((sqlprog=malloc(sizeof(struct program)*50))==NULL) {
		perror("malloc");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mallor Error.\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		goto out;
	}
	
        sqlcount=get_prog_finder_char_title_mysql(&sqlprog, starttime, "A", 
		sqlptr->host, mysqlptr->user, mysqlptr->pass,mysqlptr->db);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1): sqlcount=%d\n",
			__FUNCTION__, __FILE__, __LINE__,sqlcount); 
	if (sqlcount < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Database Error.  Please check your settings\n" );
		mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		goto out;
	}
/*
	if (myth_sql_program_info(curtime,sqlcount, 1) <0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "error returned from %s [#%s] line: %d\n",
			__FUNCTION__ ,sqlcount,__LINE__); 
	}
*/
#endif
	add_osd_widget(mythtv_program_widget, OSD_PROGRAM, osd_settings.program, NULL);
#ifdef UPDOWN		/* Remove since up/down arrow does not work to change titles */
	count = sqlcount;

	for (i=2; i<sqlcount; i++) {
		snprintf(buf, sizeof(buf),"%s",sqlprog[i].title); 
		if (i <=(sqlcount/2)) { 
			mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		}
		else {
			mvpw_add_menu_item(widget2, buf , (void*)i, &item_attr);
		}
	}
	out:
#endif		/* Remove since up/down arrow does not work to change titles */
		mvpw_show(widget);
		mvpw_show(widget2);
		mvpw_show(widget3);
		mvpw_focus(widget);
		pthread_mutex_unlock(&myth_mutex);
		busy_end();
		return 0;
}

int
mythtv_prog_finder_title_menu_right (mvp_widget_t *widget, mvp_widget_t *widget2, mvp_widget_t *widget3) 
{
	int i=0,sqlcount,count;
	char buf[256];
        char starttime[25];
	char frmttime[25];
        struct tm *loctime;

        cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
                 __FUNCTION__, __FILE__, __LINE__);

        curtime=time(NULL);
        loctime=localtime(&curtime);
        strftime(starttime,256, "%Y-%m-%d %H:00:00", loctime);

	item_attr.select = prog_finder_time_callback;
	item_attr.hilite = hilite_prog_finder_time_callback;
	item_attr.fg = mythtv_attr.fg;
	item_attr.bg = mythtv_attr.bg;

	mvpw_show(root);
	mvpw_expose(root);

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mythtv Server Error\n" );
		mvpw_add_menu_item(widget3, buf , (void*)i, &item_attr);
		goto out;
	}

	busy_start();
	pthread_mutex_lock(&myth_mutex);

	mysqlptr->version = myth_check_version(); 
	if (sqlprog != NULL) { 
		free(sqlprog);
		sqlprog=NULL;
	}
	if ((sqlprog=malloc(sizeof(struct program)*50))==NULL) {
		perror("malloc");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Mallor Error.\n" );
		mvpw_add_menu_item(widget, buf , (void*)i, &item_attr);
		goto out;
	}

        sqlcount=get_prog_finder_time_mysql(&sqlprog, starttime, prog_finder_hilite_title, 
		mysqlptr->host, mysqlptr->user, mysqlptr->pass,mysqlptr->db);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1): sqlcount=%d\n",
			__FUNCTION__, __FILE__, __LINE__,sqlcount); 
	if (sqlcount < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1)\n",
			__FUNCTION__, __FILE__, __LINE__); 
		snprintf(buf, sizeof(buf),"Database Error.  Please check your settings\n" );
		mvpw_add_menu_item(widget3, buf , (void*)i, &item_attr);
		goto out;
	}

	fprintf(stderr, "Calling program_info with time: %s and sql count: %d\n", starttime, sqlcount);
	if (myth_sql_program_info(curtime,sqlcount, 1) <0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "error returned from %s [#%s] line: %d\n",
			__FUNCTION__ ,sqlcount,__LINE__); 
	}

	mvpw_clear_menu(widget3);
	count = sqlcount;

	for (i=0; i<sqlcount; i++) {
		fprintf(stderr, "%s: Recording Type: %d\n", sqlprog[i].title, sqlprog[i].recording);
		switch (sqlprog[i].recording) {
			case 1:
				item_attr.fg = mythtv_colors.pending_recording;
				break;
			case 2:
				item_attr.fg = mythtv_colors.pending_will_record;
				break;
			case 3:
				item_attr.fg = mythtv_colors.pending_conflict;
				break;
			case 4:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 5:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 6:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 7:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 8:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 9:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 10:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			case 99:
				item_attr.fg = mythtv_colors.pending_other;
				break;
			default:
				item_attr.fg = mythtv_attr.fg;
		}


		strptime(sqlprog[i].starttime, "%Y-%m-%d %H:%M:%S", loctime);
		strftime(frmttime,25, "%a %b %d, %I:%M %p", loctime);
		snprintf(buf, sizeof(frmttime),"%s",frmttime); 
		if (i <=(sqlcount/2)) { 
			mvpw_add_menu_item(widget3, buf , (void*)i, &item_attr);
		}
		else {
			mvpw_add_menu_item(widget3, buf , (void*)i, &item_attr);
		}
	}
	out:
		pthread_mutex_unlock(&myth_mutex);
		busy_end();
		return 0;
}

void
mythtv_browser_expose(mvp_widget_t *widget)
{
	cmyth_proginfo_t prog = NULL;

	if (gui_state != MVPMC_STATE_MYTHTV)
		return;

	if (!episode_dirty && !pending_dirty)
		return;

	/*
	 * To avoid scaring the user into thinking that they might be
	 * deleting the wrong episode, do not update the menu while
	 * the popup window is visible.
	 */
	if (mvpw_visible(mythtv_popup)) {
		printf("mythtv_popup has focus, skipping update\n");
		return;
	}

	if (hilite_prog)
		prog = cmyth_hold(hilite_prog);

	switch (mythtv_state) {
	case MYTHTV_STATE_SCHEDULE:
		break;
	case MYTHTV_STATE_PROG_FINDER:
		mythtv_update(widget);
		mythtv_set_popup_menu(MYTHTV_STATE_PROG_FINDER);
		break;
	case MYTHTV_STATE_MAIN:
		break;
	case MYTHTV_STATE_PENDING:
	case MYTHTV_STATE_UPCOMING:
		if (!pending_dirty)
			goto out;
		printf("myth browser expose: pending\n");
		mythtv_pending(widget);
		break;
	case MYTHTV_STATE_PROGRAMS:
		if (!episode_dirty)
			goto out;
		printf("myth browser expose: programs\n");
		mythtv_update(widget);
		break;
	case MYTHTV_STATE_EPISODES:
		if (!episode_dirty)
			goto out;
		printf("myth browser expose: episodes\n");
		mythtv_update(widget);
		mythtv_set_popup_menu(MYTHTV_STATE_EPISODES);
		if (prog) {
			int i;

			if ((i=episode_index(prog)) >= 0) {
				printf("change hilite\n");
				mvpw_menu_hilite_item(widget, (void*)i);
				hilite_callback(widget, NULL, (void*)i, 1);
			}
		}
		break;
	case MYTHTV_STATE_LIVETV:
		break;
	}

 out:
	if (prog)
		cmyth_release(prog);
}

void
mythtv_thruput(void)
{
	cmyth_proginfo_t hi_prog = cmyth_hold(hilite_prog);

	switch_hw_state(MVPMC_STATE_MYTHTV);

	if (mythtv_recdir) {
		video_functions = &file_functions;
	} else {
		video_functions = &mythtv_functions;
	}

	while (video_reading)
		;

	CHANGE_GLOBAL_REF(current_prog, hi_prog);

	printf("reset demuxer and play video...\n");

	demux_reset(handle);
	demux_attr_reset(handle);
	video_play(root);

	cmyth_release(hi_prog);
}
