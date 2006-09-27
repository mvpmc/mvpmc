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

/** \file widget.h
 * Local definitions for libwidget.
 */

#ifndef WIDGET_H
#define WIDGET_H

#include "nano-X.h"

/**
 * Widget identifiers.
 */
typedef enum {
	MVPW_UNKNOWN,		/**< unknown widget type */
	MVPW_ROOT,		/**< root window */
	MVPW_TEXT,		/**< text box */
	MVPW_MENU,		/**< menu */
	MVPW_CONTAINER,		/**< widget container */
	MVPW_IMAGE,		/**< image */
	MVPW_GRAPH,		/**< graph */
	MVPW_CHECKBOX,		/**< checkbox */
	MVPW_BITMAP,		/**< bitmap */
	MVPW_DIALOG,		/**< dialog box */
	MVPW_SURFACE,		/**< drawing surface */
	MVPW_ARRAY,		/**< array */
} mvpw_id_t;

/**
 * Container widget data.
 */
typedef struct {
	int		  nitems;	/**< number of sub-widgets */
	int		  max_items;	/**< size of widgets array */
	mvp_widget_t	**widgets;	/**< sub-widget list */
} mvpw_container_t;

/**
 * Text box data.
 */
typedef struct {
	char		*str;		/**< text string */
	bool		 wrap;		/**< allow text to auto-wrap */
	bool		 pack;		/**< pack text lines */
	bool		 justify;	/**< justification */
	int		 margin;	/**< margin in pixels */
	GR_FONT_ID	 font;		/**< font id */
	GR_COLOR	 fg;		/**< foreground color */
	GR_COLOR	 text_bg;	/**< text background color */
	bool		 rounded;	/**< round or square hilite */
	int		 buflen;	/**< size of str */
	bool		 utf8;		/**< utf8 encoding */
} mvpw_text_t;

typedef struct {
	int rows;
	int cols;
	int col_label_height;
	int row_label_width;
	int cell_height;
	int cell_width;
	int dirty;
	uint32_t array_border;
	int border_size;
	int hilite_x;
	int hilite_y;
	/* Default attributes for cells and headers */
	uint32_t row_label_fg;
	uint32_t row_label_bg;
	uint32_t col_label_fg;
	uint32_t col_label_bg;
	uint32_t cell_fg;
	uint32_t cell_bg;
	uint32_t hilite_fg;
	uint32_t hilite_bg;
	mvpw_array_cell_theme **cell_theme;
	int cell_rounded;
	mvp_widget_t **row_labels;
	mvp_widget_t **col_labels;
	mvp_widget_t **cells;
	int *cell_viz;
	char ** row_strings;
	char ** col_strings;
	char ** cell_strings;
	/* A data pointer identifying the corresponding cell that is provided
	 * by the user and returned to the user when the cell is selected.
	 */
	void ** cell_data;

	/* Placeholder for now, might need others */
	void (*callback_key)(char*, char);
	void (*scroll_callback)(mvp_widget_t *widget, int direction);
} mvpw_array_t;

/**
 * Menu data.
 */
typedef struct {
	char		*title;		/**< title string */
	int		 nitems;	/**< current number of menu items */
	int		 max_items;	/**< max number of items */
	int		 current;	/**< currently hilited item */
	GR_FONT_ID	 font;		/**< font id */
	GR_COLOR	 fg;		/**< foreground color */
	GR_COLOR	 bg;		/**< background color */
	GR_COLOR	 hilite_fg;	/**< hilite foreground color */
	GR_COLOR	 hilite_bg;	/**< hilite background color */
	GR_COLOR	 title_fg;	/**< title foreground color */
	GR_COLOR	 title_bg;	/**< title background color */
	int		 title_justify;	/**< justification for title bar */
	int		 rows;		/**< visible rows */
	bool		 checkboxes;	/**< checkboxes enabled */
	bool		 rounded;	/**< rounded hilite */
	int		 margin;	/**< margin in pixels */
	bool		 utf8;		/**< utf8 encoding */

	mvp_widget_t	*title_widget;	/**< title bar widget */
	mvp_widget_t	*first_widget;	/**< top visible menu item widget */

	/**
	 * Menu item data.
	 */
	struct menu_item_s {
		char		 *label;	/**< text string */
		void		 *key;		/**< menu item key */
		mvp_widget_t	 *widget;	/**< text widget */
		mvp_widget_t	 *checkbox;	/**< checkbox widget */
		bool		  selectable;	/**< selectable item */
		bool		  checked;	/**< checkbox checked */
		GR_COLOR	  fg;		/**< foreground color */
		GR_COLOR	  bg;		/**< background color */
		GR_COLOR	  checkbox_fg;	/**< checkbox color */

		/** callback for item select */
		void		(*select)(mvp_widget_t*, char*, void*);
		/** callback for item hilite */
		void		(*hilite)(mvp_widget_t*, char*, void*, bool);
		/** callback for item destroy */
		void		(*destroy)(mvp_widget_t*, char*, void*);
	} *items;
} mvpw_menu_t;

/**
 * Image data.
 */
typedef struct {
	GR_IMAGE_ID	  iid;		/**< image ID */
	GR_WINDOW_ID	  wid;		/**< drawing window ID */
	GR_WINDOW_ID	  pid;		/**< pixmap window ID */
	char		 *file;		/**< image filename */
} mvpw_image_t;

/**
 * Graph data.
 */
typedef struct {
	GR_COLOR	 fg;		/**< foreground color */
	int		 min;		/**< minimum value */
	int		 max;		/**< maximum value */
	int		 current;	/**< current value */
	bool		 gradient;	/**< gradient fill */
	GR_COLOR	 left;		/**< left color */
	GR_COLOR	 right;		/**< right color */
} mvpw_graph_t;

/**
 * Checkbox data.
 */
typedef struct {
	GR_COLOR	 fg;		/**< foreground color */
	bool		 checked;	/**< checked */
} mvpw_checkbox_t;

/**
 * Bitmap data.
 */
typedef struct {
	char		*image;		/**< image data */
} mvpw_bitmap_t;

/**
 * Dialog box data.
 */
typedef struct {
	int		 modal;		/**< modal */
	GR_COLOR	 fg;		/**< foreground color */
	GR_COLOR	 title_fg;	/**< title foreground color */
	GR_COLOR	 title_bg;	/**< title background color */
	int		 font;		/**< font id */
	mvp_widget_t	*title_widget;	/**< title widget */
	mvp_widget_t	*text_widget;	/**< text widget */
	mvp_widget_t	*image_widget;	/**< image widget */
	int		 margin;	/**< margin in pixel */
	bool		 utf8;		/**< utf8 encoding */
} mvpw_dialog_t;

/**
 * Drawing surface data.
 */
typedef struct {
	int		wid;		/**< drawing window id */
	MWPIXELVAL	foreground;	/**< foreground color */
	int		fd;
} mvpw_surface_t;

/**
 * Widget data.
 */
struct mvp_widget_s {
	mvpw_id_t	 type;		/**< widget type */
	GR_WINDOW_ID	 wid;		/**< window id */
	GR_TIMER_ID	 tid;		/**< timer id */
	mvp_widget_t	*parent;	/**< parent widget */
	GR_COORD	 x;		/**< horizontal coordinate */
	GR_COORD	 y;		/**< vertical coordinate */
	unsigned int	 width;		/**< width in pixels */
	unsigned int	 height;	/**< height in pixels */
	GR_COLOR	 bg;		/**< background color */
	GR_COLOR	 border_color;	/**< border color */
	int		 border_size;	/**< border size */
	GR_EVENT_MASK	 event_mask;	/**< current event mask */
	mvp_widget_t	*attach[4];	/**< attached widgets */
	mvp_widget_t	*above;		/**< widget in front of this widget */
	mvp_widget_t	*below;		/**< widget behind this widget */
	void 		*user_data;	/**< opaque user data */

	/** callback for adding a child widget */
	int (*add_child)(mvp_widget_t*, mvp_widget_t*);
	/** callback for removing a child widget */
	int (*remove_child)(mvp_widget_t*, mvp_widget_t*);

	/** callback for destoying the widget */
	void (*destroy)(mvp_widget_t*);
	/** callback for exposing the widget */
	void (*expose)(mvp_widget_t*);
	/** callback for a remote key */
	void (*key)(mvp_widget_t*, char);
	/** callback for a timer */
	void (*timer)(mvp_widget_t*);
	/** callback for file descriptor input */
	void (*fdinput)(mvp_widget_t*, int);
	/** callback for showing or hiding the widget */
	void (*show)(mvp_widget_t*, bool);

	/** user callback for destroying the widget */
	void (*callback_destroy)(mvp_widget_t*);
	/** user callback for exposing the widget */
	void (*callback_expose)(mvp_widget_t*);
	/** user callback for a remote key */
	void (*callback_key)(mvp_widget_t*, char);
	/** user callback for a timer */
	void (*callback_timer)(mvp_widget_t*);
	/** user callback for file descriptor input */
	void (*callback_fdinput)(mvp_widget_t*, int);

	/**
	 * Widget type specific data.
	 */
	union {
		mvpw_text_t		text;
		mvpw_array_t		array;
		mvpw_menu_t		menu;
		mvpw_container_t	container;
		mvpw_image_t		image;
		mvpw_graph_t		graph;
		mvpw_checkbox_t		checkbox;
		mvpw_bitmap_t		bitmap;
		mvpw_dialog_t		dialog;
		mvpw_surface_t		surface;
	} data;
};

/**
 * Create a new widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param width width
 * \param height height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t* mvpw_create(mvp_widget_t *parent, GR_COORD x, GR_COORD y,
				 unsigned int width, unsigned int height,
				 GR_COLOR bg,
				 GR_COLOR border_color, int border_size);

/**
 * Destroy a widget.
 * \param widget widget handle
 */
extern void mvpw_destroy(mvp_widget_t *widget);

#endif /* WIDGET_H */
