/*
 *  Copyright (C) 2005-2006, Jon Gettler
 *  http://www.mvpmc.org/
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mvp_widget.h"
#include "widget.h"

static void
expose(mvp_widget_t *widget)
{
	int i = 0;
	mvpw_expose(widget->data.dialog.title_widget);
	mvpw_expose(widget->data.dialog.text_widget);
	mvpw_expose(widget->data.dialog.image_widget);
	for(i=0;i<widget->data.dialog.button_ct;i++) {
		if(widget->data.dialog.buttons[i])
			mvpw_expose(widget->data.dialog.buttons[i]);
	}
}

static void
destroy(mvp_widget_t *widget)
{
	int i;

	mvpw_destroy(widget->data.dialog.title_widget);
	mvpw_destroy(widget->data.dialog.text_widget);
	mvpw_destroy(widget->data.dialog.image_widget);
	for(i=0; i<widget->data.dialog.button_ct; i++) {
		if(widget->data.dialog.buttons[i])
			mvpw_destroy(widget->data.dialog.buttons[i]);
		if(widget->data.dialog.button_strs[i])
			free(widget->data.dialog.button_strs[i]);
	}
}

mvp_widget_t*
mvpw_create_dialog(mvp_widget_t *parent,
		   int x, int y, int w, int h,
		   uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	GrSelectEvents(widget->wid, widget->event_mask);

	widget->type = MVPW_DIALOG;
	widget->expose = expose;
	widget->destroy = destroy;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

int
mvpw_set_dialog_attr(mvp_widget_t *widget, mvpw_dialog_attr_t *attr)
{
	mvpw_image_info_t iid;
	mvp_widget_t *title = NULL, *text = NULL, *image = NULL;
	GR_FONT_INFO finfo;
	int h, w, i;
	mvpw_text_attr_t text_attr = {
		.wrap = true,
		.pack = false,
		.margin = 4,
	};
	mvpw_text_attr_t button_attr = {
		.wrap = true,
		.pack = false,
		.margin = 2,
		.rounded = true,
		.justify = MVPW_TEXT_CENTER,
	};

	if ((widget == NULL) || (attr == NULL))
		return -1;

	text_attr.font = attr->font;
	text_attr.fg = attr->title_fg;
	text_attr.bg = attr->title_bg;
	text_attr.justify = attr->justify_title;
	text_attr.margin = widget->data.dialog.margin;

	GrGetFontInfo(attr->font, &finfo);
	h = finfo.height + (2 * text_attr.margin);
	w = widget->width;
	title = mvpw_create_text(widget, 0, 0, w, h, attr->title_bg,
				 widget->border_color, widget->border_size);
	if (title == NULL)
		goto err;
	mvpw_set_text_attr(title, &text_attr);

	if (attr->image) {
		if (mvpw_get_image_info(attr->image, &iid) < 0)
			goto err;

		h = iid.height;
		w = iid.width;
		image = mvpw_create_image(widget, 0, 0, w, h,
					  widget->bg, 0, 0);
		if (image == NULL)
			goto err;
		mvpw_set_image(image, attr->image);
	} else {
		w = 0;
	}

	if(widget->data.dialog.button_ct == 0)
		h = widget->height - title->height;
	else { /* Leave room for one row of buttons */
		h = finfo.height + finfo.height/2 + (2 * button_attr.margin);
		h = widget->height - title->height - h;
	}
	w = widget->width - w;
	text_attr.fg = attr->fg;
	text_attr.justify = attr->justify_body;
	text = mvpw_create_text(widget, 0, 0, w, h, widget->bg,
				widget->border_color, 0);
	if (text == NULL)
		goto err;
	mvpw_set_text_attr(text, &text_attr);

	/* Create the buttons */
	button_attr.fg = attr->button_fg;
	button_attr.bg = attr->button_bg;
	button_attr.font = attr->font;
	if(widget->data.dialog.button_ct) {
		w = (widget->width/widget->data.dialog.button_ct) - 4*button_attr.border;
		h = finfo.height + (2 * button_attr.margin);
		for(i=0;i<widget->data.dialog.button_ct;i++) {
			widget->data.dialog.buttons[i] =
			mvpw_create_text(widget, 0,0, w, h, widget->bg,
										 	widget->border_color, 0);
			mvpw_set_text_str(widget->data.dialog.buttons[i],
												widget->data.dialog.button_strs[i]);
			if(i == 0) {
				button_attr.bg = attr->button_h_bg;
				button_attr.fg = attr->button_h_fg;
			}
			else {
				button_attr.fg = attr->button_fg;
				button_attr.bg = attr->button_bg;
			}
			mvpw_set_text_attr(widget->data.dialog.buttons[i], &button_attr);
		}
	}

	widget->data.dialog.fg = attr->fg;
	widget->data.dialog.title_fg = attr->title_fg;
	widget->data.dialog.title_bg = attr->title_bg;
	widget->data.dialog.modal = attr->modal;
	widget->data.dialog.font = attr->font;
	widget->data.dialog.margin = attr->margin;
	widget->data.dialog.button_fg = attr->button_fg;
	widget->data.dialog.button_bg = attr->button_bg;
	widget->data.dialog.button_h_fg = attr->button_h_fg;
	widget->data.dialog.button_h_bg = attr->button_h_bg;


	widget->data.dialog.title_widget = title;
	widget->data.dialog.text_widget = text;
	widget->data.dialog.image_widget = image;

	if (image) {
		mvpw_attach(title, image, MVPW_DIR_DOWN);
		mvpw_attach(image, text, MVPW_DIR_RIGHT);
	} else {
		mvpw_attach(title, text, MVPW_DIR_DOWN);
	}

	for(i=0; i<widget->data.dialog.button_ct; i++) {
		if(widget->data.dialog.buttons[i]) {
			if(i > 0) {
				mvpw_attach(widget->data.dialog.buttons[i-1],
										widget->data.dialog.buttons[i],
										MVPW_DIR_RIGHT);
			}
			else {
				mvpw_attach(text, widget->data.dialog.buttons[i], MVPW_DIR_DOWN);
			}
		}
	}

	mvpw_show(title);
	mvpw_show(text);
	for(i=0; i<widget->data.dialog.button_ct; i++)
		if(widget->data.dialog.buttons[i])
			mvpw_show(widget->data.dialog.buttons[i]);

	if (image)
		mvpw_show(image);

	return 0;

 err:
	if (title)
		mvpw_destroy(title);
	if (text)
		mvpw_destroy(text);
	if (image)
		mvpw_destroy(image);

	return -1;
}

void
mvpw_dialog_next_button(mvp_widget_t *widget)
{
	int cur;
	mvp_widget_t ** buttons;

	mvpw_text_attr_t button_attr = {
		.wrap = true,
		.pack = false,
		.margin = 2,
		.rounded = true,
		.justify = MVPW_TEXT_CENTER,
	};

	if(widget) {
		cur = widget->data.dialog.cur_button;
		buttons = widget->data.dialog.buttons;
		button_attr.fg = widget->data.dialog.button_fg;
		button_attr.bg = widget->data.dialog.button_bg;
		button_attr.font = widget->data.dialog.font;
		mvpw_set_text_attr(buttons[cur], &button_attr);
		mvpw_expose(buttons[cur]);
		if(widget->data.dialog.cur_button == widget->data.dialog.button_ct-1)
			widget->data.dialog.cur_button = 0;
		else
			widget->data.dialog.cur_button++;
		cur = widget->data.dialog.cur_button;
		button_attr.fg = widget->data.dialog.button_h_fg;
		button_attr.bg = widget->data.dialog.button_h_bg;
		mvpw_set_text_attr(buttons[cur], &button_attr);
		mvpw_expose(buttons[cur]);
	}
}

void
mvpw_dialog_prev_button(mvp_widget_t *widget)
{
	int cur;
	mvp_widget_t ** buttons;
	mvpw_text_attr_t button_attr = {
		.wrap = true,
		.pack = false,
		.margin = 2,
		.rounded = true,
		.justify = MVPW_TEXT_CENTER,
	};

	if(widget) {
		cur = widget->data.dialog.cur_button;
		buttons = widget->data.dialog.buttons;
		button_attr.fg = widget->data.dialog.button_fg;
		button_attr.bg = widget->data.dialog.button_bg;
		button_attr.font = widget->data.dialog.font;
		mvpw_set_text_attr(buttons[cur], &button_attr);
		mvpw_expose(buttons[cur]);
		if(widget->data.dialog.cur_button == 0)
			widget->data.dialog.cur_button = widget->data.dialog.button_ct-1;
		else
			widget->data.dialog.cur_button--;
		cur = widget->data.dialog.cur_button;
		button_attr.fg = widget->data.dialog.button_h_fg;
		button_attr.bg = widget->data.dialog.button_h_bg;
		mvpw_set_text_attr(buttons[cur], &button_attr);
		mvpw_expose(buttons[cur]);
	}
}

char *
mvpw_dialog_cur_button_s(mvp_widget_t *widget)
{
	char * rtrn = NULL;
	if(widget) {
		rtrn = widget->data.dialog.button_strs[widget->data.dialog.cur_button];
	}

	return rtrn;
}

int
mvpw_dialog_cur_button_i(mvp_widget_t *widget)
{
	int rtrn = -1;
	if(widget) {
		rtrn = widget->data.dialog.cur_button;
	}

	return rtrn;
}

void
mvpw_dialog_set_cur_button(mvp_widget_t *widget, int button)
{
	int cur;
	mvp_widget_t ** buttons;
	mvpw_text_attr_t button_attr = {
		.wrap = true,
		.pack = false,
		.margin = 2,
		.rounded = true,
		.justify = MVPW_TEXT_CENTER,
	};

	if(widget) {
		cur = widget->data.dialog.cur_button;
		if(button < widget->data.dialog.button_ct && button != cur) {
			buttons = widget->data.dialog.buttons;
			button_attr.fg = widget->data.dialog.button_fg;
			button_attr.bg = widget->data.dialog.button_bg;
			button_attr.font = widget->data.dialog.font;
			mvpw_set_text_attr(buttons[cur], &button_attr);
			mvpw_expose(buttons[cur]);
			button_attr.fg = widget->data.dialog.button_h_fg;
			button_attr.bg = widget->data.dialog.button_h_bg;
			mvpw_set_text_attr(buttons[button], &button_attr);
			mvpw_expose(buttons[button]);
		}
	}

}

int
mvpw_set_dialog_title(mvp_widget_t *widget, char *title)
{
	if ((widget == NULL) || (title == NULL))
		return -1;

	if (widget->data.dialog.title_widget)
		mvpw_set_text_str(widget->data.dialog.title_widget, title);

	return 0;
}

mvp_widget_t *
mvpw_get_dialog_title(mvp_widget_t *widget)
{
	mvp_widget_t *rtrn = NULL;
	
	if(widget)
		rtrn = widget->data.dialog.title_widget;

	return rtrn;
}

char *
mvpw_get_dialog_text(mvp_widget_t *widget)
{
	if (widget == NULL)
		return NULL;

	return 	mvpw_get_text_str(widget->data.dialog.text_widget);
}

int
mvpw_set_dialog_text(mvp_widget_t *widget, char *text)
{
	if ((widget == NULL) || (text == NULL))
		return -1;

	if (widget->data.dialog.text_widget)
		mvpw_set_text_str(widget->data.dialog.text_widget, text);

	return 0;
}

/* Adds one of 3 possible buttons to the bottom of the widget */
int
mvpw_add_dialog_button(mvp_widget_t *widget, char *text)
{
	int rtrn = -1, ct;

	if(widget) {
		ct = widget->data.dialog.button_ct;
		if(ct < MVPW_MAX_DLG_BUTTON_CT) {
			widget->data.dialog.button_strs[ct] = strdup(text);
			widget->data.dialog.button_ct++;
			rtrn = 0;
		}
	}
	return rtrn;
}
