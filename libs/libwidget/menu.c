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
destroy(mvp_widget_t *widget)
{
	int i;
	char *str;
	void *key;

	for (i=0; i<widget->data.menu.nitems; i++) {
		if (widget->data.menu.items[i].destroy) {
			str = widget->data.menu.items[i].label;
			key = widget->data.menu.items[i].key;
			widget->data.menu.items[i].destroy(widget, str, key);
		}
	}

	if (widget->data.menu.container_widget)
		mvpw_destroy(widget->data.menu.container_widget);
	if (widget->data.menu.title_widget)
		mvpw_destroy(widget->data.menu.title_widget);

	if (widget->data.menu.items)
		free(widget->data.menu.items);
}

static void
change_items(mvp_widget_t *widget, int first)
{
	int i;
	mvp_widget_t *cw, *cbw;

	if (first < 0)
		first = 0;

	for (i=0; i<widget->data.menu.nitems; i++) {
		cbw = widget->data.menu.items[i].checkbox;
		if (cbw) {
			mvpw_set_checkbox(cbw, 0);
			mvpw_expose(cbw);
		}
		widget->data.menu.items[i].widget = NULL;
		widget->data.menu.items[i].checkbox = NULL;
	}

	cw = widget->data.menu.container_widget;
	for (i=0; i<widget->data.menu.rows; i++) {
		if (i >= widget->data.menu.nitems)
			break;
		widget->data.menu.items[first].widget =
			cw->data.container.widgets[i];
		widget->data.menu.items[first].checkbox =
			cw->data.container.widgets[i]->attach[MVPW_DIR_LEFT];
		mvpw_set_bg(widget->data.menu.items[first].widget,
			    widget->data.menu.items[first].bg);
		mvpw_set_text_fg(widget->data.menu.items[first].widget,
				 widget->data.menu.items[first].fg);
		if (first >= widget->data.menu.nitems)
			mvpw_set_text_str(cw->data.container.widgets[i], "");
		else
			mvpw_set_text_str(cw->data.container.widgets[i],
					  widget->data.menu.items[first].label);
		if (widget->data.menu.items[first].checkbox) {
			cbw = widget->data.menu.items[first].checkbox;
			mvpw_set_checkbox(cbw,
					  widget->data.menu.items[first].checked);
			mvpw_set_checkbox_fg(cbw,
					     widget->data.menu.items[first].checkbox_fg);
			mvpw_expose(cbw);
		}
		mvpw_expose(cw->data.container.widgets[i]);
		first++;
	}
}

static int
hilite_item(mvp_widget_t *widget, int which, int hilite)
{
	mvpw_text_attr_t attr;
	char *str;
	void *key;

	/*
	 * If the widget to be unhilighted is not visible, skip it
	 */
	if ((widget->data.menu.items[which].widget == NULL) && !hilite)
		return 0;

	/*
	 * If the widget to be hilighted is not visible, make it visible
	 */
	if ((widget->data.menu.items[which].widget == NULL) && hilite)
		change_items(widget, which);

	if (hilite) {
		if (!widget->data.menu.items[which].selectable)
			return -1;

		mvpw_get_text_attr(widget->data.menu.items[which].widget,
				   &attr);
		mvpw_expose(widget->data.menu.items[which].widget);
		if (widget->data.menu.rounded) {
			mvpw_set_bg(widget->data.menu.items[which].widget,
				    widget->data.menu.items[which].bg);
			attr.bg = widget->data.menu.hilite_bg;
			attr.rounded = 1;
		} else {
			mvpw_set_bg(widget->data.menu.items[which].widget,
				    widget->data.menu.hilite_bg);
		}
		attr.fg = widget->data.menu.hilite_fg;
		mvpw_set_text_attr(widget->data.menu.items[which].widget,
				   &attr);
	} else {
		mvpw_get_text_attr(widget->data.menu.items[which].widget,
				   &attr);
		mvpw_expose(widget->data.menu.items[which].widget);
		mvpw_set_bg(widget->data.menu.items[which].widget,
			    widget->data.menu.items[which].bg);
		attr.fg = widget->data.menu.items[which].fg;
		attr.rounded = 0;
		mvpw_set_text_attr(widget->data.menu.items[which].widget,
				   &attr);
	}

	if (widget->data.menu.items[which].hilite) {
		str = widget->data.menu.items[which].label;
		key = widget->data.menu.items[which].key;
		widget->data.menu.items[which].hilite(widget, str, key,
						      hilite);
	}

	return 0;
}

static void
key(mvp_widget_t *widget, char c)
{
	int i, j;
	void *k;
	char *str;

	switch (c) {
#ifdef MVPW_KEY_RED
	case MVPW_KEY_RED:
#endif
	case MVPW_KEY_CHAN_UP:
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
#ifdef MVPW_KEY_YELLOW
	case MVPW_KEY_YELLOW:
#endif
	case MVPW_KEY_CHAN_DOWN:
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
	case MVPW_KEY_UP:
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
	case MVPW_KEY_DOWN:
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
	case MVPW_KEY_OK:
		if (widget->data.menu.current < widget->data.menu.nitems) {
			i = widget->data.menu.current;
			str = widget->data.menu.items[i].label;
			k = widget->data.menu.items[i].key;
			if (widget->data.menu.items[i].select &&
			    widget->data.menu.items[i].selectable)
				widget->data.menu.items[i].select(widget, str, k);
		}
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
	widget->destroy = destroy;
	widget->key = key;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

int
mvpw_add_menu_item(mvp_widget_t *widget, char *label, void *key,
		   mvpw_menu_item_attr_t *item_attr)
{
	int i, border_size;
	GR_COORD w, h;
	GR_COLOR bg, border_color;
	GR_FONT_INFO finfo;
	mvp_widget_t *cw;
	mvp_widget_t *tw = NULL, *cbw = NULL;
	mvpw_text_attr_t attr = {
		.wrap = 0,
		.justify = MVPW_TEXT_LEFT,
		.margin = 4,
	};

	if (widget == NULL)
		return -1;

	attr.font = widget->data.menu.font;
	attr.fg = widget->data.menu.fg;

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
		memset(widget->data.menu.items+widget->data.menu.nitems,0, 32);

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

	if (widget->data.menu.checkboxes) {
		cbw = mvpw_create_checkbox(cw, 0, 0, h, h, bg,
					   border_color, border_size);
		tw = mvpw_create_text(cw, h, 0, w-h, h, bg,
				      border_color, border_size);
		mvpw_attach(cbw, tw, MVPW_DIR_RIGHT);
	} else {
		tw = mvpw_create_text(cw, 0, 0, w, h, bg,
				      border_color, border_size);
	}

	if (i == widget->data.menu.current) {
		attr.fg = widget->data.menu.hilite_fg;
		if (widget->data.menu.rounded) {
			mvpw_set_bg(tw, widget->bg);
			attr.bg = widget->data.menu.hilite_bg;
			attr.rounded = 1;
		} else {
			mvpw_set_bg(tw, widget->data.menu.hilite_bg);
			attr.rounded = 0;
		}
	} else {
		if (item_attr) {
			attr.fg = item_attr->fg;
			mvpw_set_bg(tw, item_attr->bg);
		}
	}

	attr.font = widget->data.menu.font;
	mvpw_set_text_attr(tw, &attr);
	mvpw_set_text_str(tw, label);

	if (i != 0)
		mvpw_attach(widget->data.menu.items[i-1].widget,
			    tw, MVPW_DIR_DOWN);

	cw->add_child(cw, tw);

	mvpw_show(tw);
	if (cbw)
		mvpw_show(cbw);

 out:
	widget->data.menu.items[i].label = strdup(label);
	widget->data.menu.items[i].key = key;
	widget->data.menu.items[i].widget = tw;
	widget->data.menu.items[i].checkbox = cbw;

	if (item_attr) {
		widget->data.menu.items[i].select = item_attr->select;
		widget->data.menu.items[i].hilite = item_attr->hilite;
		widget->data.menu.items[i].destroy = item_attr->destroy;
		widget->data.menu.items[i].selectable = item_attr->selectable;
		widget->data.menu.items[i].fg = item_attr->fg;
		widget->data.menu.items[i].bg = item_attr->bg;
		widget->data.menu.items[i].checkbox_fg =
			item_attr->checkbox_fg;
	}

	widget->data.menu.nitems++;

	if ((i == 0) && (widget->data.menu.items[i].hilite != NULL))
		widget->data.menu.items[i].hilite(widget, label, key, 1);

	return 0;
}

int
mvpw_delete_menu_item(mvp_widget_t *widget, void *key)
{
	char *str;
	void *k;
	int count = 0, i, j;
	mvp_widget_t *w, *cbw;
	struct menu_item_s *item;

	for (i=0; i<widget->data.menu.nitems; i++) {
		k = widget->data.menu.items[i].key;
		if (k == key) {
			if (widget->data.menu.items[i].destroy) {
				str = widget->data.menu.items[i].label;
				widget->data.menu.items[i].destroy(widget,
								   str, key);
			}
			if (widget->data.menu.items[i].label)
				free(widget->data.menu.items[i].label);
			for (j=widget->data.menu.nitems-1; j>i; j--) {
				item = widget->data.menu.items+j;
				item->widget =
					widget->data.menu.items[j-1].widget;
				w = item->widget;
				cbw = widget->data.menu.items[j-1].checkbox;
				str = item->label;
				if (w) {
					mvpw_set_text_str(w, str);
					mvpw_set_bg(w, item->bg);
					mvpw_set_text_fg(w, item->fg);
					mvpw_expose(w);
				}
				if (cbw) {
					mvpw_set_checkbox(cbw,
							  item->checked);
					mvpw_set_checkbox_fg(cbw,
							     item->checkbox_fg);
					mvpw_expose(cbw);
				}
			}
			widget->data.menu.nitems--;
			memmove(widget->data.menu.items+i,
				widget->data.menu.items+i+1,
				(widget->data.menu.nitems - i) *
				sizeof(struct menu_item_s));
			j = widget->data.menu.nitems;
			item = widget->data.menu.items + j + 1;
			if (item->widget) {
				mvpw_set_text_str(item->widget, "");
				mvpw_set_bg(item->widget, widget->bg);
				mvpw_expose(item->widget);
			}
			count++;
		}
	}

	if (widget->data.menu.current >= widget->data.menu.nitems)
		widget->data.menu.current = widget->data.menu.nitems - 1;

	hilite_item(widget, widget->data.menu.current, 1);

	return count;
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
		.justify = widget->data.menu.title_justify,
		.margin = 4,
		.font = widget->data.menu.font,
		.fg = widget->data.menu.title_fg,
	};

	if (widget->data.menu.title)
		free(widget->data.menu.title);
	widget->data.menu.title = strdup(title);

	GrGetFontInfo(widget->data.menu.font, &finfo);

	x = 0;
	y = 0;
	w = widget->width;
	h = finfo.height + (attr.margin * 2);

	bg = widget->data.menu.title_bg;
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
	int i;

	if (widget == NULL)
		return;

	widget->data.menu.font = attr->font;
	widget->data.menu.fg = attr->fg;
	widget->data.menu.bg = attr->bg;
	widget->data.menu.hilite_fg = attr->hilite_fg;
	widget->data.menu.hilite_bg = attr->hilite_bg;
	widget->data.menu.title_fg = attr->title_fg;
	widget->data.menu.title_bg = attr->title_bg;
	widget->data.menu.title_justify = attr->title_justify;
	widget->data.menu.checkboxes = attr->checkboxes;
	widget->data.menu.rounded = attr->rounded;

	if (widget->border_color != attr->border) {
		GrSetWindowBorderColor(widget->wid, attr->border);
		widget->border_color = attr->border;
	}
	widget->border_size = attr->border_size;

	i = widget->data.menu.current;
	if (i < widget->data.menu.nitems)
		hilite_item(widget, i, 1);
}

void
mvpw_get_menu_attr(mvp_widget_t *widget, mvpw_menu_attr_t *attr)
{
	if (widget == NULL)
		return;

	attr->font = widget->data.menu.font;
	attr->fg = widget->data.menu.fg;
	attr->bg = widget->data.menu.bg;
	attr->hilite_fg = widget->data.menu.hilite_fg;
	attr->hilite_bg = widget->data.menu.hilite_bg;
	attr->title_fg = widget->data.menu.title_fg;
	attr->title_bg = widget->data.menu.title_bg;
	attr->title_justify = widget->data.menu.title_justify;
	attr->checkboxes = widget->data.menu.checkboxes;
	attr->rounded = widget->data.menu.rounded;
	attr->border_size = widget->border_size;
	attr->border = widget->border_color;
}

void
mvpw_clear_menu(mvp_widget_t *widget)
{
	int i;
	char *str;
	void *key;

	if (widget == NULL)
		return;

	for (i=0; i<widget->data.menu.nitems; i++) {
		str = widget->data.menu.items[i].label;
		if (widget->data.menu.items[i].destroy) {
			key = widget->data.menu.items[i].key;
			widget->data.menu.items[i].destroy(widget, str, key);
		}
		if (str)
			free(str);
		widget->data.menu.items[i].label = NULL;
		if (widget->data.menu.items[i].widget) {
			mvpw_set_text_str(widget->data.menu.items[i].widget,
					  "");
			mvpw_expose(widget->data.menu.items[i].widget);
		}
		memset(widget->data.menu.items+i, 0,
		       sizeof(*(widget->data.menu.items+i)));
	}

	if (widget->data.menu.items)
		free(widget->data.menu.items);
	widget->data.menu.items = NULL;
	widget->data.menu.nitems = 0;
	widget->data.menu.current = 0;
}

void
mvpw_check_menu_item(mvp_widget_t *widget, void *key, int checked)
{
	struct menu_item_s *item;
	void *k;
	int i;

	for (i=0; i<widget->data.menu.nitems; i++) {
		item = widget->data.menu.items + i;
		k = item->key;
		if (k == key) {
			item->checked = checked;
			if (item->checkbox) {
				mvpw_set_checkbox_fg(item->checkbox,
						     item->checkbox_fg);
				mvpw_set_checkbox(item->checkbox, checked);
				mvpw_expose(item->checkbox);
			}
		}
	}
}

int
mvpw_menu_hilite_item(mvp_widget_t *widget, void *key)
{
	struct menu_item_s *item;
	void *k;
	int i;
	int ret = -1;

	if (widget == NULL)
		return -1;

	for (i=0; i<widget->data.menu.nitems; i++) {
		item = widget->data.menu.items + i;
		k = item->key;
		if (k == key) {
			widget->data.menu.current = i;
			hilite_item(widget, i, 1);
			ret = 0;
		} else {
			hilite_item(widget, i, 0);
		}
	}

	return ret;
}

int
mvpw_menu_get_item_attr(mvp_widget_t *widget, void *key,
			mvpw_menu_item_attr_t *item_attr)
{
	int i;

	if ((widget == NULL) || (item_attr == NULL))
		return -1;

	for (i=0; i<widget->data.menu.nitems; i++) {
		if (key == widget->data.menu.items[i].key) {
			item_attr->select = widget->data.menu.items[i].select;
			item_attr->hilite = widget->data.menu.items[i].hilite;
			item_attr->destroy = widget->data.menu.items[i].destroy;
			item_attr->selectable = widget->data.menu.items[i].selectable;
			item_attr->fg = widget->data.menu.items[i].fg;
			item_attr->bg = widget->data.menu.items[i].bg;
			item_attr->checkbox_fg = widget->data.menu.items[i].checkbox_fg;
			return 0;
		}
	}

	return -1;
}

int
mvpw_menu_set_item_attr(mvp_widget_t *widget, void *key,
			mvpw_menu_item_attr_t *item_attr)
{
	int i;

	if ((widget == NULL) || (item_attr == NULL))
		return -1;

	for (i=0; i<widget->data.menu.nitems; i++) {
		if (key == widget->data.menu.items[i].key) {
			widget->data.menu.items[i].select = item_attr->select;
			widget->data.menu.items[i].hilite = item_attr->hilite;
			widget->data.menu.items[i].destroy =
				item_attr->destroy;
			widget->data.menu.items[i].selectable =
				item_attr->selectable;
			widget->data.menu.items[i].fg = item_attr->fg;
			widget->data.menu.items[i].bg = item_attr->bg;
			widget->data.menu.items[i].checkbox_fg =
				item_attr->checkbox_fg;
			return 0;
		}
	}

	return -1;
}

int
mvpw_menu_change_item(mvp_widget_t *widget, void *key, char *label)
{
	int i;
	char *old;

	if ((widget == NULL) || (label == NULL))
		return -1;

	for (i=0; i<widget->data.menu.nitems; i++) {
		if (key == widget->data.menu.items[i].key) {
			old = widget->data.menu.items[i].label;
			widget->data.menu.items[i].label = strdup(label);
			free(old);
			if (widget->data.menu.items[i].widget)
				mvpw_set_text_str(widget->data.menu.items[i].widget, label);
			return 0;
		}
	}

	return -1;
}
