/*
 *  Copyright (C) 2005, Jon Gettler
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
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include <mvp_widget.h>
#include <mvp_demux.h>
#include <mvp_av.h>

#include "expat.h"
#include "mvpmc.h"
#include "mythtv.h"
#include "colorlist.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

static char* theme_name(char *file);

theme_t theme_list[THEME_MAX+1];

typedef struct {
	XML_Parser	 p;
	void		*data;
	int		 depth;
	char		*cur;
	int		 cur_attr;
	int		 cur_type;
	char		*theme_err;
	char		*name;
} parser_data_t;

enum {
	TYPE_WIDGET = 1,
	TYPE_SETTINGS,
};

static int tag_font(parser_data_t*, const char*, const char**, char*);
static int tag_include(parser_data_t*, const char*, const char**, char*);
static int tag_mvpmctheme(parser_data_t*, const char*, const char**);
static int tag_settings(parser_data_t*, const char*, const char**);
static int tag_sinclude(parser_data_t*, const char*, const char**, char*);
static int tag_widget(parser_data_t*, const char*, const char**);

typedef struct {
	char *tag;
	int (*func)(parser_data_t*, const char*, const char**);
	int (*vfunc)(parser_data_t*, const char*, const char**, char*);
} theme_tag_t;

static theme_tag_t tags[] = {
	{ .tag = "font",	.vfunc = tag_font },
	{ .tag = "include",	.vfunc = tag_include },
	{ .tag = "sinclude",	.vfunc = tag_sinclude },
	{ .tag = "mvpmctheme",	.func = tag_mvpmctheme },
	{ .tag = "settings",	.func = tag_settings },
	{ .tag = "widget",	.func = tag_widget },
	{ .tag = NULL }
};

static int tag_widget_font(parser_data_t*, const char*, const char**, char*);
static int tag_widget_color(parser_data_t*, const char*, const char**, char*);
static int tag_widget_style(parser_data_t*, const char*, const char**, char*);

static theme_tag_t tags_widget[] = {
	{ .tag = "font",	.vfunc = tag_widget_font },
	{ .tag = "color",	.vfunc = tag_widget_color },
	{ .tag = "style",	.vfunc = tag_widget_style },
	{ .tag = NULL }
};

static int tag_settings_item(parser_data_t*, const char*, const char**, char*);
static int tag_settings_color(parser_data_t*, const char*, const char**, char*);

static theme_tag_t tags_settings[] = {
	{ .tag = "item",	.vfunc = tag_settings_item },
	{ .tag = "color",	.vfunc = tag_settings_color },
	{ .tag = NULL }
};

static int tag_settings_screensaver(parser_data_t*, const char*, const char**,
				    char*);
static int tag_settings_themes(parser_data_t*, const char*, const char**,
			       char*);
static int tag_settings_video(parser_data_t*, const char*, const char**,
			      char*);
static int tag_settings_mythtv_livetv(parser_data_t*, const char*,
				      const char**, char*);
static int tag_settings_mythtv_pending(parser_data_t*, const char*,
				       const char**, char*);
static int tag_settings_osd(parser_data_t*, const char*, const char**, char*);

static theme_tag_t tag_settings_names[] = {
	{ .tag = "mythtv_livetv",	.vfunc = tag_settings_mythtv_livetv },
	{ .tag = "mythtv_pending",	.vfunc = tag_settings_mythtv_pending },
	{ .tag = "osd",		.vfunc = tag_settings_osd },
	{ .tag = "screensaver",	.vfunc = tag_settings_screensaver },
	{ .tag = "themes",	.vfunc = tag_settings_themes },
	{ .tag = "video",	.vfunc = tag_settings_video },
	{ .tag = NULL }
};

typedef struct {
	char *tag;
	char *attr[9];
	int (*func)(parser_data_t*, const char*, const char**, char*);
	char *value;
} theme_data_t;

typedef struct {
	char *name;
	char *file;
	int id;
} theme_font_t;

#define MAX_FONTS	16
static theme_font_t fonts[MAX_FONTS+1];

static void
theme_fail(parser_data_t *pdata)
{
	if (pdata->theme_err)
		fprintf(stderr, "Error '%s' at line %d in theme file\n",
			pdata->theme_err, XML_GetCurrentLineNumber(pdata->p));
	else
		fprintf(stderr, "Error at line %d in theme file\n",
			XML_GetCurrentLineNumber(pdata->p));
	exit(65);
}

static int
find_font(char *str, int *id, parser_data_t *pdata)
{
	int i = 0;

	while (fonts[i].name) {
		if (strcasecmp(fonts[i].name, str) == 0) {
			*id = fonts[i].id;
			PRINTF("using font %d\n", *id);
			return 0;
		}
		i++;
	}

	pdata->theme_err = "unknown font";

	return -1;
}

static int
tag_widget_font(parser_data_t *pdata, const char *el, const char **attr,
		char *value)
{
	char *tok;
	int *font;
	int cur_attr = pdata->cur_attr;

	if ((tok=strtok(value, " \t\r\n")) == NULL)
		return -1;

	PRINTF("WIDGET FONT: '%s' '%s'\n", el, value);

	switch (theme_attr[cur_attr].type) {
	case WIDGET_TEXT:
		font = &theme_attr[cur_attr].attr.text->font;
		break;
	case WIDGET_MENU:
		font = &theme_attr[cur_attr].attr.menu->font;
		break;
	case WIDGET_DIALOG:
		font = &theme_attr[cur_attr].attr.dialog->font;
		break;
	default:
		pdata->theme_err = "invalid widget type";
		return -1;
	}

	if (find_font(value, font, pdata) < 0)
		return -1;

	return 0;
}

static int
tag_widget_color(parser_data_t *pdata, const char *el, const char **attr,
		 char *value)
{
	char *tok;
	unsigned int *color;
	int cur_attr = pdata->cur_attr;

	if ((attr[0] == NULL) || (attr[1] == NULL))
		return -1;
	if (strcasecmp(attr[0], "type") != 0)
		return -1;

	if ((tok=strtok(value, " \t\r\n")) == NULL)
		return -1;

	PRINTF("WIDGET COLOR: '%s' '%s'\n", el, value);

	if (strcasecmp(attr[1], "fg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			color = &theme_attr[cur_attr].attr.text->fg;
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->fg;
			break;
		case WIDGET_GRAPH:
			color = &theme_attr[cur_attr].attr.graph->fg;
			break;
		case WIDGET_DIALOG:
			color = &theme_attr[cur_attr].attr.dialog->fg;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "bg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			color = &theme_attr[cur_attr].attr.text->bg;
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->bg;
			break;
		case WIDGET_GRAPH:
			color = &theme_attr[cur_attr].attr.graph->bg;
			break;
		case WIDGET_DIALOG:
			color = &theme_attr[cur_attr].attr.dialog->bg;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "hilite_bg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			pdata->theme_err = "invalid attribute";
			theme_fail(pdata);
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->hilite_bg;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "hilite_fg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			pdata->theme_err = "invalid attribute";
			theme_fail(pdata);
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->hilite_fg;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "title_bg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			pdata->theme_err = "invalid attribute";
			theme_fail(pdata);
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->title_bg;
			break;
		case WIDGET_DIALOG:
			color = &theme_attr[cur_attr].attr.dialog->title_bg;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "title_fg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			pdata->theme_err = "invalid attribute";
			theme_fail(pdata);
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->title_fg;
			break;
		case WIDGET_DIALOG:
			color = &theme_attr[cur_attr].attr.dialog->title_fg;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "border") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			color = &theme_attr[cur_attr].attr.text->border;
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->border;
			break;
		case WIDGET_GRAPH:
			color = &theme_attr[cur_attr].attr.graph->border;
			break;
		case WIDGET_DIALOG:
			color = &theme_attr[cur_attr].attr.dialog->border;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "checkbox") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->checkbox_fg;
			theme_attr[cur_attr].attr.menu->checkboxes = 1;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "left") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_GRAPH:
			color = &theme_attr[cur_attr].attr.graph->left;
			theme_attr[cur_attr].attr.graph->gradient = 1;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "right") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_GRAPH:
			color = &theme_attr[cur_attr].attr.graph->right;
			theme_attr[cur_attr].attr.graph->gradient = 1;
			break;
		default:
			return -1;
		}
	} else {
		pdata->theme_err = "unknown attribute";
		return -1;
	}

	if (find_color(value, color) < 0) {
		pdata->theme_err = "unknown color";
		return -1;
	}

	if (attr[2]) {
		int alpha;

		if (strcasecmp(attr[2], "alpha") != 0) {
			pdata->theme_err = "unknown attribute";
			return -1;
		}

		alpha = (atoi(attr[3]) / 100.0) * 255;

		if ((alpha < 0) || (alpha > 255)) {
			pdata->theme_err = "invalid alpha value";
			return -1;
		}

		*color = mvpw_color_alpha(*color, alpha);
	}

	return 0;
}

static int
tag_widget_style(parser_data_t *pdata, const char *el, const char **attr,
		 char *value)
{
	char *tok;
	int cur_attr = pdata->cur_attr;

	if ((attr[0] == NULL) || (attr[1] == NULL))
		return -1;
	if (strcasecmp(attr[0], "type") != 0)
		return -1;

	if ((tok=strtok(value, " \t\r\n")) == NULL)
		return -1;

	PRINTF("STYLE '%s'\n", tok);

	if (strcasecmp(attr[1], "hilite") == 0) {
		if (theme_attr[cur_attr].type != WIDGET_MENU)
			return -1;

		if (strcasecmp(value, "rounded") == 0) {
			theme_attr[cur_attr].attr.menu->rounded = 1;
		} else if (strcasecmp(value, "square") == 0) {
			theme_attr[cur_attr].attr.menu->rounded = 0;
		} else {
			return -1;
		}
	} else if (strcasecmp(attr[1], "border_size") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_MENU:
			theme_attr[cur_attr].attr.menu->border_size = atoi(value);
			break;
		case WIDGET_TEXT:
			theme_attr[cur_attr].attr.text->border_size = atoi(value);
			break;
		case WIDGET_GRAPH:
			theme_attr[cur_attr].attr.graph->border_size = atoi(value);
			break;
		case WIDGET_DIALOG:
			theme_attr[cur_attr].attr.dialog->border_size = atoi(value);
			break;
		default:
			pdata->theme_err = "unknown attribute";
			return -1;
			break;
		}
	} else {
		pdata->theme_err = "unknown attribute";
		return -1;
	}

	return 0;
}

static int
tag_settings_screensaver(parser_data_t *pdata, const char *el,
			 const char **attr, char *value)
{
	if (strcasecmp(attr[1], "timeout") == 0) {
		int to;

		to = atoi(value);
		if ((to < 0) || (to > 3600)) {
			pdata->theme_err = "invalid screensaver timeout";
			return -1;
		}

		screensaver_default = to;
		screensaver_timeout = to;
	} else {
		pdata->theme_err = "unknown item";
		return -1;
	}

	return 0;
}

static int
tag_settings_osd(parser_data_t *pdata, const char *el,
		 const char **attr, char *value)
{
	int setting;

	if (strcasecmp(value, "on") == 0) {
		setting = 1;
	} else if (strcasecmp(value, "off") == 0) {
		setting = 0;
	} else {
		pdata->theme_err = "invalid value";
		return -1;
	}

	if (strcasecmp(attr[1], "bitrate") == 0) {
		osd_settings.bitrate = setting;
	} else if (strcasecmp(attr[1], "clock") == 0) {
		osd_settings.clock = setting;
	} else if (strcasecmp(attr[1], "demux_info") == 0) {
		osd_settings.demux_info = setting;
	} else if (strcasecmp(attr[1], "progress") == 0) {
		osd_settings.progress = setting;
	} else if (strcasecmp(attr[1], "program") == 0) {
		osd_settings.program = setting;
	} else if (strcasecmp(attr[1], "timecode") == 0) {
		osd_settings.timecode = setting;
	} else {
		pdata->theme_err = "unknown item";
		return -1;
	}

	return 0;
}

static int
add_theme_file(parser_data_t *pdata, char *file)
{
	int i;
	char *name;

	name = theme_name(file);

	for (i=0; i<THEME_MAX; i++) {
		if (theme_list[i].path == NULL) {
			theme_list[i].name = name;
			theme_list[i].path = strdup(file);
			return 0;
		}
	}

	pdata->theme_err = "too many theme files";
	return -1;
}

static int
add_theme_dir(parser_data_t *pdata, char *dir)
{
	DIR *dp;
	struct dirent *de;
	int ret = -1;
	char buf[256];
	struct stat sb;

	/*
	 * Ignore directories that do not exist
	 */
	if ((dp=opendir(dir)) == NULL) {
		fprintf(stderr, "%s(): directory '%s' not found\n",
			__FUNCTION__, dir);
		return 0;
	}

	while ((de=readdir(dp)) != NULL) {
		int len = strlen(de->d_name);
		if ((len > 4) &&
		    (strcasecmp(de->d_name+len-4, ".xml") == 0)) {
			snprintf(buf, sizeof(buf), "%s/%s", dir, de->d_name);
			if ((stat(buf, &sb) == 0) && (sb.st_size > 0)) {
				if (add_theme_file(pdata, buf) < 0)
					goto err;
				if (strcmp(de->d_name, "default.xml") == 0) {
					unlink(DEFAULT_THEME);
					if (symlink(buf, DEFAULT_THEME) != 0)
						return -1;
				}
			}
		}
	}

	ret = 0;

 err:
	closedir(dp);

	return ret;
}

static int
tag_settings_themes(parser_data_t *pdata, const char *el, const char **attr,
		    char *value)
{
	struct stat sb;

	if (strcasecmp(attr[1], "default") == 0) {
		if (stat(value, &sb) != 0) {
			pdata->theme_err = "no such file";
			return -1;
		}
		unlink(DEFAULT_THEME);
		if (symlink(value, DEFAULT_THEME) != 0)
			return -1;
		return add_theme_file(pdata, value);
	} else if (strcasecmp(attr[1], "alternate") == 0) {
		if (stat(value, &sb) != 0) {
			pdata->theme_err = "no such file";
			return -1;
		}
		return add_theme_file(pdata, value);
	} else if (strcasecmp(attr[1], "directory") == 0) {
		return add_theme_dir(pdata, value);
	}

	pdata->theme_err = "unknown item";
	return -1;
}

static int
tag_settings_video(parser_data_t *pdata, const char *el, const char **attr,
		   char *value)
{
	if (strcasecmp(attr[1], "seek_osd_timeout") == 0) {
		int to;

		to = atoi(value);
		if ((to < 0) || (to > 3600)) {
			pdata->theme_err = "invalid osd timeout";
			return -1;
		}

		seek_osd_timeout = to;
	} else if (strcasecmp(attr[1], "pause_osd") == 0) {
		int val;

		val = atoi(value);
		pause_osd = val;
	} else {
		pdata->theme_err = "unknown item";
		return -1;
	}

	return 0;
}

static int
tag_settings_mythtv_livetv(parser_data_t *pdata, const char *el,
			   const char **attr, char *value)
{
	unsigned int color;

	if (find_color(value, &color) < 0) {
		pdata->theme_err = "unknown color";
		return -1;
	}

	if (strcasecmp(attr[1], "current") == 0) {
		mythtv_colors.livetv_current = color;
	} else {
		pdata->theme_err = "unknown item";
		return -1;
	}

	return 0;
}

static int
tag_settings_mythtv_pending(parser_data_t *pdata, const char *el,
			    const char **attr, char *value)
{
	unsigned int color;

	if (find_color(value, &color) < 0) {
		pdata->theme_err = "unknown color";
		return -1;
	}

	if (strcasecmp(attr[1], "recording") == 0) {
		mythtv_colors.pending_recording = color;
	} else if (strcasecmp(attr[1], "will_record") == 0) {
		mythtv_colors.pending_will_record = color;
	} else if (strcasecmp(attr[1], "conflict") == 0) {
		mythtv_colors.pending_conflict = color;
	} else if (strcasecmp(attr[1], "other") == 0) {
		mythtv_colors.pending_other = color;
	} else {
		pdata->theme_err = "unknown item";
		return -1;
	}

	return 0;
}

static int
tag_settings_item(parser_data_t *pdata, const char *el, const char **attr,
		  char *value)
{
	char *tok;
	int cur_attr = pdata->cur_attr;

	if ((tok=strtok(value, " \t\r\n")) == NULL)
		return -1;

	PRINTF("SETTINGS ITEM: '%s' '%s'\n", el, value);

	if ((strcasecmp(attr[0], "name") != 0) || (attr[2] != NULL)) {
		pdata->theme_err = "unknown attribute";
		return -1;
	}

	return tag_settings_names[cur_attr].vfunc(pdata, el, attr, value);
}

static int
tag_settings_color(parser_data_t *pdata, const char *el, const char **attr,
		   char *value)
{
	char *tok;
	int cur_attr = pdata->cur_attr;

	if ((tok=strtok(value, " \t\r\n")) == NULL)
		return -1;

	PRINTF("SETTINGS COLOR: '%s' '%s'\n", el, value);

	if ((strcasecmp(attr[0], "type") != 0) || (attr[2] != NULL)) {
		pdata->theme_err = "unknown attribute";
		return -1;
	}

	return tag_settings_names[cur_attr].vfunc(pdata, el, attr, value);
}

static int
tag_font(parser_data_t *pdata, const char *el, const char **attr, char *value)
{
	char *name = NULL, *file = NULL;
	int i, fontid;

	file = value;

	for (i=0; attr[i]; i+=2) {
		if (strcasecmp(attr[i], "name") == 0) {
			name = (char*)attr[i+1];
		} else {
			return -1;
		}
	}

	if ((name == NULL) || (file == NULL))
		return -1;

	i = 0;
	while ((i < MAX_FONTS) && (fonts[i].name != NULL)) {
		if (strcmp(fonts[i].file, file) == 0) {
			if (strcmp(fonts[i].name, name) != 0) {
				int j = 0;
				while ((j < MAX_FONTS) &&
				       (fonts[j].name != NULL)) {
					j++;
				}
				if (j < MAX_FONTS) {
					fonts[j].name = strdup(name);
					fonts[j].id = fonts[i].id;
					return 0;
				}
				return -1;
			}
			return 0;
		}
		if (strcmp(fonts[i].name, name) == 0) {
			/*
			 * XXX: should we unload the font?
			 */
			free(fonts[i].name);
			free(fonts[i].file);
			fonts[i].name = NULL;
			fonts[i].file = NULL;
			break;
		}
		i++;
	}

#ifdef MVPMC_HOST
	fontid = 0;
#else
	if ((fontid=mvpw_load_font(file)) <= 0)
		theme_fail(pdata);
#endif /* MVPMC_HOST */

	i = 0;
	while ((i < MAX_FONTS) && (fonts[i].name != NULL)) {
		i++;
	}

	if (i < MAX_FONTS) {
		PRINTF("ADD FONT: '%s'\n", name);
		fonts[i].name = strdup(name);
		fonts[i].file = strdup(file);
		fonts[i].id = fontid;
	} else {
		return -1;
	}

	if (i == MAX_FONTS)
		return -1;

	return 0;
}

static int
tag_include(parser_data_t *pdata, const char *el, const char **attr,
	    char *value)
{
	theme_parse(value);

	return 0;
}

static int
tag_sinclude(parser_data_t *pdata, const char *el, const char **attr,
	     char *value)
{
	struct stat sb;

	if (stat(value, &sb) == 0)
		return tag_include(pdata, el, attr, value);

	return 0;
}

static int
tag_mvpmctheme(parser_data_t *pdata, const char *el, const char **attr)
{
	int i;
	int ret = -1;

	for (i=0; attr[i]; i+=2) {
		if (strcasecmp(attr[i], "version") == 0) {
			if (strcasecmp(attr[i+1], "0") != 0) {
				pdata->theme_err = "invalid theme version";
				return -1;
			} else {
				ret = 0;
			}
		} else if (strcasecmp(attr[i], "name") == 0) {
			if (pdata->name)
				free(pdata->name);
			pdata->name = strdup(attr[i+1]);
		} else {
			pdata->theme_err = "unknown attribute";
			return -1;
		}
	}

	return ret;
}

static int
tag_settings(parser_data_t *pdata, const char *el, const char **attr)
{
	int i;
	char *type = NULL;

	for (i=0; attr[i]; i+=2) {
		if (strcasecmp(attr[i], "name") == 0) {
			type = strdup(attr[i+1]);
		} else {
			pdata->theme_err = "unknown attribute";
			goto err;
		}
	}

	if (type == NULL) {
		pdata->theme_err = "unknown setting";
		goto err;
	}

	i = 0;
	while (tag_settings_names[i].tag != NULL) {
		if (strcasecmp(type, tag_settings_names[i].tag) == 0)
			break;
		i++;
	}

	if (tag_settings_names[i].tag == NULL)
		goto err;
	pdata->cur_type = TYPE_SETTINGS;
	pdata->cur_attr = i;

	return 0;

 err:
	return -1;
}

static int
tag_widget(parser_data_t *pdata, const char *el, const char **attr)
{
	int i;
	char *type = NULL, *name = NULL, *copy = NULL;

	for (i=0; attr[i]; i+=2) {
		if (strcasecmp(attr[i], "type") == 0) {
			type = strdup(attr[i+1]);
		} else if (strcasecmp(attr[i], "name") == 0) {
			name = strdup(attr[i+1]);
		} else if (strcasecmp(attr[i], "copy") == 0) {
			copy = strdup(attr[i+1]);
		} else {
			goto err;
		}
	}

	if ((type == NULL) || (name == NULL))
		goto err;

	i = 0;
	while (theme_attr[i].name) {
		if (strcasecmp(theme_attr[i].name, name) == 0) {
			break;
		}
		i++;
	}

	if (theme_attr[i].name == NULL) {
		pdata->theme_err = "unknown widget";
		goto err;
	}

	pdata->cur_attr = i;
	pdata->cur_type = TYPE_WIDGET;

	if (copy) {
		int cur_attr = pdata->cur_attr;
		int utf8;

		i = 0;
		while (theme_attr[i].name) {
			if (strcasecmp(theme_attr[i].name, copy) == 0) {
				break;
			}
			i++;
		}
		if (theme_attr[i].name == NULL)
			goto err;
		if (i == cur_attr)
			goto err;
		if (theme_attr[i].type != theme_attr[cur_attr].type)
			goto err;

		/*
		 * XXX: Not copying utf8 is a hack, since that item is
		 *      not themeable at the moment.
		 */

		switch (theme_attr[i].type) {
		case WIDGET_TEXT:
			utf8 = theme_attr[cur_attr].attr.text->utf8;
			memcpy(theme_attr[cur_attr].attr.text,
			       theme_attr[i].attr.text,
			       sizeof(*(theme_attr[i].attr.text)));
			theme_attr[cur_attr].attr.text->utf8 = utf8;
			break;
		case WIDGET_MENU:
			utf8 = theme_attr[cur_attr].attr.menu->utf8;
			memcpy(theme_attr[cur_attr].attr.menu,
			       theme_attr[i].attr.menu,
			       sizeof(*(theme_attr[i].attr.menu)));
			theme_attr[cur_attr].attr.menu->utf8 = utf8;
			break;
		case WIDGET_GRAPH:
			memcpy(theme_attr[cur_attr].attr.graph,
			       theme_attr[i].attr.graph,
			       sizeof(*(theme_attr[i].attr.graph)));
			break;
		case WIDGET_DIALOG:
			utf8 = theme_attr[cur_attr].attr.dialog->utf8;
			memcpy(theme_attr[cur_attr].attr.dialog,
			       theme_attr[i].attr.dialog,
			       sizeof(*(theme_attr[i].attr.dialog)));
			theme_attr[cur_attr].attr.dialog->utf8 = utf8;
			break;
		}
	}

	return 0;

 err:
	return -1;
}

static int
call_func(const char *el, const char **attr, theme_tag_t *tag,
	  parser_data_t *pdata)
{
	theme_data_t *udata;
	int i;

	if (tag->func) {
		if (tag->func(pdata, el, attr) != 0)
			goto err;
	} else {
		if ((udata=malloc(sizeof(*udata))) == NULL)
			goto err;
		memset(udata, 0, sizeof(*udata));
		udata->tag = strdup(el);
		for (i=0; attr[i]; i++)
			udata->attr[i] = strdup(attr[i]);
		udata->func = tag->vfunc;
		pdata->data = udata;
	}

	return 0;

 err:
	return -1;
}

static void XMLCALL
value(void *data, const char *el, int len)
{
	char *buf;
	char *val;
	parser_data_t *pdata = (parser_data_t*)data;
	theme_data_t *udata = pdata->data;

	buf = alloca(len+1);
	strncpy(buf, el, len);
	buf[len] = '\0';

	if ((val=strtok(buf, " \t\r\n")) != NULL) {
		if (data) {
			PRINTF("DATA ");
			if (udata->value) {
				udata->value = realloc(udata->value,
						       strlen(udata->value)+
						       strlen(val)+1);
				if (udata->value == NULL) {
					pdata->theme_err = "out of memory";
					theme_fail(pdata);
				}
				strcat(udata->value, val);
			} else {
				udata->value = strdup(val);
			}
		}

		PRINTF("value='%s'\n", val);
	}
}

static void XMLCALL
start(void *data, const char *el, const char **attr)
{
	int i;
	parser_data_t *pdata = (parser_data_t*)data;

	PRINTF("<%s", el);
	for (i=0; attr[i]; i+=2)
		PRINTF(" %s='%s'", attr[i], attr[i+1]);
	PRINTF(">\n");

	if (i >= 8)
		goto err;

	switch (pdata->depth) {
	case 0:
		if (strcasecmp(el, "mvpmctheme") != 0)
			goto err;
	case 1:
		i = 0;
		while (tags[i].tag) {
			if (strcasecmp(tags[i].tag, el) == 0) {
				if (call_func(el, attr, tags+i, pdata) != 0)
					goto err;
				break;
			}
			i++;
		}

		if (tags[i].tag == NULL)
			goto err;
		break;
	case 2:
		switch (pdata->cur_type) {
		case TYPE_WIDGET:
			i = 0;
			while (tags_widget[i].tag) {
				if (strcasecmp(tags_widget[i].tag, el) == 0) {
					if (call_func(el, attr, tags_widget+i,
						      pdata) != 0)
						goto err;
					break;
				}
				i++;
			}

			if (tags[i].tag == NULL)
				goto err;
			break;
		case TYPE_SETTINGS:
			i = 0;
			while (tags_settings[i].tag) {
				if (strcasecmp(tags_settings[i].tag, el) == 0) {
					if (call_func(el, attr,
						      tags_settings+i,
						      pdata) != 0)
						goto err;
					break;
				}
				i++;
			}

			if (tags[i].tag == NULL)
				goto err;
			break;
		default:
			goto err;
			break;
		}
		break;
	default:
		goto err;
		break;
	}

	if (++pdata->depth == 2)
		pdata->cur = strdup(el);

	return;

 err:
	theme_fail(pdata);
}

static void XMLCALL
end(void *data, const char *el)
{
	parser_data_t *pdata = (parser_data_t*)data;
	theme_data_t *udata = (theme_data_t*)pdata->data;

	PRINTF("</%s>\n", el);

	if (udata && udata->value) {
		PRINTF("value '%s'\n", udata->value);
		if (udata->func(pdata, udata->tag,
				(const char**)udata->attr, udata->value) < 0)
			theme_fail(pdata);
		free(udata->value);
	}

	if (udata)
		free(udata);

	pdata->data = NULL;

	if (--pdata->depth == 1) {
		if (pdata->cur)
			free(pdata->cur);
		pdata->cur = NULL;
		pdata->cur_attr = -1;
	}
}

int
theme_parse(char *file)
{
	FILE *f;
	int len, done = 0;
	char buf[1024];
	parser_data_t pdata;
	char *base;
	extern char *basename(char*);

	if ((f=fopen(file, "r")) == NULL) {
		perror(file);
		exit(1);
	}

	pdata.p = XML_ParserCreate(NULL);
	if (pdata.p) {
		pdata.data = NULL;
		pdata.cur = NULL;
		pdata.theme_err = NULL;
		pdata.depth = 0;
		pdata.cur_attr = -1;
		pdata.cur_type = -1;
		if ((base=basename(file)) == NULL) {
			fprintf(stderr, "bad file '%s'\n", file);
			exit(1);
		}
		pdata.name = strdup(base);
		XML_SetElementHandler(pdata.p, start, end);
		XML_SetCharacterDataHandler(pdata.p, value);
		XML_SetUserData(pdata.p, (void*)&pdata);
	}

	while (1) {
		len = fread(buf, 1, sizeof(buf), f);
		done = feof(f);
		XML_Parse(pdata.p, buf, len, done);
		if (done)
			break;
	}

	fclose(f);

	return 0;
}

static void XMLCALL
value_name(void *data, const char *el, int len)
{
}

static void XMLCALL
start_name(void *data, const char *el, const char **attr)
{
	int i;
	parser_data_t *pdata = (parser_data_t*)data;

	PRINTF("<%s", el);
	for (i=0; attr[i]; i+=2)
		PRINTF(" %s='%s'", attr[i], attr[i+1]);
	PRINTF(">\n");

	switch (pdata->depth) {
	case 0:
		if (strcasecmp(el, "mvpmctheme") != 0)
			return;
		pdata->name = NULL;
		for (i=0; attr[i]; i+=2) {
			if (strcasecmp(attr[i], "name") == 0)
				pdata->name = strdup(attr[i+1]);
		}
		break;
	default:
		break;
	}

	pdata->depth++;
}

static void XMLCALL
end_name(void *data, const char *el)
{
	parser_data_t *pdata = (parser_data_t*)data;

	pdata->depth--;
}

static char*
theme_name(char *file)
{
	FILE *f;
	int len, done = 0;
	char buf[1024];
	parser_data_t pdata;
	char *base;
	extern char *basename(char*);

	if ((f=fopen(file, "r")) == NULL) {
		perror(file);
		exit(1);
	}

	pdata.p = XML_ParserCreate(NULL);
	if (pdata.p) {
		pdata.data = NULL;
		pdata.cur = NULL;
		pdata.theme_err = NULL;
		pdata.depth = 0;
		pdata.cur_attr = -1;
		pdata.cur_type = -1;
		if ((base=basename(file)) == NULL) {
			fprintf(stderr, "bad file '%s'\n", file);
			exit(1);
		}
		pdata.name = strdup(base);
		XML_SetElementHandler(pdata.p, start_name, end_name);
		XML_SetCharacterDataHandler(pdata.p, value_name);
		XML_SetUserData(pdata.p, (void*)&pdata);
	}

	while (1) {
		len = fread(buf, 1, sizeof(buf), f);
		done = feof(f);
		XML_Parse(pdata.p, buf, len, done);
		if (done)
			break;
	}

	fclose(f);

	return pdata.name;
}
