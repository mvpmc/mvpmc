/*
 *  Copyright (C) 2006, Sergio Slobodrian
 *  http://mvpmc.org/
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

#ifdef MVPMC_HOST
#define FONT_STANDARD 0
#define FONT_LARGE  0
#else
#define FONT_STANDARD 1000
#define FONT_LARGE  1001
#endif

#if 0
#define PRINTF(x...) printf(x) 
#define TRC(fmt, args...) printf(fmt, ## args)
#else
#define PRINTF(x...)
#define TRC(fmt, args...) 
#endif


static void
destroy(mvp_widget_t *widget)
{
	int r, c, i;
	if (widget->data.array.col_labels) {
		for(c=0;c<widget->data.array.cols+1;c++) {
			if(widget->data.array.col_labels[c]) {
				mvpw_destroy(widget->data.array.col_labels[c]);
			}
		}
		free(widget->data.array.col_labels);
	}
	if (widget->data.array.row_labels) {
		for(r=0;r<widget->data.array.rows;r++) {
			if(widget->data.array.row_labels[r]) {
				mvpw_destroy(widget->data.array.row_labels[r]);
			}
		}
		free(widget->data.array.row_labels);
	}
	for(r = 0; r < widget->data.array.rows; r++)
		for(c = 0; c < widget->data.array.cols; c++) {
			i = widget->data.array.cols * r + c;
			if(widget->data.array.cells[i]) {
				mvpw_destroy(widget->data.array.cells[i]);
			}
		}
	free(widget->data.array.cells);
	free(widget->data.array.cell_viz);
}

static void
expose(mvp_widget_t *widget)
{
	int r, c, i;

	if(widget->data.array.dirty) {
		PRINTF("** SSDEBUG: exposing col labels\n");
		if (widget->data.array.col_labels) {
			for(c=0;c<widget->data.array.cols+1;c++) {
				if(widget->data.array.col_labels[c]) {
					mvpw_expose(widget->data.array.col_labels[c]);
				}
			}
		}
		PRINTF("** SSDEBUG: exposing row labels\n");
		if (widget->data.array.row_labels) {
			for(r=0;r<widget->data.array.rows;r++) {
				if(widget->data.array.row_labels[r]) {
					mvpw_expose(widget->data.array.row_labels[r]);
				}
			}
		}
		PRINTF("** SSDEBUG: exposing cells\n");
		for(r = 0; r < widget->data.array.rows; r++)
			for(c = 0; c < widget->data.array.cols; c++) {
				i = widget->data.array.cols * r + c;
				if(widget->data.array.cells[i]) {
					mvpw_expose(widget->data.array.cells[i]);
						/*PRINTF("** SSDEBUG: exposing cell(%d)\n", i);*/
				}
			}
		widget->data.array.dirty = 0;
	}
}

static void
show(mvp_widget_t *widget, int shw)
{
	int r, c, i;
	PRINTF("** SSDEBUG: %s col labels\n", shw?"showing":"hiding");
	if (widget->data.array.col_labels) {
		for(c=0;c<widget->data.array.cols+1;c++) {
			if(widget->data.array.col_labels[c]) {
				if(shw)
					mvpw_show(widget->data.array.col_labels[c]);
				else
					mvpw_hide(widget->data.array.col_labels[c]);
			}
		}
	}
	PRINTF("** SSDEBUG: %s row labels\n", shw?"showing":"hiding");
	if (widget->data.array.row_labels) {
		for(r=0;r<widget->data.array.rows;r++) {
			if(widget->data.array.row_labels[r]) {
				if(shw)
					mvpw_show(widget->data.array.row_labels[r]);
				else
					mvpw_hide(widget->data.array.row_labels[r]);
			}
		}
	}
	PRINTF("** SSDEBUG: %s cells\n", shw?"showing":"hiding");
	for(r = 0; r < widget->data.array.rows; r++)
		for(c = 0; c < widget->data.array.cols; c++) {
			i = widget->data.array.cols * r + c;
			if(widget->data.array.cells[i]) {
				/*PRINTF("** SSDEBUG: %s cell(%d)\n", shw?"showing":"hiding",i);*/
				if(shw && widget->data.array.cell_viz[i])
					mvpw_show(widget->data.array.cells[i]);
				else
					mvpw_hide(widget->data.array.cells[i]);
			}
		}
	widget->data.array.dirty = 1;
}

mvp_widget_t*
mvpw_create_array(mvp_widget_t *parent,
		 int x, int y, int w, int h,
		 uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	if (widget == NULL)
		return NULL;

	widget->type = MVPW_ARRAY;
	widget->expose = expose;
	widget->destroy = destroy;
	widget->show = show;

	memset(&widget->data, 0, sizeof(widget->data));
	widget->data.array.hilite_x = -1;
	widget->data.array.hilite_y = -1;
	/* Managing our own dirtyness minimizes the calls to expose
	 * which seem to pile up and not be consolidated to one by
	 * microwindows.
	 */
	widget->data.array.dirty = 1;

	return widget;
}

/* This function will perform the initial creation of cells, headers,
 * and will lay-out an array widget based on its attributes. It is
 * called by the mvpw_set_array_attribute function. 
 */
static void
mvpw_array_layout(mvp_widget_t *widget)
{
	int x, y, w, h; 
	int new_w, new_h;
	int cell_w, cell_h;
	int rlw, clh;
	int r, c, i;
	mvpw_text_attr_t ta;

	/* Calculate the size of the cells based on header sizes
	 * and overall array size.
	 */
	x = widget->x;
	y = widget->y;
	w = widget->width;
	h = widget->height;
	r = widget->data.array.rows;
	c = widget->data.array.cols;
	rlw = widget->data.array.row_label_width;
	clh = widget->data.array.col_label_height;
	new_w = w - rlw;
	new_h = h - clh;
	cell_w = new_w/widget->data.array.cols;
	cell_h = new_h/widget->data.array.rows;
	widget->data.array.cell_width = cell_w;
	widget->data.array.cell_height = cell_h;
	/*
	 * Create the widget arrays that hold the headers and cells.
	 */
	PRINTF("** SSDEBUG: creating row label array\n");
	if(!widget->data.array.row_labels
		 && widget->data.array.row_label_width != 0) {
		widget->data.array.row_labels = malloc(r * sizeof(mvp_widget_t*));
		memset(widget->data.array.row_labels, 0, r * sizeof(mvp_widget_t*));
	}
			
	PRINTF("** SSDEBUG: creating column label array\n");
	if(!widget->data.array.col_labels 
		 && widget->data.array.col_label_height != 0) {
		widget->data.array.col_labels = malloc((c+1) * sizeof(mvp_widget_t*));
		memset(widget->data.array.col_labels, 0, (c+1) * sizeof(mvp_widget_t*));
	}

	PRINTF("** SSDEBUG: creating cell array\n");
	if(!widget->data.array.cells) {
		widget->data.array.cells = malloc(r * c * sizeof(mvp_widget_t*));
		memset(widget->data.array.cells, 0, r * c * sizeof(mvp_widget_t*));
	}

	/*
	 * Create the cell index array (user data for each cell).
	 */
	PRINTF("** SSDEBUG: creating user data pointer array\n");
	if(!widget->data.array.cell_data) {
		widget->data.array.cell_data = malloc(r * c
					*	sizeof(*(widget->data.array.cell_data)));
		memset(widget->data.array.cell_data, 0, r * c
					*	sizeof(*(widget->data.array.cell_data)));
	}

	/*
	 * Create the row header string array.
	 */
	PRINTF("** SSDEBUG: creating row header string array\n");
	if(!widget->data.array.row_strings) {
		widget->data.array.row_strings = malloc(c
					*	sizeof(*(widget->data.array.row_strings)));
		memset(widget->data.array.row_strings, 0, c
					*	sizeof(*(widget->data.array.row_strings)));
	}

	/*
	 * Create the column header string array.
	 */
	PRINTF("** SSDEBUG: creating column header string array\n");
	if(!widget->data.array.col_strings) {
		widget->data.array.col_strings = malloc(r
					*	sizeof(*(widget->data.array.col_strings)));
		memset(widget->data.array.col_strings, 0, r
					*	sizeof(*(widget->data.array.col_strings)));
	}

	/*
	 * Create the cell string array.
	 */
	PRINTF("** SSDEBUG: creating cell string array\n");
	if(!widget->data.array.cell_strings) {
		widget->data.array.cell_strings = malloc(r * c
					*	sizeof(*(widget->data.array.cell_strings)));
		memset(widget->data.array.cell_strings, 0, r * c
					*	sizeof(*(widget->data.array.cell_strings)));
	}

	/*
	 * Create the visibility array.
	 */
	PRINTF("** SSDEBUG: creating the visibility array\n");
	if(!widget->data.array.cell_viz) {
		widget->data.array.cell_viz = malloc(r * c
					*	sizeof(*(widget->data.array.cell_viz)));
		memset(widget->data.array.cell_strings, 0, r * c
					*	sizeof(*(widget->data.array.cell_viz)));
	}

	/*
	 * Fill the headers with empty text widgets
	 */
	PRINTF("** SSDEBUG: creating column label widgets\n");
	for(c=0;c<widget->data.array.cols+1;c++) {
		if(!widget->data.array.col_labels[c]) {
			widget->data.array.col_labels[c] =
				mvpw_create_text(widget,
					c==0?0:0+rlw+cell_w*(c-1), 0, c==0?rlw:cell_w, clh,
					widget->data.array.col_label_bg, MVPW_LIGHTGREY, 3);
			mvpw_get_text_attr(widget->data.array.col_labels[c], &ta);
			ta.margin = 0;
			ta.pack = 1;
			ta.justify = MVPW_TEXT_CENTER;
			ta.font = FONT_LARGE;
			ta.fg = widget->data.array.col_label_fg;
			ta.bg = widget->data.array.col_label_bg;
			mvpw_set_text_attr(widget->data.array.col_labels[c], &ta);
			PRINTF("** SSDEBUG: creating col label %d @ (%d,%d)\n", c,
						c==0?0:0+rlw+cell_w*(c-1), 0);
		}
	}

	PRINTF("** SSDEBUG: creating row label widgets\n");
	for(r=0;r<widget->data.array.rows;r++) {
		if(!widget->data.array.row_labels[r]) {
			widget->data.array.row_labels[r] =
				mvpw_create_text(widget,
					0, 0+clh+cell_h*r, rlw, cell_h,
					widget->data.array.row_label_bg, MVPW_LIGHTGREY, 3);
			mvpw_get_text_attr(widget->data.array.row_labels[r], &ta);
			ta.wrap = 1;
			ta.pack = 1;
			ta.margin = 0;
			ta.justify = MVPW_TEXT_CENTER;
			ta.font = FONT_LARGE;
			ta.fg = widget->data.array.row_label_fg;
			ta.bg = widget->data.array.row_label_bg;
			mvpw_set_text_attr(widget->data.array.row_labels[r], &ta);
			PRINTF("** SSDEBUG: creating row label %d @ (%d,%d)\n", r,
						0, 0+clh+cell_h*r);
		}
	}

	/*
	 * Create empty text widgets that represent each of the cells
	 */
	PRINTF("** SSDEBUG: creating cell widgets\n");
	for(r = 0; r < widget->data.array.rows; r++)
		for(c = 0; c < widget->data.array.cols; c++) {
			i = widget->data.array.cols * r + c;
			if(!widget->data.array.cells[i]) {
				widget->data.array.cells[i] =
					mvpw_create_text(widget,
						rlw+0+cell_w*c, clh+0+cell_h*r, cell_w, cell_h,
						widget->data.array.cell_bg, MVPW_LIGHTGREY, 3);
				mvpw_get_text_attr(widget->data.array.cells[i], &ta);
				ta.wrap = 1;
				ta.pack = 1;
				ta.margin = 0;
				ta.justify = MVPW_TEXT_CENTER;
				ta.font = FONT_LARGE;
				ta.fg = widget->data.array.cell_fg;
				ta.bg = widget->data.array.cell_bg;
				mvpw_set_text_attr(widget->data.array.cells[i], &ta);
				PRINTF("** SSDEBUG: creating cell %d,%d @ (%d,%d)\n", r, c,
							rlw+0+cell_w*c, clh+0+cell_h*r);
			}
		}

	/*
	 * Initialize the visibility
	 */
	PRINTF("** SSDEBUG: initializing visiblity array.\n");
	for(r = 0; r < widget->data.array.rows; r++)
		for(c = 0; c < widget->data.array.cols; c++) {
			i = widget->data.array.cols * r + c;
			widget->data.array.cell_viz[i] = 1;
		}

	widget->data.array.dirty = 1;
}

void
mvpw_set_array_row(mvp_widget_t *widget, int which, char * string,
	mvpw_text_attr_t * attr)
{
	if(widget->data.array.row_labels[which]) {
		if(string)
			mvpw_set_text_str(widget->data.array.row_labels[which], string);
		if(attr)
			mvpw_set_text_attr(widget->data.array.row_labels[which], attr);
	}
	widget->data.array.dirty = 1;
}

void
mvpw_set_array_row_bg(mvp_widget_t *widget, int which, uint32_t bg)
{
	mvpw_text_attr_t ta;
	if(widget->data.array.row_labels[which]) {
		mvpw_get_text_attr(widget->data.array.row_labels[which], &ta);
		ta.bg = bg;
		/* There's a bug in the text widget, need to set the widget
		 * background as well for some reason. Look into this
		 */
		widget->data.array.row_labels[which]->bg = bg;
		mvpw_set_text_attr(widget->data.array.row_labels[which], &ta);
	}
	widget->data.array.dirty = 1;
}

void
mvpw_set_array_col(mvp_widget_t *widget, int which, char * string,
	mvpw_text_attr_t * attr)
{
	if(widget->data.array.col_labels[which]) {
		if(string)
			mvpw_set_text_str(widget->data.array.col_labels[which], string);
		if(attr)
			mvpw_set_text_attr(widget->data.array.col_labels[which], attr);
	}
	widget->data.array.dirty = 1;
}

void
mvpw_set_array_cell(mvp_widget_t *widget, int x, int y, char * string,
	mvpw_text_attr_t * attr)
{
	int i;
	i = widget->data.array.cols * y + x;
	if(widget->data.array.cells[i]) {
		if(string)
			mvpw_set_text_str(widget->data.array.cells[i], string);
		if(attr)
			mvpw_set_text_attr(widget->data.array.cells[i], attr);
	}
	widget->data.array.dirty = 1;
}

void
mvpw_set_array_cell_data(mvp_widget_t *widget, int x, int y, void * data)
{
	int i;
	i = widget->data.array.cols * y + x;
	if(widget->data.array.cells[i])
		if(widget->data.array.cell_data)
			widget->data.array.cell_data[i] = data;
}

void *
mvpw_get_array_cell_data(mvp_widget_t *widget, int x, int y)
{
	int i;
	void * rtrn = NULL;

	i = widget->data.array.cols * y + x;
	if(widget->data.array.cells[i])
		if(widget->data.array.cell_data)
			rtrn = widget->data.array.cell_data[i];

	return rtrn;
}

void *
mvpw_get_array_cur_cell_data(mvp_widget_t *widget)
{
	int i;
	void * rtrn = NULL;

	i = widget->data.array.cols * widget->data.array.hilite_y
															+	widget->data.array.hilite_x;
	if(widget->data.array.cells[i])
		if(widget->data.array.cell_data)
			rtrn = widget->data.array.cell_data[i];

	return rtrn;
}

void
mvpw_reset_array_cells(mvp_widget_t * widget)
{
	int i,len;
	len = widget->data.array.cols * widget->data.array.rows;

	for(i=0;i<len;i++) {
		mvpw_resize(widget->data.array.cells[i],
								widget->data.array.cell_width,
								widget->data.array.cell_height);
		mvpw_show(widget->data.array.cells[i]);
	}
	widget->data.array.dirty = 1;
}

void
mvpw_set_array_cell_span(mvp_widget_t * widget, int x, int y, int span)
{
	int i = widget->data.array.cols * y + x;

	mvpw_resize(widget->data.array.cells[i], widget->data.array.cell_width*span,
							widget->data.array.cell_height);
	switch(span) {
		case 1:
			widget->data.array.cell_viz[i] = 1;
			mvpw_show(widget->data.array.cells[i]);
		break;
		case 2:
			widget->data.array.cell_viz[i+1] = 0;
			mvpw_hide(widget->data.array.cells[i+1]);
		break;
		case 3:
			widget->data.array.cell_viz[i+1] = 0;
			mvpw_hide(widget->data.array.cells[i+1]);
			widget->data.array.cell_viz[i+2] = 0;
			mvpw_hide(widget->data.array.cells[i+2]);
		break;
	}
	widget->data.array.dirty = 1;
}

void
mvpw_set_array_scroll(mvp_widget_t * widget, 
	 void (*scroll_callback)(mvp_widget_t *widget, int direction))
{
	widget->data.array.scroll_callback = scroll_callback;
}

void
mvpw_reset_array_selection(mvp_widget_t *widget)
{
	mvpw_hilite_array_cell(widget,
		widget->data.array.hilite_x,
		widget->data.array.hilite_y, 0);
	widget->data.array.hilite_x = 0;
	widget->data.array.hilite_y = 0;
	mvpw_hilite_array_cell(widget,
		widget->data.array.hilite_x,
		widget->data.array.hilite_y, 1);
}

void
mvpw_move_array_selection(mvp_widget_t *widget, int direction)
{
	int ofs;
	/*
	PRINTF("** SSDEBUG: %s called in file %s on line %d with direction %d\n",
		__FUNCTION__, __FILE__, __LINE__, direction);
	*/
	switch(direction) {
		case MVPW_ARRAY_LEFT:
			if(widget->data.array.hilite_x > 0) {
				/*
				PRINTF("** SSEDBUG: moving LEFT from %d,%d\n",
					widget->data.array.hilite_x,widget->data.array.hilite_y);
				*/
				mvpw_hilite_array_cell(widget,
					widget->data.array.hilite_x,
					widget->data.array.hilite_y, 0);
				/* Determine if we're moving left to a multispan cell
				 * and if so, skip further left
				 */
				ofs = widget->data.array.cols * widget->data.array.hilite_y
							+ widget->data.array.hilite_x;
				if(widget->data.array.cell_viz[ofs-1] == 0)
					widget->data.array.hilite_x -= 1;
				widget->data.array.hilite_x -= 1;
				mvpw_hilite_array_cell(widget,
					widget->data.array.hilite_x,
					widget->data.array.hilite_y, 1);
			}
			else if(widget->data.array.scroll_callback) {
				(*widget->data.array.scroll_callback)(widget, MVPW_ARRAY_RIGHT);
			}
		break;
		case MVPW_ARRAY_RIGHT:
			ofs = widget->data.array.cols * widget->data.array.hilite_y
						+ widget->data.array.hilite_x;
			mvpw_hilite_array_cell(widget,
				widget->data.array.hilite_x,
				widget->data.array.hilite_y, 0);
			if(widget->data.array.cell_viz[ofs+1] == 0) {
					widget->data.array.hilite_x += 1;
				if(widget->data.array.hilite_x < widget->data.array.cols - 1
			   	&& widget->data.array.cell_viz[ofs+2] == 0)
				 	widget->data.array.hilite_x += 1;
			}
			/*
			PRINTF("** SSEDBUG: moving RIGHT from %d,%d\n",
							widget->data.array.hilite_x,widget->data.array.hilite_y);
			*/
			if(widget->data.array.hilite_x < widget->data.array.cols - 1) {
				widget->data.array.hilite_x += 1;
				mvpw_hilite_array_cell(widget,
					widget->data.array.hilite_x,
					widget->data.array.hilite_y, 1);
			}
			else if(widget->data.array.scroll_callback) {
				(*widget->data.array.scroll_callback)(widget, MVPW_ARRAY_LEFT);
				ofs = widget->data.array.cols * widget->data.array.hilite_y
						+ widget->data.array.hilite_x;
				if(widget->data.array.cell_viz[ofs] == 0) {
					widget->data.array.hilite_x -=1;
					if(widget->data.array.cell_viz[ofs-1] == 0)
						widget->data.array.hilite_x -=1;
				}
				mvpw_hilite_array_cell(widget,
					widget->data.array.hilite_x,
					widget->data.array.hilite_y, 1);
			}
		break;
		case MVPW_ARRAY_UP:
			mvpw_hilite_array_cell(widget,
				widget->data.array.hilite_x,
				widget->data.array.hilite_y, 0);
			if(widget->data.array.hilite_y > 0) {
				/*
				PRINTF("** SSEDBUG: moving UP from %d,%d\n",
								widget->data.array.hilite_x,widget->data.array.hilite_y);
				*/
				widget->data.array.hilite_y -= 1;
			}
			else if(widget->data.array.scroll_callback) {
				(*widget->data.array.scroll_callback)(widget, MVPW_ARRAY_DOWN);
			}
			ofs = widget->data.array.cols * widget->data.array.hilite_y
						+ widget->data.array.hilite_x;
			if(widget->data.array.hilite_x > 0 &&
					widget->data.array.cell_viz[ofs] == 0) {
				widget->data.array.hilite_x -=1;
				if(widget->data.array.hilite_x > 0 &&
						widget->data.array.cell_viz[ofs-1] == 0)
					widget->data.array.hilite_x -=1;
			}
			mvpw_hilite_array_cell(widget,
				widget->data.array.hilite_x,
				widget->data.array.hilite_y, 1);
		break;
		case MVPW_ARRAY_DOWN:
			mvpw_hilite_array_cell(widget,
				widget->data.array.hilite_x,
				widget->data.array.hilite_y, 0);
			if(widget->data.array.hilite_y < widget->data.array.rows - 1) {
				/*
				PRINTF("** SSEDBUG: moving DOWN from %d,%d\n",
								widget->data.array.hilite_x,widget->data.array.hilite_y);
				*/
				widget->data.array.hilite_y += 1;
			}
			else if(widget->data.array.scroll_callback) {
				(*widget->data.array.scroll_callback)(widget, MVPW_ARRAY_UP);
			}
			ofs = widget->data.array.cols * widget->data.array.hilite_y
						+ widget->data.array.hilite_x;
			if(widget->data.array.hilite_x > 0 &&
					widget->data.array.cell_viz[ofs] == 0) {
				widget->data.array.hilite_x -=1;
				if(widget->data.array.hilite_x > 0 &&
						widget->data.array.cell_viz[ofs-1] == 0)
					widget->data.array.hilite_x -=1;
			}
			mvpw_hilite_array_cell(widget,
				widget->data.array.hilite_x,
				widget->data.array.hilite_y, 1);
		break;
		case MVPW_ARRAY_PAGE_UP:
			(*widget->data.array.scroll_callback)(widget, MVPW_ARRAY_PAGE_DOWN);
		break;
		case MVPW_ARRAY_PAGE_DOWN:
			(*widget->data.array.scroll_callback)(widget, MVPW_ARRAY_PAGE_UP);
		break;
	}
}

void
mvpw_hilite_array_cell(mvp_widget_t *widget, int x, int y, int hlt)
{
	int i;
	mvpw_text_attr_t ta;

	if(y<0 || x<0) return;

	i = widget->data.array.cols * y + x;
	if(widget->data.array.cells[i]) {
		mvpw_get_text_attr(widget->data.array.cells[i], &ta);
		if(hlt) {
			ta.fg = widget->data.array.hilite_fg;
			ta.bg = widget->data.array.hilite_bg;
			widget->data.array.hilite_x = x;
			widget->data.array.hilite_y = y;
		}
		else {
			ta.fg = widget->data.array.cell_fg;
			ta.bg = widget->data.array.cell_bg;
		}
		mvpw_set_text_attr(widget->data.array.cells[i], &ta);
		mvpw_set_bg(widget->data.array.cells[i], ta.bg);
		/*
		mvpw_expose(widget->data.array.cells[i]); */
	}
}

void
mvpw_set_array_attr(mvp_widget_t *widget, mvpw_array_attr_t *attr)
{
	widget->data.array.rows = attr->rows;
	widget->data.array.cols = attr->cols;
	widget->data.array.col_label_height = attr->col_label_height;
	widget->data.array.row_label_width = attr->row_label_width;
	widget->data.array.array_border = attr->array_border;
	widget->data.array.border_size = attr->border_size;
	widget->data.array.row_label_fg = attr->row_label_fg;
	widget->data.array.row_label_bg = attr->row_label_bg;
	widget->data.array.col_label_fg = attr->col_label_fg;
	widget->data.array.col_label_bg = attr->col_label_bg;
	widget->data.array.cell_fg = attr->cell_fg;
	widget->data.array.cell_bg = attr->cell_bg;
	widget->data.array.hilite_fg = attr->hilite_fg;
	widget->data.array.hilite_bg = attr->hilite_bg;
	widget->data.array.cell_rounded = attr->cell_rounded;

	if (widget->border_color != attr->array_border) {
		GrSetWindowBorderColor(widget->wid, attr->array_border);
		widget->border_color = attr->array_border;
	}
	widget->border_size = attr->border_size;

	mvpw_array_layout(widget);

	widget->data.array.dirty = 1;
	mvpw_expose(widget);
}

void
mvpw_get_array_attr(mvp_widget_t *widget, mvpw_array_attr_t *attr)
{
	attr->rows = widget->data.array.rows;
	attr->cols = widget->data.array.cols;
	attr-> col_label_height = widget->data.array.col_label_height;
	attr->row_label_width = widget->data.array.row_label_width;
	attr->array_border = widget->data.array.array_border;
	attr->border_size = widget->data.array.border_size;
	attr->row_label_fg = widget->data.array.row_label_fg;
	attr->row_label_bg = widget->data.array.row_label_bg;
	attr->col_label_fg = widget->data.array.col_label_fg;
	attr->col_label_bg = widget->data.array.col_label_bg;
	attr->cell_fg = widget->data.array.cell_fg;
	attr->cell_bg = widget->data.array.cell_bg;
	attr->hilite_fg = widget->data.array.hilite_fg;
	attr->hilite_bg = widget->data.array.hilite_bg;
	attr->cell_rounded = widget->data.array.cell_rounded;

	attr->array_border = widget->border_color;
	attr->border_size = widget->border_size;
}

void mvpw_array_clear_dirty(mvp_widget_t *widget)
{
	widget->data.array.dirty = 0;
}

#if 0
void
mvpw_set_array_size(mvp_widget_t *widget, int w, int h)
{
}

void
mvpw_set_array_col_label_h(mvp_widget_t *widget, int h)
{
}

void
mvpw_set_arraw_row_label_w(mvp_widget_t *widget, int w)
{
}

void
mvpw_set_array_row_label_attr(mvp_widget_t *widget, int r, mvpw_text_attr_t a)
{
}

void
mvpw_set_array_col_label_attr(mvp_widget_t *widget, int c, mvpw_text_attr_t a)
{
}

void
mvpw_set_array_cel_label_attr(mvp_widget_t *widget, int x, int y, 
															mvpw_text_attr_t a)
{
}
#endif
