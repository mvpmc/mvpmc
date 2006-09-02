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
destroy(mvp_widget_t *widget)
{
	if (widget->data.text.str)
		free (widget->data.text.str);
}

static void
expose(mvp_widget_t *widget)
{
	GR_GC_ID gc, gcr;
	GR_FONT_INFO finfo;
	int x, y, h, w, descent, indent = 0, width, dia,tx;
	char *str;
	int i, j, k, nl = 0;
	int encoding;

	int consume_spaces;

	if (widget->data.text.str == NULL)
		return;

	if (!mvpw_visible(widget))
		return;

	if (widget->data.text.utf8) {
		encoding = MWTF_UTF8;
	} else {
		encoding = MWTF_ASCII;
	}

	GrGetFontInfo(widget->data.text.font, &finfo);
	h       = finfo.height;
	/*
		 This change packs the font in closer but will require that it be
		 drawn from the bottom up or the descents get covered by the next line
		 being drawn. Need to figure this out later. Probably using a parameter
		 in the attribute. TODO: Fix this so that packing only occurs when
		 requested.
	h       = finfo.baseline;
	*/
	descent = h - finfo.baseline;

	gc = GrNewGC();
	gcr = GrNewGC();

	GrSetGCForeground(gc, widget->bg);
	GrFillRect(widget->wid, gc, 0, 0, widget->width, widget->height);

	if (widget->data.text.rounded) {
		GrSetGCBackground(gc, widget->data.text.text_bg);
	} else {
		GrSetGCBackground(gc, widget->bg);
	}
	GrSetGCForeground(gc, widget->data.text.fg);
	GrSetGCFont(gc, widget->data.text.font);

	GrSetGCForeground(gcr, widget->data.text.text_bg);

	if (widget->data.text.rounded) {
		dia = widget->height / 2;
		indent = dia / 2;
		GrArc(widget->wid, gcr,
		      dia, dia,
		      dia, dia,
		      0, 0,
		      0, dia,
		      GR_PIE);
		width = mvpw_font_width(widget->data.text.font,
					widget->data.text.str,
					widget->data.text.utf8);
		if (width > widget->width - (2*dia))
			width = widget->width - (2*dia);
		GrArc(widget->wid, gcr,
		      dia+width, dia,
		      dia, dia,
		      0+width, dia,
		      0+width, 0,
		      GR_PIE);
		GrFillRect(widget->wid, gcr, dia, 0, width, widget->height);
	}

	str = widget->data.text.str;

	w = mvpw_font_width(widget->data.text.font, widget->data.text.str,
			    widget->data.text.utf8);

	switch (widget->data.text.justify) {
	case MVPW_TEXT_LEFT:
		x = widget->data.text.margin;
		break;
	case MVPW_TEXT_RIGHT:
		if (w > (widget->width - widget->data.text.margin))
			x = 0;
		else
			x = widget->width - w - widget->data.text.margin;
		break;
	case MVPW_TEXT_CENTER:
		if (w > (widget->width - widget->data.text.margin))
			x = 0;
		else
			x = (widget->width - w - widget->data.text.margin) / 2;
		break;
	default:
		x = 0;
		break;
	}
	y = h + widget->data.text.margin;

	for (i=0; i<strlen(str); i++) {
		if (str[i] == '\n') {
			nl = 1;
			x=0;
			break;
		}
	}

	if (widget->data.text.wrap && ((w > widget->width - x) || nl)) {
		char buf[256];

		/*
		 * If we find CRs then don't consume leading spaces unless
		 * we wrap.
		 */
		i = 0;
		j = 0;
		while (i < strlen(str)) {
			if (str[i++] == '\n')
				j++;
			}
		if (j > 1)
			consume_spaces = 0;
		else
			consume_spaces = 1;
			
		i = 0;
		while (i < strlen(str)) {
			j = 0;

			memset(buf, 0, sizeof(buf));

			/*
			 * Found CRs, don't consume leading spaces unless
			 * we wrap.
			 */
			if (consume_spaces == 1) {
				/*
				 * No CRs, get rid of any leading blanks.
				 */
				while (str[i] == ' ')
					i++;
			}

			/*
			 * Grow the part of the string to be printed until
			 * it fills the width of the widget or we run out
			 * of characters. This is an inefficient algorithm
			 * because it wasts lots of cycles re-copying the sting.
			  *TODO.
			 */
			w = 0;
			while (((w < widget->width - x) &&
				((j+i) < strlen(str))) &&
			       (str[i+j] != '\n')) {
				strncpy(buf, str+i, j+1);
				w = mvpw_font_width(widget->data.text.font,
						    buf,
						    widget->data.text.utf8);
				j++;
			}

			/*
			 * Remove last partial word and spaces.
			 */
			k = j;
			if (((j+i) < strlen(str)) && (str[i+j] != '\n')) {
				while ((j > 0) && (buf[j] != ' '))
					j--;

				while ((j > 0) && (buf[j] == ' '))
					j--;
				buf[++j] = '\0';

				/*
				 * We are wrapping, so consume spaces to
				 * next non-space character in next line.
				 */
				k = j + 1;
				while ((k < strlen(str)) && (str[i+k] == ' '))
					k++;
			}

			w = mvpw_font_width(widget->data.text.font, buf,
					    widget->data.text.utf8);
	
			tx = x; /* Sergio: Keeping x to fix a bug */
			switch (widget->data.text.justify) {
			case MVPW_TEXT_LEFT:
				x = widget->data.text.margin;
				break;
			case MVPW_TEXT_RIGHT:
				x = widget->width - w -
					widget->data.text.margin;
				break;
			case MVPW_TEXT_CENTER:
				x = (widget->width - w -
				     widget->data.text.margin) / 2;
				break;
			default:
				break;
			}
			if (buf[strlen(buf)-1] == '\n')
				buf[strlen(buf)-1] = '\0';
			GrText(widget->wid, gc, x+indent, y-descent, buf, strlen(buf), encoding);
			y += h;

			i += k;

			if (str[i] == '\n')
				i++;

			/* If we don't do this each line inherits the adjustment from
			 * the line before. This is a bug.
			 */
			x=tx;
		}
	} else {
		GrText(widget->wid, gc, x+indent, y-descent, str, strlen(str), encoding);
	}

	GrDestroyGC(gc);
	GrDestroyGC(gcr);
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
	widget->destroy = destroy;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

void
mvpw_set_text_str(mvp_widget_t *widget, char *str)
{
	if (widget->data.text.str && (strcmp(str, widget->data.text.str) == 0))
		return;

	if (widget->data.text.buflen < (strlen(str)+1)) {
		if (widget->data.text.str)
			free(widget->data.text.str);
		widget->data.text.str = strdup(str);
		widget->data.text.buflen = strlen(str) + 1;
	} else {
		strcpy(widget->data.text.str, str);
	}

	expose(widget);
}

char*
mvpw_get_text_str(mvp_widget_t *widget)
{
	return widget->data.text.str;
}

void
mvpw_set_text_attr(mvp_widget_t *widget, mvpw_text_attr_t *attr)
{
	widget->data.text.wrap = attr->wrap;
	widget->data.text.justify = attr->justify;
	widget->data.text.margin = attr->margin;
	widget->data.text.fg = attr->fg;
	widget->data.text.font = attr->font;
	widget->data.text.rounded = attr->rounded;
	widget->data.text.text_bg = attr->bg;
	widget->data.text.utf8 = attr->utf8;

	if (widget->border_color != attr->border) {
		GrSetWindowBorderColor(widget->wid, attr->border);
		widget->border_color = attr->border;
	}
	widget->border_size = attr->border_size;

	mvpw_expose(widget);
}

void
mvpw_get_text_attr(mvp_widget_t *widget, mvpw_text_attr_t *attr)
{
	attr->wrap = widget->data.text.wrap;
	attr->justify = widget->data.text.justify;
	attr->margin = widget->data.text.margin;
	attr->fg = widget->data.text.fg;
	attr->bg = widget->data.text.text_bg;
	attr->border = widget->border_color;
	attr->font = widget->data.text.font;
	attr->rounded = widget->data.text.rounded;
	attr->utf8 = widget->data.text.utf8;

	attr->border = widget->border_color;
	attr->border_size = widget->border_size;
}

void
mvpw_set_text_fg(mvp_widget_t *widget, uint32_t fg)
{
	widget->data.text.fg = fg;
}
