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
	GR_GC_ID gc;

	if (!widget->data.checkbox.checked)
		return;

	gc = GrNewGC();
	GrSetGCForeground(gc, widget->data.checkbox.fg);
	GrSetGCBackground(gc, 0);

	GrFillRect(widget->wid, gc, 4, 4, widget->width-8, widget->height-8);

	GrDestroyGC(gc);
}

/**
 * \brief	Create a checkbox widget within the specified parent widget.
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
 * \return	a pointer to the created checkbox widget
 *
 * Create a checkbox widget within a parent widget. The checkbox is created
 * with its upper left corner located at an offset specified by the (x,y)
 * arguments from the upper left corner of the parent widget.  The checkbox
 * size is specified by the (w,h) arguments. 
 *
 * If the \a parent argument is \a NULL the checkbox will be a created in the
 * \a ROOT window.
 *
 * If the checkbox cannot be created, a \a NULL pointer will be returned.
 **/
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

/**
 * \brief	Set the foreground color of a checkbox widget.
 * 
 *	param[in]	widget	a pointer to the checkbox widget
 *	param[in]	fg	the forground color to use for the widget
 *
 **/
void
mvpw_set_checkbox_fg(mvp_widget_t *widget, uint32_t fg)
{
	widget->data.checkbox.fg = fg;
}

/**
 * \brief	Set the checked state of a checkbox widget.
 * 
 *	param[in]	widget	a pointer to the checkbox widget
 *	param[in]	checked	the state to set (0 is unchecked, 1 is checked)
 *
 **/
void
mvpw_set_checkbox(mvp_widget_t *widget, bool checked)
{
	widget->data.checkbox.checked = checked;
}
