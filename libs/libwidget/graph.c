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
do_expose(mvp_widget_t *widget, int val1, int val2)
{
	GR_GC_ID gc;
	int w;
	int x1, x2;

	w = widget->width * (float)(widget->data.graph.current / 
				    (float)(widget->data.graph.max -
					    widget->data.graph.min));
	x1 = widget->width * (float)(val1 / (float)(widget->data.graph.max -
						    widget->data.graph.min));
	x2 = widget->width * (float)(val2 / (float)(widget->data.graph.max -
						    widget->data.graph.min));

	gc = GrNewGC();
	GrSetGCBackground(gc, widget->bg);

	if (widget->data.graph.gradient) {
		int i;
		unsigned char r, g, b, a, R, G, B, A;
		GR_COLOR c;
		float dr, dg, db, da;
		float cr, cg, cb, ca;

		mvpw_get_rgba(widget->data.graph.left, &r, &g, &b, &a);
		mvpw_get_rgba(widget->data.graph.right, &R, &G, &B, &A);

		dr = (float)(R - r) / (float)widget->width;
		dg = (float)(G - g) / (float)widget->width;
		db = (float)(B - b) / (float)widget->width;
		da = (float)(A - a) / (float)widget->width;

		cr = r + (dr * x1);
		cg = g + (dg * x1);
		cb = b + (db * x1);
		ca = a + (da * x1);

		for (i=x1; i<x2; i++) {
			c = mvpw_rgba((unsigned char)cr, (unsigned char)cg,
				      (unsigned char)cb, (unsigned char)ca);
			if (i > w)
				GrSetGCForeground(gc, widget->bg);
			else
				GrSetGCForeground(gc, c);
			/*
			 * XXX: what's wrong here?
			 */
			if (i < widget->width-1)
				GrFillRect(widget->wid, gc,
					   i, 0, 1, widget->height);

			cr += dr;
			cg += dg;
			cb += db;
			ca += da;
		}
	} else {
		GrSetGCForeground(gc, widget->data.graph.fg);
		GrFillRect(widget->wid, gc, 0, 0, w, widget->height);
	}

	GrDestroyGC(gc);
}

static void
expose(mvp_widget_t *widget)
{
	do_expose(widget, 0, widget->width);
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
	widget->data.graph.gradient = attr->gradient;
	widget->data.graph.left = attr->left;
	widget->data.graph.right = attr->right;
}

void
mvpw_set_graph_current(mvp_widget_t *widget, int value)
{
	int cur = widget->data.graph.current;

	widget->data.graph.current = value;

	if (widget->data.graph.gradient) {
		if (cur > value)
			do_expose(widget, value, cur);
		else
			do_expose(widget, cur, value);
	} else {
		expose(widget);
	}
}

void
mvpw_graph_incr(mvp_widget_t *widget, int value)
{
	mvpw_set_graph_current(widget, widget->data.graph.current+value);
}
