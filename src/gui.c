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

#include <mvp_widget.h>
#include <mvp_av.h>

#include "mvpmc.h"

static mvpw_menu_attr_t fb_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_attr_t settings_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_attr_t mythtv_attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_WHITE,
	.hilite_bg = MVPW_DARKGREY2,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_attr_t attr = {
	.font = 0,
	.fg = MVPW_BLACK,
	.hilite_fg = MVPW_BLACK,
	.hilite_bg = MVPW_WHITE,
	.title_fg = MVPW_WHITE,
	.title_bg = MVPW_BLUE,
};

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_WHITE,
	.bg = 0,
	.checkbox_fg = MVPW_GREEN,
};

static mvpw_menu_item_attr_t settings_item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

static mvpw_menu_item_attr_t sub_settings_item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

static mvpw_text_attr_t about_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 4,
	.font = 0,
	.fg = MVPW_BLACK,
};

static mvpw_text_attr_t description_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 4,
	.font = 0,
	.fg = MVPW_WHITE,
};

static mvpw_text_attr_t display_attr = {
	.wrap = 0,
	.justify = MVPW_TEXT_LEFT,
	.margin = 6,
	.font = 0,
	.fg = MVPW_WHITE,
};

mvpw_graph_attr_t offset_graph_attr = {
	.min = 0,
	.max = 100,
	.fg = 0x800000ff,
};

mvp_widget_t *root;
mvp_widget_t *iw;

static mvp_widget_t *main_menu;
static mvp_widget_t *mvpmc_logo;
static mvp_widget_t *settings;
static mvp_widget_t *sub_settings;
static mvp_widget_t *about;
static mvp_widget_t *setup_image;
static mvp_widget_t *fb_image;
static mvp_widget_t *mythtv_image;
static mvp_widget_t *about_image;
static mvp_widget_t *exit_image;

mvp_widget_t *file_browser;
mvp_widget_t *mythtv_browser;
mvp_widget_t *mythtv_logo;
mvp_widget_t *mythtv_date;
mvp_widget_t *mythtv_description;
mvp_widget_t *mythtv_channel;
mvp_widget_t *pause_widget;
mvp_widget_t *mute_widget;
mvp_widget_t *ffwd_widget;
mvp_widget_t *zoom_widget;
mvp_widget_t *osd_widget;
mvp_widget_t *offset_widget;
mvp_widget_t *offset_bar;
mvp_widget_t *bps_widget;

mvpw_screen_info_t si;

enum {
	MM_EXIT,
	MM_MYTHTV,
	MM_FILESYSTEM,
	MM_ABOUT,
	MM_SETTINGS,
};

enum {
	SETTINGS_MODE,
	SETTINGS_OUTPUT,
	SETTINGS_FLICKER,
	SETTINGS_ASPECT,
};

static void settings_select_callback(mvp_widget_t*, char*, void*);
static void sub_settings_select_callback(mvp_widget_t*, char*, void*);

static void
hide_widgets(void)
{
	mvpw_hide(main_menu);
	mvpw_hide(mvpmc_logo);
	mvpw_hide(file_browser);
	mvpw_hide(settings);
	mvpw_hide(sub_settings);
}

static void
root_callback(mvp_widget_t *widget, char key)
{
	video_callback(widget, key);
}

static void
iw_key_callback(mvp_widget_t *widget, char key)
{
	mvpw_show(file_browser);
	mvpw_focus(file_browser);
}

static void
about_key_callback(mvp_widget_t *widget, char key)
{
	mvpw_hide(about);
	mvpw_show(root);
	mvpw_expose(root);
	mvpw_show(main_menu);
	mvpw_expose(main_menu);
	mvpw_focus(main_menu);
}

static void
sub_settings_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'E') {
		mvpw_hide(settings);
		mvpw_hide(sub_settings);

		mvpw_show(main_menu);
		mvpw_show(mvpmc_logo);
		mvpw_show(setup_image);

		mvpw_focus(main_menu);
	}

	if (key == '<') {
		settings_attr.hilite_bg = MVPW_BLUE;
		mvpw_set_menu_attr(settings, &settings_attr);

		settings_attr.hilite_bg = MVPW_DARKGREY;
		mvpw_set_menu_attr(sub_settings, &settings_attr);

		mvpw_expose(settings);
		mvpw_expose(sub_settings);
		mvpw_focus(settings);
	}
}

static void
settings_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'E') {
		mvpw_hide(settings);
		mvpw_hide(sub_settings);

		mvpw_show(main_menu);
		mvpw_show(mvpmc_logo);
		mvpw_show(setup_image);

		mvpw_focus(main_menu);
	}

	if (key == '>') {
		settings_select_callback(NULL, NULL, NULL);
	}
}

static void
fb_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'E') {
		mvpw_hide(widget);
		mvpw_hide(iw);

		mvpw_show(main_menu);
		mvpw_show(mvpmc_logo);
		mvpw_show(fb_image);
		mvpw_focus(main_menu);

		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);

		audio_clear();
	}
}

static void
mythtv_key_callback(mvp_widget_t *widget, char key)
{
	if (key == 'E') {
		if (mythtv_back(widget) == 0) {
			mvpw_hide(mythtv_browser);
			mvpw_hide(mythtv_logo);
			mvpw_hide(mythtv_channel);
			mvpw_hide(mythtv_date);
			mvpw_hide(mythtv_description);

			mvpw_show(mythtv_image);
			mvpw_show(main_menu);
			mvpw_show(mvpmc_logo);
			mvpw_focus(main_menu);
		}
	}
}

static int
file_browser_init(void)
{
	file_browser = mvpw_create_menu(NULL, 50, 30, si.cols-120, si.rows-190,
					0xff808080, 0xff606060, 2);

	fb_attr.font = fontid;
	mvpw_set_menu_attr(file_browser, &fb_attr);

	mvpw_set_menu_title(file_browser, "/");
	mvpw_set_bg(file_browser, MVPW_LIGHTGREY);

	mvpw_set_key(file_browser, fb_key_callback);

	return 0;
}

static void
sub_settings_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	if ((strcmp(item, "PAL") == 0) || (strcmp(item, "NTSC") == 0)) {
		if (av_set_mode((int)key) < 0)
			printf("set mode to %s failed\n", item);
		else
			printf("set mode to %s\n", item);
	}

	if ((strcmp(item, "Composite") == 0) || (strcmp(item, "S-Video") == 0)) {
		if (av_set_output((int)key) < 0)
			printf("set output to %s failed\n", item);
		else
			printf("set output to %s\n", item);
	}

	if ((strcmp(item, "4:3") == 0) || (strcmp(item, "16:9") == 0)) {
		if (av_set_aspect((int)key) < 0)
			printf("set aspect to %s failed\n", item);
		else
			printf("set aspect to %s\n", item);
	}
}

static void
sub_settings_hilite_callback(mvp_widget_t *widget, char *item, void *key,
			     int hilite)
{
}

static void
settings_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	settings_attr.hilite_bg = MVPW_BLUE;
	mvpw_set_menu_attr(sub_settings, &settings_attr);

	settings_attr.hilite_bg = MVPW_DARKGREY;
	mvpw_set_menu_attr(settings, &settings_attr);

	mvpw_expose(settings);
	mvpw_expose(sub_settings);

	mvpw_focus(sub_settings);
}

static void
settings_hilite_callback(mvp_widget_t *widget, char *item, void *key,
			 int hilite)
{
	if (hilite) {
		mvpw_clear_menu(sub_settings);
		sub_settings_item_attr.hilite = sub_settings_hilite_callback;
		sub_settings_item_attr.select = sub_settings_select_callback;
		switch ((int)key) {
		case SETTINGS_MODE:
			mvpw_add_menu_item(sub_settings, "PAL",
					   (void*)AV_MODE_PAL,
					   &sub_settings_item_attr);
			mvpw_add_menu_item(sub_settings, "NTSC",
					   (void*)AV_MODE_NTSC,
					   &sub_settings_item_attr);
			mvpw_menu_hilite_item(sub_settings, av_get_mode());
			break;
		case SETTINGS_OUTPUT:
			mvpw_add_menu_item(sub_settings, "Composite",
					   (void*)AV_OUTPUT_COMPOSITE,
					   &sub_settings_item_attr);
			mvpw_add_menu_item(sub_settings, "S-Video",
					   (void*)AV_OUTPUT_SVIDEO,
					   &sub_settings_item_attr);
			mvpw_menu_hilite_item(sub_settings, av_get_output());
			break;
		case SETTINGS_FLICKER:
			break;
		case SETTINGS_ASPECT:
			mvpw_add_menu_item(sub_settings, "4:3",
					   (void*)AV_ASPECT_4x3,
					   &sub_settings_item_attr);
			mvpw_add_menu_item(sub_settings, "16:9",
					   (void*)AV_ASPECT_16x9,
					   &sub_settings_item_attr);
			mvpw_menu_hilite_item(sub_settings, av_get_aspect());
			break;
		}
	} else {
	}
}

static int
settings_init(void)
{
	int x, y, w, h;

	settings_attr.font = fontid;
	h = mvpw_font_height(description_attr.font) +
		(2 * description_attr.margin);
	w = (si.cols - 100) / 2;

	x = 50;
	y = (si.rows / 2) - (h * 2);

	settings = mvpw_create_menu(NULL, x, y, w, h*4,
				    0xff808080, 0xff606060, 2);

	settings_attr.hilite_bg = MVPW_BLUE;
	mvpw_set_key(settings, settings_key_callback);
	mvpw_set_menu_attr(settings, &settings_attr);

	mvpw_set_bg(settings, MVPW_LIGHTGREY);

	settings_attr.hilite_bg = MVPW_DARKGREY;
	x += w;
	sub_settings = mvpw_create_menu(NULL, x, y, w, h*4,
					0xff808080, 0xff606060, 2);
	mvpw_set_key(sub_settings, sub_settings_key_callback);
	mvpw_set_menu_attr(sub_settings, &settings_attr);
	mvpw_set_bg(sub_settings, MVPW_LIGHTGREY);

	mvpw_attach(settings, sub_settings, MVPW_DIR_RIGHT);

	settings_item_attr.hilite = settings_hilite_callback;
	settings_item_attr.select = settings_select_callback;
	mvpw_add_menu_item(settings, "TV Mode",
			   (void*)SETTINGS_MODE, &settings_item_attr);
	mvpw_add_menu_item(settings, "Output",
			   (void*)SETTINGS_OUTPUT, &settings_item_attr);
#if 0
	mvpw_add_menu_item(settings, "Flicker Control",
			   (void*)SETTINGS_FLICKER, &settings_item_attr);
#endif
	mvpw_add_menu_item(settings, "Aspect Ratio",
			   (void*)SETTINGS_ASPECT, &settings_item_attr);

	sub_settings_item_attr.hilite = sub_settings_hilite_callback;
	sub_settings_item_attr.select = sub_settings_select_callback;

	return 0;
}

static int
myth_browser_init(void)
{
	mvpw_image_info_t iid;
	mvpw_widget_info_t wid, wid2;
	int h;
	char path[] = "/usr/share/mvpmc";
	char file[128];

	snprintf(file, sizeof(file), "%s/mythtv_logo_rotate.png", path);
	mvpw_get_image_info(file, &iid);
	mythtv_logo = mvpw_create_image(NULL, 50, 25, iid.width, iid.height,
				       0, 0, 0);
	mvpw_set_image(mythtv_logo, file);

	mythtv_browser = mvpw_create_menu(NULL, 50, 30,
					  si.cols-120-iid.width, si.rows-190,
					  0xff808080, 0xff606060, 2);

	mvpw_attach(mythtv_logo, mythtv_browser, MVPW_DIR_RIGHT);

	mvpw_set_key(mythtv_browser, mythtv_key_callback);

	mythtv_attr.font = fontid;
	mvpw_set_menu_attr(mythtv_browser, &mythtv_attr);

	description_attr.font = fontid;
	h = mvpw_font_height(description_attr.font) +
		(2 * description_attr.margin);

	mythtv_channel = mvpw_create_text(NULL, 0, 0, 350, h,
					  MVPW_BLACK, 0, 0);
	mythtv_date = mvpw_create_text(NULL, 0, 0, 350, h,
				       MVPW_BLACK, 0, 0);
	mythtv_description = mvpw_create_text(NULL, 0, 0, 350, h*3,
					      MVPW_BLACK, 0, 0);

	mvpw_set_text_attr(mythtv_channel, &description_attr);
	mvpw_set_text_attr(mythtv_date, &description_attr);
	mvpw_set_text_attr(mythtv_description, &description_attr);

	mvpw_get_widget_info(mythtv_logo, &wid);
	mvpw_get_widget_info(mythtv_browser, &wid2);
	mvpw_moveto(mythtv_channel, wid.x, wid2.y+wid2.h);
	mvpw_get_widget_info(mythtv_channel, &wid2);
	mvpw_moveto(mythtv_date, wid.x, wid2.y+wid2.h);
	mvpw_get_widget_info(mythtv_date, &wid2);
	mvpw_moveto(mythtv_description, wid.x, wid2.y+wid2.h);

	mvpw_raise(mythtv_browser);

	return 0;
}

static void
main_select_callback(mvp_widget_t *widget, char *item, void *key)
{
	int k = (int)key;

	switch (k) {
	case MM_EXIT:
		exit(0);
		break;
	case MM_FILESYSTEM:
		mvpw_hide(main_menu);
		mvpw_hide(mvpmc_logo);
		mvpw_hide(fb_image);

		fb_update(file_browser);

		mvpw_show(file_browser);
		mvpw_focus(file_browser);
		break;
	case MM_SETTINGS:
		mvpw_hide(main_menu);
		mvpw_hide(setup_image);

		settings_attr.hilite_bg = MVPW_BLUE;
		mvpw_set_menu_attr(settings, &settings_attr);

		settings_attr.hilite_bg = MVPW_DARKGREY;
		mvpw_set_menu_attr(sub_settings, &settings_attr);

		mvpw_show(settings);
		mvpw_show(sub_settings);
		mvpw_focus(settings);
		break;
	case MM_MYTHTV:
		mvpw_hide(main_menu);
		mvpw_hide(mvpmc_logo);
		mvpw_hide(mythtv_image);

		mythtv_update(mythtv_browser);

		mvpw_show(mythtv_logo);
		mvpw_show(mythtv_browser);
		break;
	case MM_ABOUT:
		mvpw_show(about);
		mvpw_focus(about);
		break;
	}
}

static void
main_hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	int k = (int)key;

	if (hilite) {
		switch (k) {
		case MM_SETTINGS:
			mvpw_show(setup_image);
			break;
		case MM_FILESYSTEM:
			mvpw_show(fb_image);
			break;
		case MM_MYTHTV:
			mvpw_show(mythtv_image);
			break;
		case MM_ABOUT:
			mvpw_show(about_image);
			break;
		case MM_EXIT:
			mvpw_show(exit_image);
			break;
		}
	} else {
		switch (k) {
		case MM_SETTINGS:
			mvpw_hide(setup_image);
			break;
		case MM_FILESYSTEM:
			mvpw_hide(fb_image);
			break;
		case MM_MYTHTV:
			mvpw_hide(mythtv_image);
			break;
		case MM_ABOUT:
			mvpw_hide(about_image);
			break;
		case MM_EXIT:
			mvpw_hide(exit_image);
			break;
		}
	}
}

int
main_menu_init(void)
{
	mvpw_image_info_t iid;
	mvpw_widget_info_t wid;
	char path[] = "/usr/share/mvpmc";
	char file[128];

	snprintf(file, sizeof(file), "%s/wrench.png", path);
	mvpw_get_image_info(file, &iid);
	setup_image = mvpw_create_image(NULL, 50, 25,
					iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(setup_image, file);

	snprintf(file, sizeof(file), "%s/video_folder.png", path);
	mvpw_get_image_info(file, &iid);
	fb_image = mvpw_create_image(NULL, 50, 25,
				     iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(fb_image, file);

	snprintf(file, sizeof(file), "%s/tv2.png", path);
	mvpw_get_image_info(file, &iid);
	mythtv_image = mvpw_create_image(NULL, 50, 25,
					 iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(mythtv_image, file);

	snprintf(file, sizeof(file), "%s/unknown.png", path);
	mvpw_get_image_info(file, &iid);
	about_image = mvpw_create_image(NULL, 50, 25,
					iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(about_image, file);

	snprintf(file, sizeof(file), "%s/stop.png", path);
	mvpw_get_image_info(file, &iid);
	exit_image = mvpw_create_image(NULL, 50, 25,
				       iid.width, iid.height, 0, 0, 0);
	mvpw_set_image(exit_image, file);

	snprintf(file, sizeof(file), "%s/mvpmc_logo.png", path);
	mvpw_get_image_info(file, &iid);
	mvpmc_logo = mvpw_create_image(NULL, 50, 25, iid.width, iid.height,
				       0, 0, 0);
	mvpw_set_image(mvpmc_logo, file);

	main_menu = mvpw_create_menu(NULL, 50, 50, iid.width, si.rows-150,
				     0, 0, 0);

	mvpw_attach(mvpmc_logo, main_menu, MVPW_DIR_DOWN);
	mvpw_attach(main_menu, setup_image, MVPW_DIR_RIGHT);

	mvpw_get_widget_info(setup_image, &wid);
	mvpw_moveto(fb_image, wid.x, wid.y);
	mvpw_moveto(mythtv_image, wid.x, wid.y);
	mvpw_moveto(about_image, wid.x, wid.y);
	mvpw_moveto(exit_image, wid.x, wid.y);

	attr.font = fontid;
	mvpw_set_menu_attr(main_menu, &attr);

	item_attr.select = main_select_callback;
	item_attr.hilite = main_hilite_callback;

	mvpw_add_menu_item(main_menu, "MythTV",
			   (void*)MM_MYTHTV, &item_attr);
	mvpw_add_menu_item(main_menu, "Filesystem",
			   (void*)MM_FILESYSTEM, &item_attr);
	mvpw_add_menu_item(main_menu, "Settings",
			   (void*)MM_SETTINGS, &item_attr);
	mvpw_add_menu_item(main_menu, "About",
			   (void*)MM_ABOUT, &item_attr);
	mvpw_add_menu_item(main_menu, "Exit",
			   (void*)MM_EXIT, &item_attr);

	return 0;
}

static int
about_init(void)
{
	int x, y, w, h;

	x = 150;
	y = 150;
	w = si.cols - (x * 2);
	h = si.rows - (y * 2);

	about = mvpw_create_text(NULL, x, y, w, h,
				 MVPW_LIGHTGREY, MVPW_DARKGREY, 2);

	mvpw_set_text_str(about, "MediaMVP Media Center http://mvpmc.sourceforge.net/");
	mvpw_set_key(about, about_key_callback);

	about_attr.font = fontid;
	mvpw_set_text_attr(about, &about_attr);

	return 0;
}

static int
image_init(void)
{
	iw = mvpw_create_image(NULL, 0, 0, si.cols, si.rows, 0, 0, 0);
	mvpw_set_key(iw, iw_key_callback);

	return 0;
}

static int
osd_init(void)
{
	int h, w;

	display_attr.font = fontid;
	h = mvpw_font_height(display_attr.font) +
		(2 * display_attr.margin);
	w = mvpw_font_width(display_attr.font, " 100");

	/*
	 * State widgets for pause, mute, fast forward, zoom
	 */
	mute_widget = mvpw_create_text(NULL, 50, 25, 125, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(mute_widget, &display_attr);
	mvpw_set_text_str(mute_widget, "MUTE");

	pause_widget = mvpw_create_text(NULL, 50, 25, 125, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(pause_widget, &display_attr);
	mvpw_set_text_str(pause_widget, "PAUSE");

	ffwd_widget = mvpw_create_text(NULL, 50, 25, 125, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(ffwd_widget, &display_attr);
	mvpw_set_text_str(ffwd_widget, "FFWD");

	zoom_widget = mvpw_create_text(NULL, 50, 25, 125, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(zoom_widget, &display_attr);
	mvpw_set_text_str(zoom_widget, "ZOOM");

	mvpw_attach(mute_widget, pause_widget, MVPW_DIR_RIGHT);
	mvpw_attach(pause_widget, ffwd_widget, MVPW_DIR_RIGHT);
	mvpw_attach(ffwd_widget, zoom_widget, MVPW_DIR_RIGHT);

	/*
	 * OSD
	 */
	osd_widget = mvpw_create_container(NULL, 50, 80,
					   300, h*3, 0x80000000, 0, 0);

	offset_widget = mvpw_create_text(osd_widget,
					 0, 0, w, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(offset_widget, &display_attr);
	mvpw_show(offset_widget);

	offset_bar = mvpw_create_graph(osd_widget,
				       0, 0, 300-w, 10, 0x80000000, 0, 0);
	mvpw_set_graph_attr(offset_bar, &offset_graph_attr);
	mvpw_show(offset_bar);

	bps_widget = mvpw_create_text(osd_widget,
				      0, 0, 275, h, 0x80000000, 0, 0);
	mvpw_set_text_attr(bps_widget, &display_attr);
	mvpw_show(bps_widget);

	mvpw_attach(offset_widget, offset_bar, MVPW_DIR_RIGHT);
	mvpw_attach(offset_widget, bps_widget, MVPW_DIR_DOWN);

	mvpw_attach(mute_widget, osd_widget, MVPW_DIR_DOWN);

	return 0;
}

int
gui_init(void)
{
	mvpw_init();
	root = mvpw_get_root();
	mvpw_set_key(root, root_callback);

	mvpw_get_screen_info(&si);

	printf("screen is %d x %d\n", si.cols, si.rows);

	main_menu_init();
	myth_browser_init();
	file_browser_init();
	settings_init();
	about_init();
	image_init();
	osd_init();

	mvpw_show(mvpmc_logo);
	mvpw_show(main_menu);

	mvpw_focus(main_menu);

	return 0;
}
