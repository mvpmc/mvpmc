/*
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mvp_widget.h"
#include "widget.h"

static void
expose(mvp_widget_t *widget)
{
}

static void
change_items(mvp_widget_t *widget, int first)
{
	int i;
	mvp_widget_t *cw;

	if (first < 0)
		first = 0;

	for (i=0; i<widget->data.menu.nitems; i++)
		widget->data.menu.items[i].widget = NULL;

	cw = widget->data.menu.container_widget;
	for (i=0; i<widget->data.menu.rows; i++) {
		if (i >= widget->data.menu.nitems)
			break;
		widget->data.menu.items[first].widget =
			cw->data.container.widgets[i];
		if (first >= widget->data.menu.nitems)
			mvpw_set_text_str(cw->data.container.widgets[i], "");
		else
			mvpw_set_text_str(cw->data.container.widgets[i],
					  widget->data.menu.items[first++].label);
		mvpw_expose(cw->data.container.widgets[i]);
	}
}

static void
hilite_item(mvp_widget_t *widget, int which, int hilite)
{
	mvpw_text_attr_t attr;
	char *str;
	int key;

	if (hilite) {
		mvpw_get_text_attr(widget->data.menu.items[which].widget,
				   &attr);
		mvpw_expose(widget->data.menu.items[which].widget);
		mvpw_set_bg(widget->data.menu.items[which].widget,
			    widget->data.menu.hilite_bg);
		attr.fg = widget->data.menu.hilite_fg;
		mvpw_set_text_attr(widget->data.menu.items[which].widget,
				   &attr);
	} else {
		mvpw_get_text_attr(widget->data.menu.items[which].widget,
				   &attr);
		mvpw_expose(widget->data.menu.items[which].widget);
		mvpw_set_bg(widget->data.menu.items[which].widget,
			    widget->bg);
		attr.fg = widget->data.menu.fg;
		mvpw_set_text_attr(widget->data.menu.items[which].widget,
				   &attr);
	}

	if (widget->data.menu.items[which].hilite) {
		str = widget->data.menu.items[which].label;
		key = widget->data.menu.items[which].key;
		widget->data.menu.items[which].hilite(widget, str, key,
						      hilite);
	}
}

static void
key(mvp_widget_t *widget, char c)
{
	int i, j, k;
	char *str;

	switch (c) {
	case 'r':
		if (widget->data.menu.current > 0) {
			i = widget->data.menu.current;
			hilite_item(widget, i, 0);

			widget->data.menu.current -= widget->data.menu.rows;
			if (widget->data.menu.current < 0)
				widget->data.menu.current = 0;

			i = widget->data.menu.current;
			change_items(widget, i);

			hilite_item(widget, i, 1);
		}
		break;
	case 'y':
		if (widget->data.menu.current < (widget->data.menu.nitems-1)) {
			i = widget->data.menu.current;
			hilite_item(widget, i, 0);

			widget->data.menu.current += widget->data.menu.rows;
			if (widget->data.menu.current >=
			    widget->data.menu.nitems)
				widget->data.menu.current =
					widget->data.menu.nitems - 1;

			i = j = widget->data.menu.current;
			if (j > (widget->data.menu.nitems -
				 widget->data.menu.rows))
				j = widget->data.menu.nitems -
					widget->data.menu.rows;
			change_items(widget, j);

			hilite_item(widget, i, 1);
		}
		break;
	case '^':
		if (widget->data.menu.current > 0) {
			i = widget->data.menu.current;
			hilite_item(widget, i, 0);

			widget->data.menu.current--;

			i = widget->data.menu.current;
			if (widget->data.menu.items[i].widget == NULL)
				change_items(widget, i);

			hilite_item(widget, i, 1);
		}
		break;
	case 'V':
		if (widget->data.menu.current < (widget->data.menu.nitems-1)) {
			i = widget->data.menu.current;
			hilite_item(widget, i, 0);

			widget->data.menu.current++;

			i = widget->data.menu.current;
			if (widget->data.menu.items[i].widget == NULL)
				change_items(widget,
					     i - widget->data.menu.rows + 1);

			hilite_item(widget, i, 1);
		}
		break;
	case '\n':
		i = widget->data.menu.current;
		str = widget->data.menu.items[i].label;
		k = widget->data.menu.items[i].key;
		if (widget->data.menu.items[i].callback)
			widget->data.menu.items[i].callback(widget, str, k);
		break;
	}
}

mvp_widget_t*
mvpw_create_menu(mvp_widget_t *parent,
		 int x, int y, int w, int h,
		 uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	widget->event_mask |= GR_EVENT_MASK_KEY_DOWN;

	GrSelectEvents(widget->wid, widget->event_mask);

	widget->type = MVPW_MENU;
	widget->expose = expose;
	widget->key = key;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

int
mvpw_add_menu_item(mvp_widget_t *widget, char *label, int key,
		   void (*callback)(mvp_widget_t*, char*, int),
		   void (*hilite)(mvp_widget_t*, char*, int, int))
{
	int i, border_size;
	GR_COORD w, h;
	GR_COLOR bg, border_color;
	GR_FONT_INFO finfo;
	mvp_widget_t *cw, *tw = NULL;
	mvpw_text_attr_t attr = {
		.wrap = 0,
		.justify = MVPW_TEXT_LEFT,
		.margin = 4,
		.font = widget->data.menu.font,
		.fg = widget->data.menu.fg,
	};

	if (widget->data.menu.nitems == 0) {
		if ((widget->data.menu.items=
		     malloc(sizeof(*widget->data.menu.items)*32)) == NULL)
			return -1;
		memset(widget->data.menu.items, 0,
		       sizeof(*widget->data.menu.items)*32);
		widget->data.menu.max_items = 32;

		w = widget->width;
		if (widget->data.menu.title_widget)
			h = widget->height -
				widget->data.menu.title_widget->height;
		else
			h = widget->height;
		bg = widget->bg;
		border_color = widget->border_color;
		border_size = widget->border_size;

		cw = mvpw_create_container(widget, 0, 0, w, h, bg,
					   border_color, border_size);

		if (widget->data.menu.title_widget)
			mvpw_attach(widget->data.menu.title_widget, cw,
				    MVPW_DIR_DOWN);

		mvpw_show(cw);

		widget->data.menu.container_widget = cw;
	}

	if (widget->data.menu.nitems == widget->data.menu.max_items) {
		int sz = widget->data.menu.nitems + 32;

		if ((widget->data.menu.items=
		     realloc(widget->data.menu.items,
			     sizeof(*widget->data.menu.items)*sz)) == NULL)
			return -1;

		widget->data.menu.max_items = sz;
	}

	i = widget->data.menu.nitems;

	cw = widget->data.menu.container_widget;

	GrGetFontInfo(widget->data.menu.font, &finfo);
	w = widget->width;
	h = finfo.height + (2 * attr.margin);

	if (widget->data.menu.rows == 0)
		widget->data.menu.rows = cw->height / h;

	if (i >= widget->data.menu.rows)
		goto out;

	border_size = 0;
	bg = widget->bg;
	border_color = widget->border_color;
	tw = mvpw_create_text(cw, 0, 0, w, h, bg, border_color, border_size);

	if (i == widget->data.menu.current) {
		attr.fg = widget->data.menu.hilite_fg;
		mvpw_set_bg(tw, widget->data.menu.hilite_bg);
	}

	attr.font = widget->data.menu.font;
	mvpw_set_text_attr(tw, &attr);
	mvpw_set_text_str(tw, label);

	if (i != 0)
		mvpw_attach(widget->data.menu.items[i-1].widget,
			    tw, MVPW_DIR_DOWN);

	cw->add_child(cw, tw);

	mvpw_show(tw);

 out:
	widget->data.menu.items[i].label = strdup(label);
	widget->data.menu.items[i].key = key;
	widget->data.menu.items[i].callback = callback;
	widget->data.menu.items[i].hilite = hilite;
	widget->data.menu.items[i].widget = tw;

	widget->data.menu.nitems++;

	if ((i == 0) && (hilite != NULL))
		hilite(widget, label, key, 1);

	return 0;
}

int
mvpw_set_menu_title(mvp_widget_t *widget, char *title)
{
	mvp_widget_t *tw;
	GR_COORD x, y, w, h;
	GR_COLOR bg, border_color;
	GR_FONT_INFO finfo;
	int border_size;
	mvpw_text_attr_t attr = {
		.wrap = 0,
		.justify = MVPW_TEXT_LEFT,
		.margin = 4,
		.font = widget->data.menu.font,
		.fg = 0xff000000,
	};

	widget->data.menu.title = strdup(title);

	GrGetFontInfo(widget->data.menu.font, &finfo);

	x = 0;
	y = 0;
	w = widget->width;
	h = finfo.height + (attr.margin * 2);

	bg = widget->bg;
	border_color = widget->border_color;
	border_size = widget->border_size;

	tw = mvpw_create_text(widget, x, y, w, h, bg,
			      border_color, border_size);
	mvpw_set_text_attr(tw, &attr);
	mvpw_set_text_str(tw, title);

	mvpw_show(tw);

	widget->data.menu.title_widget = tw;

	return 0;
}

void
mvpw_set_menu_attr(mvp_widget_t *widget, mvpw_menu_attr_t *attr)
{
	widget->data.menu.font = attr->font;
	widget->data.menu.fg = attr->fg;
	widget->data.menu.hilite_fg = attr->hilite_fg;
	widget->data.menu.hilite_bg = attr->hilite_bg;
}
