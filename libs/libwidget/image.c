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
expose(mvp_widget_t *widget)
{
}

mvp_widget_t*
mvpw_create_image(mvp_widget_t *parent,
		  int x, int y, int w, int h,
		  uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	GrSelectEvents(widget->wid, widget->event_mask);

	widget->type = MVPW_IMAGE;
	widget->expose = expose;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}

int
mvpw_set_image(mvp_widget_t *widget, char *file)
{
	GR_GC_ID gc;
	GR_WINDOW_ID pid;
	GR_WINDOW_ID wid;
	GR_WM_PROPERTIES props;
	int width, height;

	if (widget == NULL)
		return -1;

	wid = widget->data.image.wid;
	pid = widget->data.image.pid;

	width = widget->width;
	height = widget->height;

	if (pid == 0)
		pid = GrNewPixmap(width, height, NULL);
        gc = GrNewGC();
	GrSetGCForeground(gc, 0xff000000);
	GrFillRect(pid, gc, 0, 0, width, height);
	if (file) GrDrawImageFromFile(pid, gc, 0, 0, width, height, file, 0);
        GrDestroyGC(gc);

	if (wid == 0) {
		wid = GrNewWindowEx(GR_WM_PROPS_APPWINDOW|
				    GR_WM_PROPS_NOAUTOMOVE,
				    NULL, widget->wid, 0, 0, width, height,
				    0xff00ff00);
		GrSetBackgroundPixmap(wid, pid, GR_BACKGROUND_CENTER);

		props.flags = GR_WM_FLAGS_PROPS;
		props.props = GR_WM_PROPS_NODECORATE;
		GrSetWMProperties(wid, &props);

		GrMapWindow(wid);
	} else GrClearArea(wid, 0, 0, 0, 0, 0);

	widget->data.image.wid = wid;
	widget->data.image.pid = pid;

	return 0;
}

int
mvpw_get_image_info(char *file, mvpw_image_info_t *info)
{
	GR_IMAGE_ID iid;
	GR_IMAGE_INFO iif;

	if ((file == NULL) || (info == NULL))
		return -1;

	if ((iid=GrLoadImageFromFile(file, 0)) == 0)
		return -1;

        GrGetImageInfo(iid, &iif);

	info->width = iif.width;
	info->height = iif.height;

        GrFreeImage(iid);

	return 0;
}

int
mvpw_image_destroy(mvp_widget_t *widget)
{
	if (widget == NULL)
		return -1;

	if (widget->data.image.pid)
		GrDestroyWindow(widget->data.image.pid);
	if (widget->data.image.wid)
		GrDestroyWindow(widget->data.image.wid);
	widget->data.image.wid = 0;
	widget->data.image.pid = 0;

	return 0;
}
