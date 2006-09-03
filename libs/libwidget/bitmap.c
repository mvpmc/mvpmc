/*
 *  Copyright (C) 2004-2006, Jon Gettler
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
draw_points(mvp_widget_t *widget, GR_GC_ID *gc, int color, char *image)
{
	int count;
	int x, y, n = 0;
	GR_POINT *points;

	for (y=0; y<widget->height; y++)
		for (x=0; x<widget->width; x++)
			if (image[(y*widget->width)+x] == color)
				n++;

	if (n == 0)
		return;

	if ((points=alloca(n*sizeof(*points))) == NULL)
		return;

	count = 0;
	for (y=0; y<widget->height; y++) {
		for (x=0; x<widget->width; x++) {
			if (image[(y*widget->width)+x] == color) {
				points[count].x = x;
				points[count].y = y;
				count++;
			}
		}
	}

	GrPoints(widget->wid, gc[0], count, points);
}

static void
expose(mvp_widget_t *widget)
{
	int x, y, i;
	GR_GC_ID gc[4];
	char *image;
	int count[4];
	int alpha;

	memset(count, 0, sizeof(count));

	image = widget->data.bitmap.image;

	for (y=0; y<widget->height; y++) {
		for (x=0; x<widget->width; x++) {
			count[(int)image[(y*widget->width)+x]]++;
		}
	}

	for (i=0; i<4; i++) {
		gc[i] = GrNewGC();
		GrSetGCBackground(gc[i], 0);
	}

	if (count[1] > count[0]) {
		GrSetGCForeground(gc[0], 0xffffffff);
		GrSetGCForeground(gc[1], 0);
		alpha = 1;
	} else {
		GrSetGCForeground(gc[0], 0);
		GrSetGCForeground(gc[1], 0xffffffff);
		alpha = 0;
	}
	GrSetGCForeground(gc[2], 0xff808080);
	GrSetGCForeground(gc[3], 0xff008080);

	for (i=0; i<4; i++)
		if (i != alpha)
			draw_points(widget, gc+i, i, image);

	for (i=0; i<4; i++)
		GrDestroyGC(gc[i]);
}

static void
destroy(mvp_widget_t *widget)
{
	if (widget->data.bitmap.image)
		free(widget->data.bitmap.image);
}

/**
 * \brief	Create a bitmap within the specified parent widget.
 * 
 * \param[in]	parent 	A pointer to the parent widget 
 * \param[in]	x	The horizontal offset (in pixels) from the upper 
 * 			left corner of the parent widget
 * \param[in]	y 	The vertical offset (in pixels) from the upper 
 * 			left corner of the parent widget
 * \param[in]	w	The width of the bitmap (in pixels)
 * \param[in]	h	The height of the bitmap (in pixels)
 * \param[in]	bg	The background color of the bitmap
 * \param[in]	border_color	The color of the border
 * \param[in]	border_size	The size of the border (in pixels)
 * 
 * \return	a pointer to the created bitmap widget
 *
 * Create a bitmap widget within a parent widget. The bitmap is created
 * with its upper left corner located at an offset specified by the (x,y)
 * arguments from the upper left corner of the parent widget.  The bitmap
 * size is specified by the (w,h) arguments. 
 *
 * If the \a parent argument is \a NULL the bitmap will be a created in the
 * \a ROOT window.
 *
 * If the bitmap cannot be created, a \a NULL pointer will be returned.
 */
mvp_widget_t*
mvpw_create_bitmap(mvp_widget_t *parent,
		   int x, int y, int w, int h,
		   uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	GrSelectEvents(widget->wid, widget->event_mask);

	widget->type = MVPW_BITMAP;
	widget->expose = expose;
	widget->destroy = destroy;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

/**
 * \brief	Sets the image for a bitmap
 *
 * \param[in]	widget	A pointer to the bitmap widget created with 
 * 		\a mvpw_create_bitmap
 * \param[in]	bitmap	A pointer to the bitmap image
 * \returns	0 for success, -1 for failure
 *
 * Copys the data pointed to by the \a bitmap argument into the bitmap
 * widget pointed to by the \a widget argument.
 */
int
mvpw_set_bitmap(mvp_widget_t *widget, mvpw_bitmap_attr_t *bitmap)
{
	if ((widget->data.bitmap.image=
	     malloc(widget->width*widget->height)) == NULL)
		return -1;

	memcpy(widget->data.bitmap.image, bitmap->image,
	       widget->width*widget->height);

	return 0;
}
