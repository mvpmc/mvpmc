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

#include <mvp_widget.h>
#include <mvp_demux.h>
#include <mvp_av.h>

#include "expat.h"
#include "mvpmc.h"

#include "../tools/colortest.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

static XML_Parser p;

static int depth = 0;
static char *cur = NULL;
static int cur_attr = -1;

static int tag_font(const char *el, const char **attr, char *value);
static int tag_mvpmctheme(const char *el, const char **attr);
static int tag_settings(const char *el, const char **attr);
static int tag_widget(const char *el, const char **attr);

typedef struct {
	char *tag;
	int (*func)(const char*, const char**);
	int (*vfunc)(const char*, const char**, char *value);
} theme_tag_t;

static theme_tag_t tags[] = {
	{ .tag = "font",	.vfunc = tag_font },
	{ .tag = "mvpmctheme",	.func = tag_mvpmctheme },
	{ .tag = "settings",	.func = tag_settings },
	{ .tag = "widget",	.func = tag_widget },
	{ .tag = NULL }
};

static int tag_widget_font(const char *el, const char **attr, char *value);
static int tag_widget_color(const char *el, const char **attr, char *value);
static int tag_widget_style(const char *el, const char **attr, char *value);

static theme_tag_t tags_widget[] = {
	{ .tag = "font",	.vfunc = tag_widget_font },
	{ .tag = "color",	.vfunc = tag_widget_color },
	{ .tag = "style",	.vfunc = tag_widget_style },
	{ .tag = NULL }
};

typedef struct {
	char *tag;
	char *attr[9];
	int (*func)(const char*, const char**, char *value);
} theme_data_t;

typedef struct {
	char *name;
	int id;
} theme_font_t;

#define MAX_FONTS	8
static theme_font_t fonts[MAX_FONTS+1];

static char *theme_err = NULL;

static void
theme_fail(void)
{
	if (theme_err)
		fprintf(stderr, "Error '%s' at line %d in theme file\n",
			theme_err, XML_GetCurrentLineNumber(p));
	else
		fprintf(stderr, "Error at line %d in theme file\n",
			XML_GetCurrentLineNumber(p));
	exit(1);
}

static int
find_color(char *str, unsigned int *color)
{
	int i = 0;

	PRINTF("%s() line %d\n", __FUNCTION__, __LINE__);
	while (i < sizeof(color_list)/sizeof(color_list[0])) {
		if (strcasecmp(color_list[i].name, str) == 0) {
			*color = color_list[i].val;
			return 0;
		}
		i++;
	}

	theme_err = "unknown color";

	return -1;
}

static int
find_font(char *str, int *id)
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

	theme_err = "unknown font";

	return -1;
}

static int
tag_widget_font(const char *el, const char **attr, char *value)
{
	char *tok;
	int *font;

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
	default:
		theme_err = "invalid widget type";
		return -1;
	}

	if (find_font(value, font) < 0)
		return -1;

	return 0;
}

static int
tag_widget_color(const char *el, const char **attr, char *value)
{
	char *tok;
	unsigned int *color;

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
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "hilite_bg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			theme_err = "invalid attribute";
			theme_fail();
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
			theme_err = "invalid attribute";
			theme_fail();
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
			theme_err = "invalid attribute";
			theme_fail();
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->title_bg;
			break;
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "title_fg") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_TEXT:
			theme_err = "invalid attribute";
			theme_fail();
			break;
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->title_fg;
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
		default:
			return -1;
		}
	} else if (strcasecmp(attr[1], "checkbox") == 0) {
		switch (theme_attr[cur_attr].type) {
		case WIDGET_MENU:
			color = &theme_attr[cur_attr].attr.menu->checkbox_fg;
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
		theme_err = "unknown attribute";
		return -1;
	}

	if (find_color(value, color) < 0)
		return -1;

	if (attr[2]) {
		int alpha;

		if (strcasecmp(attr[2], "alpha") != 0) {
			theme_err = "unknown attribute";
			return -1;
		}

		alpha = (atoi(attr[3]) / 100.0) * 255;

		if ((alpha < 0) || (alpha > 255)) {
			theme_err = "invalid alpha value";
			return -1;
		}

		*color = mvpw_color_alpha(*color, alpha);
	}

	return 0;
}

static int
tag_widget_style(const char *el, const char **attr, char *value)
{
	char *tok;
	unsigned int *color;

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
		default:
			theme_err = "unknown attribute";
			return -1;
			break;
		}
	} else {
		theme_err = "unknown attribute";
		return -1;
	}

	return 0;
}

static int
tag_font(const char *el, const char **attr, char *value)
{
	char *name = NULL, *file = NULL;
	int i, fontid;

	file = value;

	for (i=0; attr[i]; i+=2) {
		if (strcasecmp(attr[i], "name") == 0) {
			name = attr[i+1];
		} else {
			return -1;
		}
	}

	if ((name == NULL) || (file == NULL))
		return -1;

#ifdef MVPMC_HOST
	fontid = 0;
#else
	if ((fontid=mvpw_load_font(file)) <= 0)
		theme_fail();
#endif /* MVPMC_HOST */

	i = 0;
	while ((i < MAX_FONTS) && (fonts[i].name != NULL)) {
		i++;
	}

	if (i < MAX_FONTS) {
		PRINTF("ADD FONT: '%s'\n", name);
		fonts[i].name = strdup(name);
		fonts[i].id = fontid;
	} else {
		return -1;
	}

	if (i == MAX_FONTS)
		return -1;

	return 0;
}

static int
tag_mvpmctheme(const char *el, const char **attr)
{
	int i;

	for (i=0; attr[i]; i+=2) {
		if (strcasecmp(attr[i], "version") == 0) {
		    if (strcasecmp(attr[i+1], "0") == 0)
			    return 0;
		    return -1;
		}
	}

	return -1;
}

static int
tag_settings(const char *el, const char **attr)
{
	return 0;
}

static int
tag_widget(const char *el, const char **attr)
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
		theme_err = "unknown widget";
		goto err;
	}

	cur_attr = i;

	if (copy) {
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

		switch (theme_attr[i].type) {
		case WIDGET_TEXT:
			memcpy(theme_attr[cur_attr].attr.text,
			       theme_attr[i].attr.text,
			       sizeof(*(theme_attr[i].attr.text)));
			break;
		case WIDGET_MENU:
			memcpy(theme_attr[cur_attr].attr.menu,
			       theme_attr[i].attr.menu,
			       sizeof(*(theme_attr[i].attr.menu)));
			break;
		case WIDGET_GRAPH:
			memcpy(theme_attr[cur_attr].attr.graph,
			       theme_attr[i].attr.graph,
			       sizeof(*(theme_attr[i].attr.graph)));
			break;
		}
	}

	return 0;

 err:
	return -1;
}

static int
call_func(const char *el, const char **attr, theme_tag_t *tag)
{
	theme_data_t *udata;
	int i;

	if (tag->func) {
		if (tag->func(el, attr) != 0)
			goto err;
	} else {
		if ((udata=malloc(sizeof(*udata))) ==
		    NULL)
			goto err;
		memset(udata, 0, sizeof(*udata));
		udata->tag = strdup(el);
		for (i=0; attr[i]; i++)
			udata->attr[i] = strdup(attr[i]);
		udata->func = tag->vfunc;
		XML_SetUserData(p, (void*)udata);
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
	theme_data_t *udata;

	buf = alloca(len+1);
	strncpy(buf, el, len);
	buf[len] = '\0';

	if ((val=strtok(buf, " \t\r\n")) != NULL) {
		if (data) {
			PRINTF("DATA ");
			udata = (theme_data_t*)data;
			if (udata->func(udata->tag, udata->attr, val) < 0)
				theme_fail();
		}

		PRINTF("value='%s'\n", val);
	}

	XML_SetUserData(p, NULL);
}

static void XMLCALL
start(void *data, const char *el, const char **attr)
{
	int i;
	theme_data_t *udata;

	PRINTF("<%s", el);
	for (i=0; attr[i]; i+=2)
		PRINTF(" %s='%s'", attr[i], attr[i+1]);
	PRINTF(">\n");

	if (i >= 8)
		goto err;

	switch (depth) {
	case 0:
		if (strcasecmp(el, "mvpmctheme") != 0)
			goto err;
	case 1:
		i = 0;
		while (tags[i].tag) {
			if (strcasecmp(tags[i].tag, el) == 0) {
				if (call_func(el, attr, tags+i) != 0)
					goto err;
				break;
			}
			i++;
		}

		if (tags[i].tag == NULL)
			goto err;
		break;
	case 2:
		PRINTF("cur_attr %d, el '%s'\n", cur_attr, el);
		i = 0;
		while (tags_widget[i].tag) {
			if (strcasecmp(tags_widget[i].tag, el) == 0) {
				if (call_func(el, attr, tags_widget+i) != 0)
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

	if (++depth == 2)
		cur = strdup(el);

	return;

 err:
	theme_fail();
}

static void XMLCALL
end(void *data, const char *el)
{
	PRINTF("</%s>\n", el);

	if (--depth == 1) {
		if (cur)
			free(cur);
		cur = NULL;
		cur_attr = -1;
	}
}

int
theme_parse(char *file)
{
	FILE *f;
	int len, done = 0;
	char buf[4096];

	if ((f=fopen(file, "r")) == NULL) {
		perror(file);
		exit(1);
	}

	p = XML_ParserCreate(NULL);
	if (p) {
		XML_SetElementHandler(p, start, end);
		XML_SetCharacterDataHandler(p, value);
	}

	while (1) {
		len = fread(buf, 1, sizeof(buf), f);
		done = feof(f);
		XML_Parse(p, buf, len, done);
		if (done)
			break;
	}

	fclose(f);

	return 0;
}
