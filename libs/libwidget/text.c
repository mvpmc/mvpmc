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
	GR_GC_ID gc;
	GR_FONT_INFO finfo;
	int x, y, h;
	char *str;

	if (widget->data.text.str == NULL)
		return;

	GrGetFontInfo(widget->data.text.font, &finfo);
	h = finfo.height;

	gc = GrNewGC();

	GrSetGCForeground(gc, widget->data.text.fg);
	GrSetGCBackground(gc, widget->bg);
	GrSetGCFont(gc, widget->data.text.font);

	str = widget->data.text.str;

	switch (widget->data.text.justify) {
	case MVPW_TEXT_LEFT:
		x = widget->data.text.margin;
		break;
	default:
		x = 0;
		break;
	}
	y = h + widget->data.text.margin;

	if (widget->data.text.wrap) {
		int i, c;
		char *p;
		int width = 0;

		c = 0;
		p = str;
		for (i=0; i<strlen(str); i++) {
			width += finfo.widths[(int)str[i]];
			if (width > widget->width) {
				GrText(widget->wid, gc, x, y, p, c-1, 0);
				p = p+c-1;
				width = finfo.widths[(int)p[0]];
				y += h;
				c = 0;
			}
			c++;
		}
		GrText(widget->wid, gc, x, y, p, strlen(p), 0);
	} else {
		GrText(widget->wid, gc, x, y, str, strlen(str), 0);
	}

	GrDestroyGC(gc);
}

mvp_widget_t*
mvpw_create_text(mvp_widget_t *parent,
		 int x, int y, int w, int h,
		 uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	widget->type = MVPW_TEXT;
	widget->expose = expose;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

void
mvpw_set_text_str(mvp_widget_t *widget, char *str)
{
	if (widget->data.text.str)
		free(widget->data.text.str);

	widget->data.text.str = strdup(str);
}

void
mvpw_set_text_attr(mvp_widget_t *widget, mvpw_text_attr_t *attr)
{
	widget->data.text.wrap = attr->wrap;
	widget->data.text.justify = attr->justify;
	widget->data.text.margin = attr->margin;
	widget->data.text.fg = attr->fg;
	widget->data.text.font = attr->font;
}

void
mvpw_get_text_attr(mvp_widget_t *widget, mvpw_text_attr_t *attr)
{
	attr->wrap = widget->data.text.wrap;
	attr->justify = widget->data.text.justify;
	attr->margin = widget->data.text.margin;
	attr->fg = widget->data.text.fg;
	attr->font = widget->data.text.font;
}
