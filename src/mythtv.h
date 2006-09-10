#ifndef MYTHTV_H
#define MYTHTV_H

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

#include <cmyth.h>


typedef struct mysql_config_s mysql_config_t;
char prog_finder_hilite_char[1];		/* Current Program Finder First Character */
char prog_finder_hilite_title[256];	/* Current Program Finder Title */ 

/*
 * mythtv_state indicates what part of the gui is active
 */
typedef enum {
	MYTHTV_STATE_MAIN,
	MYTHTV_STATE_PENDING,
	MYTHTV_STATE_PROGRAMS,
	MYTHTV_STATE_EPISODES,
	MYTHTV_STATE_LIVETV,
	MYTHTV_STATE_UPCOMING,
	MYTHTV_STATE_SCHEDULE,
	MYTHTV_STATE_PROG_FINDER,
} mythtv_state_t;

extern volatile mythtv_state_t mythtv_state;

/*
 * mythtv_filter indicates how the upcoming recordings are displayed
 */
#define MYTHTV_NUM_FILTER 4
typedef enum {
	MYTHTV_FILTER_NONE = 0,
	MYTHTV_FILTER_TITLE,
	MYTHTV_FILTER_RECORD,
	MYTHTV_FILTER_RECORD_CONFLICT,
} mythtv_filter_t;

extern mythtv_filter_t mythtv_filter;

/*
 * show_sort indicates how the recordings are categorized
 */
#define MYTHTV_NUM_SORTS_PROGRAMS 3
typedef enum {
	SHOW_TITLE,
	SHOW_CATEGORY,
	SHOW_RECGROUP,
} show_sort_t;

extern show_sort_t show_sort;

/*
 * mythtv_colors indicates the colors of each type of recording in the gui
 */
typedef struct {
	uint32_t livetv_current;
	uint32_t pending_will_record;
	uint32_t pending_conflict;
	uint32_t pending_recording;
	uint32_t pending_other;
	uint32_t menu_item;
} mythtv_color_t;

extern mythtv_color_t mythtv_colors;

extern char *mythtv_server;
extern int mythtv_debug;
extern char *mythtv_recdir;
extern char *mythtv_ringbuf;

extern int running_mythtv;
extern int mythtv_main_menu;
extern int mythtv_tcp_control;
extern int mythtv_tcp_program;
extern int mythtv_sort;
extern int mythtv_sort_dirty;

extern volatile int mythtv_livetv;
extern volatile int current_livetv;
extern volatile cmyth_conn_t control;		/* master backend */
extern volatile int playing_via_mythtv;
extern volatile cmyth_file_t mythtv_file;
extern volatile cmyth_recorder_t mythtv_recorder;
extern volatile cmyth_proginfo_t current_prog;
extern volatile int close_mythtv;
extern volatile int changing_channel;
extern volatile cmyth_database_t mythtv_database;


extern int playing_file;
extern pthread_cond_t myth_cond;

extern pthread_mutex_t myth_mutex;

extern mvp_widget_t *mythtv_browser;
extern mvp_widget_t *mythtv_menu;
extern mvp_widget_t *mythtv_logo;
extern mvp_widget_t *mythtv_date;
extern mvp_widget_t *mythtv_description;
extern mvp_widget_t *mythtv_channel;
extern mvp_widget_t *mythtv_record;
extern mvp_widget_t *mythtv_popup;
extern mvp_widget_t *mythtv_program_widget;
extern mvp_widget_t *mythtv_osd_program;
extern mvp_widget_t *mythtv_osd_description;

extern mvpw_menu_attr_t mythtv_attr;

extern int mythtv_init(char*, int,mysql_config_t *);
extern void mythtv_atexit(void);
extern int mythtv_livetv_chain_update(char * buf);

/* Widgets supporting the new live tv program guide */
extern mvp_widget_t *mythtv_livetv_clock;
extern mvp_widget_t *mythtv_livetv_description;
extern mvp_widget_t *mythtv_livetv_program_list;

/*
 * -----------------------------------------------------------------
 * TV Guide Operations
 * -----------------------------------------------------------------
 */

extern int mvp_tvguide_callback(mvp_widget_t *widget, char key);
extern int get_tvguide_selected_channel(mvp_widget_t *proglist);
extern void
mvp_tvguide_move(int direction, mvp_widget_t *proglist, mvp_widget_t *descr);
extern void mvp_tvguide_video_topright(int on);
extern int mvp_tvguide_init(int edge_left, int edge_top,
														int edge_right, int edge_bottom);
extern int mvp_tvguide_start(void);
extern int mvp_tvguide_stop(void);
extern int myth_get_chan_index(cmyth_chanlist_t chanlist,
															 cmyth_proginfo_t prog);
extern int myth_get_chan_index_from_str(cmyth_chanlist_t chanlist,
															 char * chan);
extern int myth_get_chan_index_from_int(cmyth_chanlist_t chanlist,
															 int nchan);
extern cmyth_tvguide_progs_t myth_load_guide(mvp_widget_t *widget,
												cmyth_database_t mythtv_database,
												cmyth_chanlist_t chanlist,
												cmyth_tvguide_progs_t proglist,
												int index, int * xofs, int * yofs,
												long free_recorders);
extern int myth_guide_set_channels(void * widget, cmyth_chanlist_t chanlist,
																	 int index, int yofs, long free_recorders);
extern int myth_set_guide_times(mvp_widget_t *widget, int xofs);
extern void mvp_tvguide_show(mvp_widget_t *proglist, mvp_widget_t *descr,
														 mvp_widget_t *clock);
extern void mvp_tvguide_hide(mvp_widget_t *proglist, mvp_widget_t *descr,
														 mvp_widget_t *clock);
extern cmyth_chanlist_t myth_release_chanlist(cmyth_chanlist_t cl);
extern cmyth_tvguide_progs_t
				myth_release_proglist(cmyth_tvguide_progs_t proglist);
extern long myth_tvguide_get_free_cardids(cmyth_conn_t control);
extern long myth_tvguide_get_active_card(cmyth_recorder_t rec);
extern int mvp_tvguide_sql_check(cmyth_database_t db);

/* ----------------------------------------------------------------- */

extern int mythtv_back(mvp_widget_t*);
extern int mythtv_update(mvp_widget_t*);
extern int mythtv_livetv_menu(void);
extern int mythtv_program_runtime(void);
extern void mythtv_set_popup_menu(mythtv_state_t state);

extern int mythtv_guide_menu(mvp_widget_t*);
extern int mythtv_guide_menu_move(mvp_widget_t* widget,int distance);

extern int mythtv_prog_finder_char_menu(mvp_widget_t*, mvp_widget_t*, mvp_widget_t*);
extern int mythtv_prog_finder_char_menu_right(int direction, mvp_widget_t*, mvp_widget_t*, mvp_widget_t*);
extern int mythtv_prog_finder_title_menu_right(mvp_widget_t*, mvp_widget_t*, mvp_widget_t*);

extern void mythtv_schedule_recording(mvp_widget_t*, char *item , void *key, int type);

extern void mythtv_show_widgets(void);
extern void mythtv_program(mvp_widget_t *widget);

extern int mythtv_livetv_stop(void);
extern int mythtv_channel_up(void);
extern int mythtv_channel_down(void);
extern int mythtv_channel_set(char * channame);

extern void mythtv_cleanup(void);
extern void mythtv_stop(void);
extern int mythtv_delete(void);
extern int mythtv_forget(void);
extern int mythtv_proginfo(char *buf, int size);
extern void mythtv_start_thumbnail(void);
extern int mythtv_pending(mvp_widget_t *widget);
extern int mythtv_pending_filter(mvp_widget_t *widget, mythtv_filter_t filter);
extern void mythtv_test_exit(void);
extern int mythtv_proginfo_livetv(char *buf, int size);
extern int mythtv_livetv_tuners(int*, int*);
extern void mythtv_livetv_select(int);
extern void mythtv_thruput(void);

extern void mythtv_exit(void);

extern void mythtv_browser_expose(mvp_widget_t *widget);

extern void mythtv_clear_channel();
extern void mythtv_key_callback(mvp_widget_t *widget, char key);

extern int mythtv_open(void);
extern int mythtv_read(char*, int);
extern long long mythtv_seek(long long, int);
extern long long mythtv_size(void);
extern void livetv_select_callback(mvp_widget_t *widget, char *item, void *key);
extern int mythtv_verify(void);
extern void mythtv_shutdown(int display);
extern void mythtv_fullscreen(void);

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

#endif /* MYTHTV_H */
