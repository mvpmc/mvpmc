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

	if (!widget->data.checkbox.checked)
		return;

	gc = GrNewGC();
	GrSetGCForeground(gc, widget->data.checkbox.fg);
	GrSetGCBackground(gc, 0);

	GrFillRect(widget->wid, gc, 4, 4, widget->width-8, widget->height-8);

	GrDestroyGC(gc);
}

mvp_widget_t*
mvpw_create_checkbox(mvp_widget_t *parent,
		     int x, int y, int w, int h,
		     uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	widget->type = MVPW_CHECKBOX;
	widget->expose = expose;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

void
mvpw_set_checkbox_fg(mvp_widget_t *widget, uint32_t fg)
{
	widget->data.checkbox.fg = fg;
}

void
mvpw_set_checkbox(mvp_widget_t *widget, int checked)
{
	widget->data.checkbox.checked = checked;
}
