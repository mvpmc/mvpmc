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

#include <mvp_widget.h>
#include <mvp_av.h>
#include <cmyth.h>

#include "mvpmc.h"

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

static cmyth_conn_t control;
static cmyth_proginfo_t current_prog;
static cmyth_proglist_t episode_plist;
static cmyth_proginfo_t episode_prog;
static char *titles[1024];
static char *pathname = NULL;

static int level = 0;

int running_mythtv = 0;
int mythtv_debug = 0;

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
hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	const char *description, *channame;

	if (hilite) {
		cmyth_timestamp_t ts;
		char start[256], end[256], str[256], *ptr;

		current_prog = cmyth_proglist_get_item(episode_plist,
						       (int)key);

		channame = cmyth_proginfo_channame(current_prog);
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
		current = malloc(256);
		sprintf(current, "%s%s", mythtv_recdir, pathname);
		mvpw_set_timer(root, video_play, 500);

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
		video_clear();
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
	}
}

static void
show_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	mvpw_hide(mythtv_logo);
	mvpw_hide(mythtv_channel);
	mvpw_hide(mythtv_date);
	mvpw_hide(mythtv_description);

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
	int err, count, i, n = 0;

	level = 1;

	item_attr.select = show_select_callback;
	item_attr.hilite = hilite_callback;

	mvpw_set_menu_title(widget, item);
	mvpw_clear_menu(widget);

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
		episode_prog = cmyth_proglist_get_item(episode_plist, i);
		title = cmyth_proginfo_title(episode_prog);
		if (strcmp(title, item) == 0) {
			subtitle = cmyth_proginfo_subtitle(episode_prog);
			if (strcmp(subtitle, " ") == 0)
				subtitle = "<no subtitle>";
			mvpw_add_menu_item(widget, subtitle, (void*)n,
					   &item_attr);
		}
		n++;
	}
}

static void
add_shows(mvp_widget_t *widget)
{
	cmyth_proglist_t plist;
	cmyth_proginfo_t prog;
	int err, count;
	int i, j, n = 0;
	const char *title;

	item_attr.select = select_callback;
	item_attr.hilite = NULL;

	if ((plist=cmyth_proglist_create()) == NULL) {
		fprintf(stderr, "cannot get program list\n");
		return;
	}

	if ((err=cmyth_proglist_get_all_recorded(control, plist)) < 0) {
		fprintf(stderr, "get recorded failed, err %d\n", err);
		return;
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
	}

	qsort(titles, n, sizeof(char*), string_compare);

	for (i=0; i<n; i++)
		mvpw_add_menu_item(widget, titles[i], (void*)n, &item_attr);

	cmyth_proglist_release(plist);
}

int
mythtv_update(mvp_widget_t *widget)
{
	running_mythtv = 1;

	mvpw_show(root);
	mvpw_expose(root);

	video_clear();
	mvpw_set_idle(NULL);
	mvpw_set_timer(root, NULL, 0);

	if (control == NULL)
		mythtv_init(mythtv_server, -1);

	mvpw_set_menu_title(widget, "MythTV");
	mvpw_clear_menu(widget);
	add_shows(widget);

	mvpw_show(mythtv_channel);
	mvpw_show(mythtv_date);
	mvpw_show(mythtv_description);

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

	if (level == 0) {
		running_mythtv = 0;
		return 0;
	}

	level = 0;
	mythtv_update(widget);

	return -1;
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

	return 0;
}
