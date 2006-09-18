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
	mvpw_expose(widget->data.dialog.title_widget);
	mvpw_expose(widget->data.dialog.text_widget);
	mvpw_expose(widget->data.dialog.image_widget);
}

static void
destroy(mvp_widget_t *widget)
{
	mvpw_destroy(widget->data.dialog.title_widget);
	mvpw_destroy(widget->data.dialog.text_widget);
	mvpw_destroy(widget->data.dialog.image_widget);
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
	int h, w;
	mvpw_text_attr_t text_attr = {
		.wrap = true,
		.pack = false,
		.margin = 4,
	};

	if ((widget == NULL) || (attr == NULL))
		return -1;

	text_attr.font = attr->font;
	text_attr.fg = attr->title_fg;
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

	h = widget->height - title->height;
	w = widget->width - w;
	text_attr.fg = attr->fg;
	text_attr.justify = attr->justify_body;
	text = mvpw_create_text(widget, 0, 0, w, h, widget->bg,
				widget->border_color, 0);
	if (text == NULL)
		goto err;
	mvpw_set_text_attr(text, &text_attr);

	widget->data.dialog.fg = attr->fg;
	widget->data.dialog.title_fg = attr->title_fg;
	widget->data.dialog.title_bg = attr->title_bg;
	widget->data.dialog.modal = attr->modal;
	widget->data.dialog.font = attr->font;
	widget->data.dialog.margin = attr->margin;

	widget->data.dialog.title_widget = title;
	widget->data.dialog.text_widget = text;
	widget->data.dialog.image_widget = image;

	if (image) {
		mvpw_attach(title, image, MVPW_DIR_DOWN);
		mvpw_attach(image, text, MVPW_DIR_RIGHT);
	} else {
		mvpw_attach(title, text, MVPW_DIR_DOWN);
	}

	mvpw_show(title);
	mvpw_show(text);
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

int
mvpw_set_dialog_title(mvp_widget_t *widget, char *title)
{
	if ((widget == NULL) || (title == NULL))
		return -1;

	if (widget->data.dialog.title_widget)
		mvpw_set_text_str(widget->data.dialog.title_widget, title);

	return 0;
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
