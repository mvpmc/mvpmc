/*
 *  Copyright (C) 2005, Iain McFarlane
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
	if (widget->wid != widget->data.surface.wid) {
		GR_GC_ID gc;
		gc=GrNewGC();
		GrCopyArea( widget->wid, gc, 0, 0, widget->width, widget->height, widget->data.surface.wid, 0, 0, MWROP_SRCCOPY); 
		GrDestroyGC(gc);
	}
}

static void
destroy(mvp_widget_t *widget)
{
}

mvp_widget_t*
mvpw_create_surface(mvp_widget_t *parent,
		   int x, int y, int w, int h,
		   uint32_t bg, uint32_t border_color, int border_size, int new_pixmap)
{
	mvp_widget_t *widget;
	GR_WINDOW_ID pixid;
        mvpw_screen_info_t si;

        mvpw_get_screen_info(&si);
	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);
	if (widget == NULL)
		return NULL;

	GrSelectEvents(widget->wid, widget->event_mask);

	widget->type = MVPW_SURFACE;
	widget->expose = expose;
	widget->destroy = destroy;
	
	memset(&widget->data, 0, sizeof(widget->data));
	
	if ( new_pixmap == 0 )
       	{
		pixid = widget->wid;
	} else {
	        printf("New Pixmap created\n");
		pixid = GrNewPixmap(w, h, NULL);
	}
	widget->data.surface.wid = pixid;
	widget->data.surface.foreground = 0;
	widget->data.surface.fd = 0;	
	
	return widget;
}

int
mvpw_get_surface_attr(mvp_widget_t *widget, mvpw_surface_attr_t *surface)
{
	surface->wid = widget->data.surface.wid;
	surface->foreground = widget->data.surface.foreground;
	surface->fd = widget->data.surface.fd;
	return 0;
}

int 
mvpw_set_surface_attr(mvp_widget_t *widget, mvpw_surface_attr_t *surface)
{
	widget->data.surface.wid = surface->wid;
	widget->data.surface.foreground = surface->foreground;
	widget->data.surface.fd = surface->fd;
	return 0;
}

/*NB must be called when widget is shown, otherwise it will silently fail*/
int
mvpw_set_surface(mvp_widget_t *widget, char *image, int x, int y, int width, int height)
{
        GR_GC_ID gc;
	gc=GrNewGC();

	/*
	 * XXX: GrArea() appears to be broken on the mvp, so set individual
	 * pixels instead
	 */
        MWPIXELVAL c;
	int i, j, k;
	int r, g, b;
	GrSetGCBackground(gc, 0);
	for(i=0 ; i < height ; i++){
		for(j=0 ; j < width ; j++) {
			k = ((i * width) + j) * sizeof(MWPIXELVAL);
			memcpy(&c, image + k, sizeof(MWPIXELVAL));
			r = (c & 0xff0000) >> 16;
			g = (c & 0x00ff00) >> 8;
			b = (c & 0x0000ff) >> 0;
			c = 0xff000000 | (r) | (g << 8) | (b << 16);
			GrSetGCForeground(gc, c);
			GrPoint(widget->data.surface.wid, gc, j+x, i+y);
		}
	}
	GrDestroyGC(gc);
	return 0;
}

int
mvpw_copy_area(mvp_widget_t *widget, int x, int y, int srcwid, int srcx, int srcy, int width, int height)
{
        GR_GC_ID gc;
	gc=GrNewGC();
	
	GrCopyArea(widget->data.surface.wid, gc, x, y, width, height, srcwid, srcx, srcy, MWROP_SRCCOPY);
	GrDestroyGC(gc);
	return 0;
}

int
mvpw_fill_rect(mvp_widget_t *widget, int x, int y, int w, int h, uint32_t* color)
{
        GR_GC_ID gc;
        gc=GrNewGC();

	if (color==NULL) {
		GrSetGCForegroundPixelVal(gc, widget->data.surface.foreground);
	} else {
		GrSetGCForegroundPixelVal(gc, *color);
	}
	GrFillRect(widget->data.surface.wid, gc, x, y, w, h);
	GrDestroyGC(gc);
	return 0;
}
