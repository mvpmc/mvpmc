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
	int w;

	w = widget->width * (float)(widget->data.graph.current / 
				    (float)(widget->data.graph.max -
					    widget->data.graph.min));

	gc = GrNewGC();
	GrSetGCForeground(gc, widget->data.graph.fg);
	GrSetGCBackground(gc, widget->bg);

	GrFillRect(widget->wid, gc, 0, 0, w, widget->height);

	GrDestroyGC(gc);
}

mvp_widget_t*
mvpw_create_graph(mvp_widget_t *parent,
		  int x, int y, int w, int h,
		  uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	widget->type = MVPW_GRAPH;
	widget->expose = expose;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

void
mvpw_set_graph_attr(mvp_widget_t *widget, mvpw_graph_attr_t *attr)
{
	widget->data.graph.min = attr->min;
	widget->data.graph.max = attr->max;
	widget->data.graph.fg = attr->fg;
}

void
mvpw_set_graph_current(mvp_widget_t *widget, int value)
{
	widget->data.graph.current = value;
}
