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

#include "mvpmc.h"

static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

static char cwd[1024] = "/";

static void add_dirs(mvp_widget_t*);
static void add_files(mvp_widget_t*);

char *current = NULL;

int
is_video(char *item)
{
	char *wc[] = { ".mpg", ".mpeg", ".nuv", ".vob", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

int
is_image(char *item)
{
	char *wc[] = { ".bmp", ".gif", ".jpg", ".jpeg", ".png", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

int
is_audio(char *item)
{
	char *wc[] = { ".mp3", ".ogg", ".wav", ".ac3", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

static int
is_playlist(char *item)
{
	char *wc[] = { ".m3u", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

static void
select_callback(mvp_widget_t *widget, char *item, void *key)
{
	char path[1024], *ptr;
	struct stat64 sb;

	sprintf(path, "%s/%s", cwd, item);
	stat64(path, &sb);

	if (S_ISDIR(sb.st_mode)) {
		if (strcmp(item, "../") == 0) {
			strcpy(path, cwd);
			if (path[strlen(path)-1] == '/')
				path[strlen(path)-1] = '\0';
			if ((ptr=strrchr(path, '/')) != NULL)
				*ptr = '\0';
			if (path[0] == '\0')
				sprintf(path, "/");
		} else {
			sprintf(path, "%s/%s", cwd, item);
			if ((ptr=strrchr(path, '/')) != NULL)
				*ptr = '\0';
		}

		strncpy(cwd, path, sizeof(cwd));

		while ((cwd[0] == '/') && (cwd[1] == '/'))
			memmove(cwd, cwd+1, strlen(cwd));

		mvpw_clear_menu(widget);
		mvpw_set_menu_title(widget, cwd);

		add_dirs(widget);
		add_files(widget);

		mvpw_expose(widget);
	} else {
		if (is_video(item)) {
			mvpw_hide(widget);
			av_move(0, 0, 0);
			mvpw_show(root);
			mvpw_expose(root);
			mvpw_focus(root);
		} else if (is_image(item)) {
			mvpw_hide(widget);
			mvpw_focus(iw);
		} else if (is_playlist(item)) {
			mvpw_hide(widget);
			printf("Show playlist menu\n");
			mvpw_show(playlist_widget);
			mvpw_focus(playlist_widget);
		}
	}
}

static void
hilite_callback(mvp_widget_t *widget, char *item, void *key, int hilite)
{
	char path[1024];
	struct stat64 sb;

	if (hilite) {
		sprintf(path, "%s/%s", cwd, item);

		stat64(path, &sb);

		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
		if (S_ISDIR(sb.st_mode)) {
		} else {
			if (current)
				free(current);
			current = strdup(path);
			playlist_clear();
			if (is_video(item)) {
				mvpw_set_timer(root, video_play, 500);
			} else if (is_audio(item)) {
				mvpw_set_timer(root, audio_play, 500);
			} else if (is_playlist(item)) {
				mvpw_set_timer(root, playlist_play, 500);
			} else if (is_image(item)) {
				mvpw_set_image(iw, path);
				mvpw_lower(iw);
				mvpw_show(iw);
			} else {
			}
		}
	} else {
		mvpw_hide(iw);
		mvpw_set_idle(NULL);
		mvpw_set_timer(root, NULL, 0);
		audio_clear();
		video_clear();
		playlist_clear();
	}
}

static void
add_dirs(mvp_widget_t *fbw)
{
	glob_t gb;
	char pattern[1024], buf[1024], *ptr;
	int i;
	struct stat64 sb;

	item_attr.select = select_callback;
	item_attr.hilite = hilite_callback;
	mvpw_add_menu_item(fbw, "../", 0, &item_attr);

	memset(&gb, 0, sizeof(gb));
	snprintf(pattern, sizeof(pattern), "%s/*", cwd);

	if (glob(pattern, GLOB_ONLYDIR, NULL, &gb) == 0) {
		i = 0;
		while (gb.gl_pathv[i]) {
			stat64(gb.gl_pathv[i], &sb);
			if (S_ISDIR(sb.st_mode)) {
				if ((ptr=strrchr(gb.gl_pathv[i], '/')) == NULL)
					ptr = gb.gl_pathv[i];
				else
					ptr++;
				sprintf(buf, ptr);
				strcat(buf, "/");
				mvpw_add_menu_item(fbw, buf,
						   (void*)(uint32_t)sb.st_ino,
						   &item_attr);
			}
			i++;
		}
	}
}

static void
do_glob(mvp_widget_t *fbw, char *wc[])
{
	int w, i;
	struct stat64 sb;
	glob_t gb;
	char pattern[1024], *ptr;

	w = 0;
	while (wc[w] != NULL) {
		memset(&gb, 0, sizeof(gb));
		snprintf(pattern, sizeof(pattern), "%s/%s", cwd, wc[w]);
		if (glob(pattern, 0, NULL, &gb) == 0) {
			i = 0;
			while (gb.gl_pathv[i]) {
				stat64(gb.gl_pathv[i], &sb);
				if ((ptr=strrchr(gb.gl_pathv[i], '/')) == NULL)
					ptr = gb.gl_pathv[i];
				else
					ptr++;
				mvpw_add_menu_item(fbw, ptr,
						   (void*)(uint32_t)sb.st_ino,
						   &item_attr);
				i++;
			}
		}
		w++;
	}
}

static void
add_files(mvp_widget_t *fbw)
{
	char *wc[] = { "*.mpg", "*.mpeg", "*.mp3", "*.nuv", "*.vob", "*.gif",
		       "*.bmp", "*.m3u", "*.jpg", "*.jpeg", "*.png", "*.wav",
		       "*.ac3", "*.ogg", NULL };
	char *WC[] = { "*.MPG", "*.MPEG", "*.MP3", "*.NUV", "*.VOB", "*.GIF",
		       "*.BMP", "*.M3U", "*.JPG", "*.JPEG", "*.PNG", "*.WAV",
		       "*.AC3", "*.OGG", NULL };

	item_attr.select = select_callback;
	item_attr.hilite = hilite_callback;

	do_glob(fbw, wc);
	do_glob(fbw, WC);
}

int
fb_update(mvp_widget_t *fb)
{
	video_functions = &file_functions;

	mvpw_show(root);
	mvpw_expose(root);

	mvpw_clear_menu(fb);
	mvpw_set_menu_title(fb, cwd);

	add_dirs(fb);
	add_files(fb);

	return 0;
}
