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

extern int mythtv_init(char*, int);
extern void mythtv_atexit(void);

extern int mythtv_back(mvp_widget_t*);
extern int mythtv_update(mvp_widget_t*);
extern int mythtv_livetv_menu(void);
extern int mythtv_program_runtime(void);
extern void mythtv_set_popup_menu(mythtv_state_t state);

extern int mythtv_guide_menu(mvp_widget_t*, mvp_widget_t*);
extern int mythtv_guide_menu_next(mvp_widget_t*);
extern int mythtv_guide_menu_previous(mvp_widget_t*);

extern void mythtv_show_widgets(void);
extern void mythtv_program(mvp_widget_t *widget);

extern int mythtv_livetv_stop(void);
extern int mythtv_channel_up(void);
extern int mythtv_channel_down(void);

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

#endif /* MYTHTV_H */
