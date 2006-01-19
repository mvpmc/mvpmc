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
#include "config.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

/*
 * Swap a reference counted global for a new reference (including NULL).
 * This little dance ensures that no one can be taking a new reference
 * to the global while the old reference is being destroyed.  Either another
 * thread will get the value while it is still held and hold it again, or
 * it will get the new (held) value.  At the end of this, 'new_ref' will be
 * held both by the 'new_ref' reference and the newly created 'ref'
 * reference.  The caller is responsible for releasing 'new_ref' when it is
 * no longer in use (and 'ref' for that matter).
 */
#define CHANGE_GLOBAL_REF(global_ref, new_ref) \
{                                              \
  void *tmp_ref = cmyth_hold((global_ref));    \
  cmyth_release((global_ref));                 \
  (global_ref) = cmyth_hold((new_ref));        \
  cmyth_release((tmp_ref));                    \
}

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
static pthread_cond_t event_cond = PTHREAD_COND_INITIALIZER;

static volatile cmyth_conn_t control;		/* master backend */
static volatile cmyth_conn_t event;		/* master backend */
static volatile cmyth_proginfo_t current_prog;	/* program currently being played */
static volatile cmyth_proginfo_t hilite_prog;	/* program currently hilighted */
static volatile cmyth_proglist_t episode_plist;
static volatile cmyth_proglist_t pending_plist;

static volatile cmyth_recorder_t recorder;

static volatile int pending_dirty = 0;
static volatile int episode_dirty = 0;

static volatile int current_livetv;

volatile mythtv_state_t mythtv_state = MYTHTV_STATE_MAIN;

static int show_count, episode_count;
static volatile int list_all = 0;

int playing_file = 0;
int running_mythtv = 0;
int mythtv_main_menu = 0;
int mythtv_debug = 0;

static volatile int playing_via_mythtv = 0;
static volatile int close_mythtv = 0;
static volatile int changing_channel = 0;
static volatile int video_reading = 0;

static pthread_t control_thread, wd_thread, event_thread;

#define MAX_TUNER	16
struct livetv_proginfo {
	int rec_id;
	int busy;
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

static video_callback_t livetv_functions = {
	.open      = livetv_open,
	.read      = mythtv_read,
	.read_dynb = NULL,
	.seek      = mythtv_seek,
	.size      = livetv_size,
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
};

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

static int
livetv_compare(const void *a, const void *b)
{
	const struct livetv_prog *x, *y;
	int X, Y;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	x = ((const struct livetv_prog*)a);
	y = ((const struct livetv_prog*)b);
	X = atoi(x->pi[0].chan);
	Y = atoi(y->pi[0].chan);

	if (X < Y) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1 }\n",
			    __FUNCTION__, __FILE__, __LINE__);

		return -1;
	} else if (X > Y) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 1}\n",
			    __FUNCTION__, __FILE__, __LINE__);

		return 1;
	} else {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return 0;
	}
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

static void
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

static int
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
	CHANGE_GLOBAL_REF(file, NULL);
	CHANGE_GLOBAL_REF(recorder, NULL);
	CHANGE_GLOBAL_REF(current_prog, NULL);
	CHANGE_GLOBAL_REF(hilite_prog, NULL);
	CHANGE_GLOBAL_REF(episode_plist, NULL);
	CHANGE_GLOBAL_REF(pending_plist, NULL);
	CHANGE_GLOBAL_REF(control, NULL);
	CHANGE_GLOBAL_REF(event, NULL);
}

static void
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
}

static void
mythtv_close_file(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (playing_via_mythtv && file) {
		fprintf(stderr, "%s(): closing file\n", __FUNCTION__);
		CHANGE_GLOBAL_REF(file, NULL);
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
	if ((episode_plist == NULL) || episode_dirty) {
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
		ep_list = cmyth_hold(episode_plist);
	}
	fprintf(stderr, "'cmyth_proglist_get_all_recorded' worked\n");

	count = cmyth_proglist_get_count(episode_plist);

	/* Sort on first load and when setting changes or list update makes the sort dirty */
	if(mythtv_sort_dirty) {
		printf("Sort for Dirty List\n");
		cmyth_proglist_sort(episode_plist, count, mythtv_sort);
		mythtv_sort_dirty = 0;
	}

	/*
	 * Save all known recording groups
	 */
	for (i=0; i<count; i++) {
		char *recgroup;
		cmyth_proginfo_t prog;

		prog = cmyth_proglist_get_item(episode_plist, i);
		recgroup = cmyth_proginfo_recgroup(prog);

		add_recgroup(recgroup);

		cmyth_release(prog);
		cmyth_release(recgroup);
	}

	return count;

    err:
	cmyth_release(ep_list);
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
	    (mythtv_state == MYTHTV_STATE_PENDING)) {
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
	return mythtv_pending_filter(widget, MYTHTV_FILTER_NONE);
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
	cmyth_event_t next;
	pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;

	printf("event thread started (pid %d)\n", getpid());

	pthread_mutex_lock(&event_mutex);

	while (1) {
		while (event == NULL) {
			pthread_cond_wait(&event_cond, &event_mutex);
		}
		next = cmyth_event_get(event);
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
		while ((file == NULL) && (recorder == NULL)) {
			pthread_cond_wait(&cond, &mutex);
		}
		fprintf(stderr, "Got stream recorder = %p, file = %p)\n",
			recorder, file);
		if (file)
			printf("%s(): starting file playback\n", __FUNCTION__);
		if (recorder)
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
				len = cmyth_ringbuf_request_block(recorder,
								  size);
			else
				len = cmyth_file_request_block(file, size);

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
				while (paused && (file || recorder) &&
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
		} while ((file || recorder) && (len > 0) &&
			 (playing_via_mythtv == 1) && (!close_mythtv));

		video_reading = 0;

		fprintf(stderr,
			"%s(): len %d playing_via_mythtv %d close_mythtv %d\n",
		       __FUNCTION__, len, playing_via_mythtv, close_mythtv);

		if (close_mythtv) {
			if (recorder) {
				mythtv_livetv_stop();
			}
			if (file) {
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

	fprintf(stderr, "cleanup mythtv data structures\n");

	CHANGE_GLOBAL_REF(pending_plist, NULL);
	CHANGE_GLOBAL_REF(episode_plist, NULL);

	running_mythtv = 0;
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static int
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
	CHANGE_GLOBAL_REF(file, NULL);
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
	CHANGE_GLOBAL_REF(file, f);
	cmyth_release(f);
	cmyth_release(c);
	cmyth_release(loc_prog);

	fprintf(stderr, "starting mythtv file transfer\n");

	playing_file = 1;

	pthread_cond_signal(&cond);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

static long long
mythtv_seek(long long offset, int whence)
{
	struct timeval to;
	int count = 0;
	long long seek_pos = -1, size;
	cmyth_file_t f = cmyth_hold(file);
	cmyth_recorder_t r = cmyth_hold(recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_livetv)
		seek_pos = cmyth_ringbuf_seek(r, 0, SEEK_CUR);
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
			if (cmyth_ringbuf_select(r, &to) > 0) {
				PRINTF("%s(): reading...\n", __FUNCTION__);
				len = cmyth_ringbuf_get_block(r, buf,
							     sizeof(buf));
				PRINTF("%s(): read returned %d\n",
				       __FUNCTION__, len);
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

static int
mythtv_read(char *buf, int len)
{
	int ret = -EBADF;
	int tot = 0;
	cmyth_file_t f = cmyth_hold(file);
	cmyth_recorder_t r = cmyth_hold(recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);

	pthread_mutex_lock(&seek_mutex);
	pthread_cond_signal(&cond);

	PRINTF("cmyth getting block of size %d\n", len);
	while ((f || r) && (tot < len) && !myth_seeking) {
		struct timeval to;

		to.tv_sec = 0;
		to.tv_usec = 10;
		ret = -EBADF;
		if (mythtv_livetv) {
			if (cmyth_ringbuf_select(r, &to) <= 0) {
				break;
			}
			ret = cmyth_ringbuf_get_block(r, buf+tot, len-tot);
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

static long long
mythtv_size(void)
{
	static struct timeval last = { 0, 0 };
	static cmyth_proginfo_t prog = NULL;
	struct timeval now;
	long long ret;
	static volatile int failed = 0;
	cmyth_proginfo_t loc_prog = cmyth_hold(current_prog), new_prog = NULL;
	cmyth_conn_t ctrl = cmyth_hold(control);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
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
		ret = cmyth_file_length(file);
		goto unlock;
	}

	failed = 0;

	ret = cmyth_proginfo_length(new_prog);

	memcpy(&last, &now, sizeof(last));
	CHANGE_GLOBAL_REF(current_prog, new_prog);
	CHANGE_GLOBAL_REF(prog, current_prog);

 unlock:
	pthread_mutex_unlock(&myth_mutex);
	cmyth_release(ctrl);
	cmyth_release(loc_prog);
	cmyth_release(new_prog);

 out:
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %lld}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);
	return ret;
}

static int
mythtv_livetv_start(int *tuner)
{
	double rate;
	char *rb_file;
	char *msg = NULL, buf[128], t[16];
	int c, i, id = 0;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = cmyth_hold(control);
	cmyth_recorder_t rec = NULL;
	cmyth_recorder_t ring = NULL;
	char *path;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (playing_via_mythtv && (file || recorder))
		mythtv_stop();

	if (mythtv_livetv) {
		fprintf(stderr, "Live TV already active\n");
		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		mvpw_focus(mythtv_browser);
		pthread_mutex_lock(&myth_mutex);
		get_livetv_programs();
		pthread_mutex_unlock(&myth_mutex);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return 0;
	}

	fprintf(stderr, "Starting Live TV...\n");

	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	pthread_mutex_lock(&myth_mutex);

	mythtv_livetv = 1;
	playing_via_mythtv = 1;
	if (mythtv_ringbuf) {
		video_functions = &file_functions;
	} else {
		video_functions = &livetv_functions;
	}

	if ((c = cmyth_conn_get_free_recorder_count(ctrl)) < 0) {
		mythtv_shutdown(1);
		goto err;
	}
	printf("Found %d free recorders\n", c);

	if (tuner[0]) {
		int count = 0;
		for (i=0; i<MAX_TUNER && tuner[i]; i++) {
			fprintf(stderr, "Looking for tuner %d\n", tuner[i]);
                        count++; 
			if ((rec = cmyth_conn_get_recorder_from_num(ctrl,
								    tuner[i]))
			    == NULL) {
				continue;
			}
			if(cmyth_recorder_is_recording(rec) == 1) {
				cmyth_release(rec);
				rec = NULL;
				continue;
			}
			id = tuner[i];
			break;
		}
		if (id == 0) {
			/*
			 * None of the tuners are free, so display a good error
			 * message for the user.
			 */
			if (count == 1) {
				snprintf(buf, sizeof(buf),
					 "Tuner %d is currently unavailable.",
					 tuner[0]);
			} else {
				snprintf(buf, sizeof(buf), "Tuners ");
				for (i=0; i<count; i++) {
					if ((i != 0) && (count > 2))
						strcat(buf, ",");
					if (i == (count-1))
						strcat(buf, " and");
					snprintf(t, sizeof(t),
						 " %d", tuner[i]);
					strcat(buf, t);
				}
				strcat(buf, " are currently unavailable.");
			}
			msg = buf;
			goto err;
		}
	} else {
		fprintf(stderr, "Looking for any free recorder\n");
		if ((rec = cmyth_conn_get_free_recorder(ctrl)) == NULL) {
			msg = "Failed to get free recorder.";
			goto err;
		}
	}

	if ((ring = cmyth_ringbuf_setup(rec)) == NULL) {
		msg = "Failed to setup ringbuffer.";
		goto err;
	}

	if (cmyth_conn_connect_ring(ring, 16*1024, mythtv_tcp_program) != 0) {
		msg = "Cannot connect to mythtv ringbuffer.";
		goto err;
	}

	if (cmyth_recorder_spawn_livetv(ring) != 0) {
		msg = "Spawn livetv failed.";
		goto err;
	}

	if (cmyth_recorder_is_recording(ring) != 1) {
		msg = "LiveTV not recording.";
		goto err;
	}

	if (cmyth_recorder_get_framerate(ring, &rate) != 0) {
		msg = "Get framerate failed.";
		goto err;
	}

	fprintf(stderr, "recorder framerate is %5.2f\n", rate);


	rb_file = (char *) cmyth_recorder_get_filename(ring);
	/*
	 * Change current.  The thing about 'current' is that it is a
	 * global that is not allocated as reference counted and it is
	 * allocated and freed outside this file.  If / when it turns
	 * into reference counted space, it will be much cleaner and
	 * safer to hold it in a local, release and change it and then
	 * release the local.  For now we take a chance on a non-held
	 * reference being destroyed.
	 */
	if (mythtv_ringbuf) {
		char *tmp;
		path = current;
		tmp = malloc(strlen(mythtv_ringbuf) + strlen(rb_file) + 2);
		sprintf(tmp, "%s/%s", mythtv_ringbuf, rb_file);
		current = tmp;
		free(path);
	} else {
		path = current;
		current = strdup(rb_file);
		free(path);
	}
	cmyth_release(rb_file);

	// get the information about the current programme
	// we assume last used structure is cleared already...
	//
	loc_prog = cmyth_recorder_get_cur_proginfo(ring);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	cmyth_release(loc_prog);

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

	CHANGE_GLOBAL_REF(recorder, ring);
#if 0
	cmyth_release(ctrl);
	cmyth_release(rec);
#endif
	cmyth_release(ring);
	running_mythtv = 1;
	pthread_mutex_unlock(&myth_mutex);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;

 err:
	pthread_mutex_unlock(&myth_mutex);

	mythtv_livetv = 0;
	if (msg)
		gui_error(msg);

	cmyth_release(ctrl);
	cmyth_release(rec);
	cmyth_release(ring);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return -1;
}

int
mythtv_livetv_stop(void)
{
	int ret = -1;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (!mythtv_livetv) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			  __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	fprintf(stderr, "Stopping Live TV\n");

	busy_start();

	pthread_mutex_lock(&myth_mutex);

	if (!mythtv_ringbuf) {
		playing_via_mythtv = 0;
		running_mythtv = 0;
		close_mythtv = 0;
	}

	mythtv_livetv = 0;

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);

	if (cmyth_recorder_stop_livetv(recorder) != 0) {
		fprintf(stderr, "stop livetv failed\n");
		goto fail;
	}

	if (cmyth_recorder_done_ringbuf(recorder) != 0) {
		fprintf(stderr, "done ringbuf failed\n");
		goto fail;
	}

	CHANGE_GLOBAL_REF(recorder, NULL);
	CHANGE_GLOBAL_REF(current_prog, NULL);

	ret = 0;

 fail:
	mythtv_livetv = 0;
	pthread_mutex_unlock(&myth_mutex);

	busy_end();
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);
	return ret;
}

int __change_channel(direction)
{
	int ret = 0;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = cmyth_hold(control);
	cmyth_recorder_t rec = cmyth_hold(recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	changing_channel = 1;

	busy_start();
	video_clear();
	pthread_mutex_lock(&myth_mutex);

	if (cmyth_recorder_pause(rec) < 0) {
		fprintf(stderr, "channel change (pause) failed\n");
		ret = -1;
		goto out;
	}

	if (cmyth_recorder_change_channel(rec, direction) < 0) {
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
		if (cmyth_recorder_stop_livetv(rec) != 0) {
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

	loc_prog = cmyth_recorder_get_cur_proginfo(rec);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	cmyth_release(loc_prog);

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
	cmyth_release(ctrl);
	cmyth_release(rec);
	changing_channel = 0;
	busy_end();
	pthread_mutex_unlock(&myth_mutex);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, ret);
        return ret;
}

int
mythtv_channel_up(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return __change_channel(CHANNEL_DIRECTION_UP);
}

int
mythtv_channel_down(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return __change_channel(CHANNEL_DIRECTION_DOWN);
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

static long long
livetv_size(void)
{
	long long seek_pos;
	cmyth_recorder_t rec = cmyth_hold(recorder);

	/*
	 * XXX: How do we get the program size for live tv?
	 */

	pthread_mutex_lock(&myth_mutex);
	seek_pos = cmyth_ringbuf_seek(rec, 0, SEEK_CUR);
	PRINTF("%s(): pos %lld\n", __FUNCTION__, seek_pos);
	pthread_mutex_unlock(&myth_mutex);

	cmyth_release(rec);
	return (seek_pos+(1024*1024*500));
}

static int
livetv_open(void)
{
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (playing_via_mythtv == 0) {
		playing_via_mythtv = 1;
		fprintf(stderr, "starting mythtv live tv transfer\n");
	}

	playing_file = 1;

	pthread_cond_signal(&cond);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

static void
livetv_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	char *channame = NULL;
	int i, prog = (int)key;
	int id = -1;
	int tuner_change = 1, tuner[MAX_TUNER];
	struct livetv_proginfo *pi;
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = cmyth_hold(control);
	cmyth_recorder_t rec = cmyth_hold(recorder);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	memset(tuner, 0, sizeof(tuner));

	switch_hw_state(MVPMC_STATE_MYTHTV);

	if (mythtv_recdir)
		video_functions = &file_functions;
	else
		video_functions = &livetv_functions;


	if (mythtv_livetv) {
		id = cmyth_recorder_get_recorder_id(rec);
		for (i=0; i<livetv_list[prog].count; i++) {
			pi = &livetv_list[prog].pi[i];
			if (id == pi->rec_id) {
				tuner_change = 0;
				channame = cmyth_hold(pi->chan);
				break;
			}
		}
	} else {
		channame = cmyth_hold(livetv_list[prog].pi[0].chan);
		for (i=0; i<livetv_list[prog].count; i++) {
			tuner[i] = livetv_list[prog].pi[i].rec_id;
			printf("enable livetv tuner %d chan '%s'\n",
			       tuner[i], channame);
		}
		tuner_change = 0;
	}

	if (tuner_change && (id != -1)) {
		for (i=0; i<livetv_list[prog].count; i++)
			tuner[i] = livetv_list[prog].pi[i].rec_id;
		cmyth_release(channame);
		channame = cmyth_hold(livetv_list[prog].pi[0].chan);
		fprintf(stderr, "switch from tuner %d to %d\n", id, tuner[0]);
		mythtv_livetv_stop();
	}

	if (mythtv_ringbuf)
		mythtv_livetv_stop();

	if (mythtv_livetv == 0) {
		fprintf(stderr, "Live TV not active!\n");
		if (mythtv_livetv_start(tuner) != 0) {
			goto out;
		}
		cmyth_release(rec);
		cmyth_release(ctrl);
		rec = cmyth_hold(recorder);
	}

	if (item)
		fprintf(stderr, "%s(): change channel '%s' '%s'\n",
		       __FUNCTION__, channame, item);

	changing_channel = 1;

	busy_start();
	pthread_mutex_lock(&myth_mutex);

	if (cmyth_recorder_pause(rec) < 0) {
		fprintf(stderr, "channel change (pause) failed\n");
		goto unlock;
	}

	if (cmyth_recorder_set_channel(rec, channame) < 0) {
		fprintf(stderr, "channel change failed!\n");
		goto unlock;
	}

	loc_prog = cmyth_recorder_get_cur_proginfo(rec);
	CHANGE_GLOBAL_REF(current_prog, loc_prog);
	cmyth_release(loc_prog);

	demux_reset(handle);
	demux_attr_reset(handle);
	av_play();
	video_play(root);

	if (widget)
		mythtv_fullscreen();

	i = 0;
	while (mvpw_menu_set_item_attr(mythtv_browser,
				       (void*)i, &item_attr) == 0) {
		i++;
	}
	if (mvpw_menu_get_item_attr(mythtv_browser, key, &item_attr) == 0) {
		uint32_t old_fg = item_attr.fg;
		item_attr.fg = mythtv_colors.livetv_current;
		mvpw_menu_set_item_attr(mythtv_browser, key, &item_attr);
		item_attr.fg = old_fg;
	}
	mvpw_menu_hilite_item(mythtv_browser, key);

 unlock:
	pthread_mutex_unlock(&myth_mutex);
	busy_end();

 out:
	cmyth_release(ctrl);
	cmyth_release(rec);
	cmyth_release(channame);
	changing_channel = 0;
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static void
livetv_hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	int prog = (int)key;
	char buf[256];

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
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
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);
}

static int
get_livetv_programs_rec(int id, struct livetv_prog **list, int *n, int *p)
{
	cmyth_proginfo_t next_prog = NULL, cur = NULL;
	cmyth_conn_t ctrl = cmyth_hold(control);
	cmyth_recorder_t rec = cmyth_hold(recorder);
	cmyth_timestamp_t ts;
	char *title = NULL, *subtitle = NULL, *channame = NULL;
	char *start_channame = NULL, *chansign = NULL;
	char *description = NULL;
	char start[256], end[256], *ptr;
	int cur_id, i; 
	int shows = 0, unique = 0, busy = 0;
	struct livetv_proginfo *pi;
	
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	cur_id = cmyth_recorder_get_recorder_id(rec);
	
	
	fprintf(stderr,
		"Getting program listings for recorder %d [%d]\n",
		id, cur_id);

	if (cur_id != id) {
		cmyth_release(rec);
		rec = NULL;
		if ((rec = cmyth_conn_get_recorder_from_num(ctrl,
							    id)) == NULL) {
			fprintf(stderr,
				"failed to connect to tuner %d!\n", id);
			cmyth_release(ctrl);
			cmyth_dbg(CMYTH_DBG_DEBUG,
				    "%s [%s:%d]: (trace) -1}\n",
				    __FUNCTION__, __FILE__, __LINE__);
			return -1;
		}
	}

	if (cmyth_recorder_is_recording(rec) == 1)
		busy = 1;

	cur = cmyth_recorder_get_cur_proginfo(rec);
	if (cur == NULL) {
		int i;
		char channame[32];

		fprintf(stderr, "problem getting current proginfo!\n");

		/*
		 * mythbackend must not be tuned in to a channel, so keep
		 * changing channels until we find a valid one, or until
		 * we decide to give up.
		 */
		for (i=1; i<1000; i++) {
			snprintf(channame, sizeof(channame), "%d", i);
			if (cmyth_recorder_set_channel(rec, channame) < 0) {
				continue;
			}
			cur = cmyth_recorder_get_next_proginfo(rec, cur, 1);
			if (cur != NULL)
				break;
		}
	}
	if (cur == NULL) {
		fprintf(stderr, "get program info failed!\n");
		cmyth_release(rec);
		cmyth_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			  __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}
	
	start_channame = (char *) cmyth_proginfo_channame(cur);
	do {
		next_prog = cmyth_recorder_get_next_proginfo(rec, cur, 1);
		if ( next_prog == NULL) {
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
			cmyth_release(ts);
			ts = cmyth_proginfo_end(next_prog);
			cmyth_timestamp_to_string(end, ts);
			cmyth_release(ts);
			ptr = strchr(start, 'T');
			*ptr = '\0';
			memmove(start, ptr+1, strlen(ptr+1)+1);
			ptr = strchr(end, 'T');
			*ptr = '\0';
			memmove(end, ptr+1, strlen(ptr+1)+1);
		}

		cmyth_release(cur);
		cur = cmyth_hold(next_prog);
		shows++;

		/*
		 * Search for duplicates only if the show has a title.
		 */
		if (title[0]) {
			for (i=0; i<*p; i++) {
				if ((strcmp((*list)[i].title, title) == 0) &&
				    (strcmp((*list)[i].subtitle, subtitle)
				     == 0) &&
				    (strcmp((*list)[i].description,
					    description) == 0) &&
				    (strcmp((*list)[i].start, start) == 0) &&
				    (strcmp((*list)[i].end, end) == 0)) {
					if ((*list)[i].count == MAX_TUNER)
						goto next;
					pi=&((*list)[i].pi[(*list)[i].count++]);
					pi->chan = cmyth_hold(channame);
					pi->channame = cmyth_hold(chansign);
					pi->rec_id = id;
					pi->busy = busy;
					goto next;
				}
			}
		}

		(*list)[*p].title = cmyth_hold(title);
		(*list)[*p].subtitle = cmyth_hold(subtitle);
		(*list)[*p].description = cmyth_hold(description);
		if (start)
			(*list)[*p].start = cmyth_strdup(start);
		else
			(*list)[*p].start = NULL;
		if (end)
			(*list)[*p].end = cmyth_strdup(end);
		else
			(*list)[*p].end = NULL;
		(*list)[*p].count = 1;
		(*list)[*p].pi[0].rec_id = id;
		(*list)[*p].pi[0].busy = busy;
		(*list)[*p].pi[0].chan = cmyth_hold(channame);
		(*list)[*p].pi[0].channame = cmyth_hold(chansign);
		(*p)++;
		unique++;

	next:
		if (*p == *n) {
			*n = *n*2;
			*list = realloc(*list, sizeof(**list)*(*n));
		}
		cmyth_release(title);
		cmyth_release(subtitle);
		cmyth_release(description);
		cmyth_release(channame);
		cmyth_release(chansign);
		cmyth_release(next_prog);
	} while (strcmp(start_channame, channame) != 0);

	cmyth_release(cur);
	cmyth_release(rec);
	cmyth_release(start_channame);
	fprintf(stderr, "Found %d shows on recorder %d (%d unique)\n",
		shows, id, unique);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) }\n",
		    __FUNCTION__, __FILE__, __LINE__);

	return shows;
}

static int
get_livetv_programs(void)
{
	struct livetv_prog *list;
	char buf[256];
	int i, j, c, n, p, found;
	time_t t;
	cmyth_conn_t ctrl = cmyth_hold(control);

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (livetv_list) {
		for (i=0; i<livetv_count; i++) {
			cmyth_release(livetv_list[i].title);
			cmyth_release(livetv_list[i].subtitle);
			cmyth_release(livetv_list[i].description);
			cmyth_release(livetv_list[i].start);
			cmyth_release(livetv_list[i].end);
			for (j=0; j<livetv_list[i].count; j++) {
				cmyth_release(livetv_list[i].pi[j].chan);
				cmyth_release(livetv_list[i].pi[j].channame);
			}
		}
		free(livetv_list);
		livetv_count = 0;
		livetv_list = NULL;
	}

	n = 32;
	if ((list=(struct livetv_prog*)malloc(sizeof(*list)*n)) == NULL) {
		perror("malloc()");
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	if ((c=cmyth_conn_get_free_recorder_count(ctrl)) < 0) {
		fprintf(stderr, "unable to get free recorder\n");
		mythtv_shutdown(1);
		cmyth_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -2}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -2;
	}

	mvpw_clear_menu(mythtv_browser);

	item_attr.select = livetv_select_callback;
	item_attr.hilite = livetv_hilite_callback;

	p = 0;
	found = 0;
	for (i=0; i<MAX_TUNER; i++) {
		if (get_livetv_programs_rec(i+1, &list, &n, &p) != -1)
			found++;
	}

	t = time(NULL);
	fprintf(stderr, "Found %d programs on %d tuners at %s\n",
		p, found, ctime(&t));

	if (p == 0) {
		cmyth_release(ctrl);
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

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

	cmyth_release(ctrl);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) 0}\n",
		    __FUNCTION__, __FILE__, __LINE__);
	return 0;
}

int
mythtv_livetv_menu(void)
{
	int failed = 0;
	int err;

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) {\n",
		    __FUNCTION__, __FILE__, __LINE__);
	if (mythtv_verify() < 0) {
		cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) -1}\n",
			    __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}

	fprintf(stderr, "Displaying livetv programs\n");

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

	cmyth_dbg(CMYTH_DBG_DEBUG, "%s [%s:%d]: (trace) %d}\n",
		    __FUNCTION__, __FILE__, __LINE__, failed);
	return failed;
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

int
mythtv_proginfo_livetv(char *buf, int size)
{
	snprintf(buf, size,
		 "Title: %s\n"
		 "Subtitle: %s\n"
		 "Description: %s\n"
		 "Start: %s\n"
		 "End: %s\n",
		 livetv_list[current_livetv].title,
		 livetv_list[current_livetv].subtitle,
		 livetv_list[current_livetv].description,
		 livetv_list[current_livetv].start,
		 livetv_list[current_livetv].end);

	return 0;
}

int
mythtv_livetv_tuners(int *tuners, int *busy)
{
	int i, n;

	n = livetv_list[current_livetv].count;

	for (i=0; i<n; i++) {
		tuners[i] = livetv_list[current_livetv].pi[i].rec_id;
		busy[i] = livetv_list[current_livetv].pi[i].busy;
	}

	return n;
}

void
mythtv_livetv_select(int which)
{
	cmyth_proginfo_t loc_prog = NULL;
	cmyth_conn_t ctrl = cmyth_hold(control);
	cmyth_recorder_t rec = cmyth_hold(recorder);
	int rec_id = livetv_list[current_livetv].pi[which].rec_id;
	int tuner[2] = { rec_id, 0 };
	char *channame = cmyth_hold(livetv_list[current_livetv].pi[which].chan);
	

	switch_hw_state(MVPMC_STATE_MYTHTV);

	printf("starting liveTV on tuner %d channel %s index %d\n",
	       rec_id, channame, current_livetv);

	if (mythtv_livetv_start(tuner) != 0) {
		printf("livetv failed\n");
	} else {
		printf("livetv active, changing to channel %s\n", channame);

		changing_channel = 1;

		busy_start();
		pthread_mutex_lock(&myth_mutex);

		if (cmyth_recorder_pause(rec) < 0) {
			fprintf(stderr, "channel change (pause) failed\n");
			goto err;
		}

		if (cmyth_recorder_set_channel(rec, channame) < 0) {
			fprintf(stderr, "channel change failed!\n");
			goto err;
		}

		loc_prog = cmyth_recorder_get_cur_proginfo(rec);
		CHANGE_GLOBAL_REF(current_prog, loc_prog);
		cmyth_release(loc_prog);

		demux_reset(handle);
		demux_attr_reset(handle);
		av_play();
		video_play(root);

		add_osd_widget(mythtv_program_widget, OSD_PROGRAM,
			       osd_settings.program, NULL);
		mvpw_hide(mythtv_description);
		running_mythtv = 1;

		mythtv_fullscreen();

	err:
		pthread_mutex_unlock(&myth_mutex);
		busy_end();
		changing_channel = 0;
		cmyth_release(rec);
		cmyth_release(ctrl);
	}

	cmyth_release(channame);
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
