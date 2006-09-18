/*
 *  Copyright (C) 2004-2006, Jon Gettler
 *  Copyright (C) 2006, Sergio Slobodrian (array widget)
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

/** \file mvp_widget.h
 * mvpmc widget library.  This library acts as a simple windowing API sitting
 * on top of microwindows.
 */

#ifndef MVP_WIDGET_H
#define MVP_WIDGET_H

#include <stdint.h>

#if !defined(__cplusplus) && !defined(HAVE_TYPE_BOOL)
#define HAVE_TYPE_BOOL
/**
 * Boolean type.
 */
typedef enum {
	false = 0,
	true = 1
} bool;
#endif /* !__cplusplus && !HAVE_TYPE_BOOL */

#define MVPW_DIR_UP	0	/**< widget above */
#define MVPW_DIR_DOWN	1	/**< widget below */
#define MVPW_DIR_LEFT	2	/**< widget left */
#define MVPW_DIR_RIGHT	3	/**< widget right */

typedef struct mvp_widget_s mvp_widget_t;

/**
 * Widget information
 */
typedef struct {
	int x;		/**< horizontal coordinate */
	int y;		/**< vertical coordinate */
	int w;		/**< width */
	int h;		/**< height */
} mvpw_widget_info_t;

/**
 * Screen information
 */
typedef struct {
	int rows;	/**< rows */
	int cols;	/**< columns */
	int bpp;	/**< bits per pixel */
	int pixtype;	/**< pixel type */
} mvpw_screen_info_t;

/**
 * Initialize the widget library.
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_init(void);

/**
 * Enter the widget library event loop.
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_event_loop(void);

/**
 * Flush all existing events.
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_event_flush(void);

/**
 * Return the root window widget.
 * \return root window widget
 */
extern mvp_widget_t *mvpw_get_root(void);

/**
 * Set the idle callback routine.
 * \param callback idle callback function (NULL to disable)
 */
extern void mvpw_set_idle(void (*callback)(void));

/**
 * Set the expose callback for a widget.
 * \param widget widget handle
 * \param callback callback function (NULL to disable)
 */
extern void mvpw_set_expose_callback(mvp_widget_t *widget,
				     void (*callback)(mvp_widget_t*));

/**
 * Retrieve widget information
 * \param widget widget handle
 * \param[out] info widget information
 */
extern void mvpw_get_widget_info(mvp_widget_t *widget, mvpw_widget_info_t *info);

/**
 * Get screen information
 * \param[out] info screen information
 */
extern void mvpw_get_screen_info(mvpw_screen_info_t *info);

/**
 * Set the background color of the widget.
 * \param widget widget handle
 * \param bg background color
 */
extern void mvpw_set_bg(mvp_widget_t *widget, uint32_t bg);

/**
 * Retrieve the background color of a widget.
 * \param widget widget handle
 * \return background color
 */
extern uint32_t mvpw_get_bg(const mvp_widget_t *widget);

/**
 * Set the timer callback and timeout value for a widget.
 * \param widget widget handle
 * \param callback callback function (NULL to disable)
 * \param timeout timeout in microseconds
 */
extern void mvpw_set_timer(mvp_widget_t *widget,
			   void (*callback)(mvp_widget_t*), uint32_t timeout);

/**
 * Change focus to this widget.
 * \param widget widget handle
 */
extern void mvpw_focus(mvp_widget_t *widget);

/**
 * Get the widget that currently has focus.
 * \return handle to the widget with focus
 */
extern mvp_widget_t *mvpw_get_focus(void);

/**
 * Display a widget on the screen.
 * \param widget widget handle
 */
extern void mvpw_show(mvp_widget_t *widget);

/**
 * Make a widget not visible on the screen.
 * \param widget widget to be hidden
 */
extern void mvpw_hide(mvp_widget_t *widget);

/**
 * Raise a widget so that it is fully visible.
 * \param widget widget handle
 */
extern void mvpw_raise(mvp_widget_t *widget);

/**
 * Lower a widget so that other widgets are visible over top of it.
 * \param widget widget handle
 */
extern void mvpw_lower(mvp_widget_t *widget);

/**
 * Force an expose event on a widget.
 * \param widget widget handle
 */
extern void mvpw_expose(const mvp_widget_t *widget);

/**
 * Return the height of a font.
 * \param font font ID
 * \param utf8 1 if UTF8 encoding is used, 0 otherwise
 * \return font height in pixels
 */
extern int mvpw_font_height(int font, bool utf8);

/**
 * Return the width for a string using a certain font.
 * \param font font ID
 * \param str string to calculate width for
 * \param utf8 1 if UTF8 encoding is used, 0 otherwise
 * \return string width in pixels
 */
extern int mvpw_font_width(int font, char *str, bool utf8);

/**
 * Load a font from a file.
 * \param file file name
 * \return font ID
 */
extern int mvpw_load_font(char *file);

/**
 * Move a widget, and all attached widgets, by a certain number of pixels.
 * \param widget widget handle
 * \param x horizontal offset
 * \param y vertical offset
 */
extern void mvpw_move(mvp_widget_t *widget, int x, int y);

/**
 * Move a widget, and all attached widgets, to a specific location.
 * \param widget widget handle
 * \param x horizontal coordinate
 * \param y vertical coordinate
 */
extern void mvpw_moveto(mvp_widget_t *widget, int x, int y);

/**
 * Resize a widget.
 * \param widget widget handle
 * \param w new width
 * \param h new height
 */
extern void mvpw_resize(const mvp_widget_t *widget, int w, int h);

/**
 * Attach two widgets together, so they can be moved in unison.
 * \param w1 widget handle
 * \param w2 widget handle
 * \param direction w2 should be in this direction from w2
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_attach(mvp_widget_t *w1, mvp_widget_t *w2, int direction);

/**
 * Detach widgets from each other.
 * \param widget widget handle
 * \param direction unattach the widget in this direction from the widget
 */
extern void mvpw_unattach(mvp_widget_t *widget, int direction);

/**
 * Register some user data for later retrieval by any callback.
 * \param widget widget handle
 * \param user_data a pointer to the data
 */
extern void mvpw_set_user_data(mvp_widget_t *widget,void *user_data);

/**
 * Retrieve pointer previously registered using mvpw_set_user_data
 * \param widget widget handle
 * \return a pointer to the user_data
 */
extern void *mvpw_get_user_data(mvp_widget_t *widget);

/**
 * Register a key callback on a widget.
 * \param widget widget handle
 * \param callback callback function
 */
extern void mvpw_set_key(mvp_widget_t *widget,
			 void (*callback)(mvp_widget_t*, char));

/**
 * Destroy a widget.
 * \param widget widget handle
 */
extern void mvpw_destroy(mvp_widget_t *widget);

/**
 * Determine if a widget is currently visible on the screen.
 * \param widget widget handle
 * \retval 1 widget is visible
 * \retval 0 widget is not visible
 */
extern int mvpw_visible(const mvp_widget_t *widget);

/**
 * Add a callback that will be called for every keystroke.
 * \param callback callback function
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_keystroke_callback(void (*callback)(char));

/**
 * Set the screensaver callback and timeout.
 * \param widget widget handle
 * \param seconds timeout in seconds (0 will disable screensaver)
 * \param callback callback function
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_screensaver(mvp_widget_t *widget, int seconds,
				void (*callback)(mvp_widget_t*, int));

extern void mvpw_set_fdinput(mvp_widget_t *widget,
			 void (*callback)(mvp_widget_t*, int));
extern int mvpw_fdinput_callback(void (*callback)(void));

/**
 * Change the parent of a widget.
 * \param child widget handle to change
 * \param parent widget handle of new parent (NULL for root window)
 */
extern void mvpw_reparent(mvp_widget_t *child, mvp_widget_t *parent);

extern int mvpw_read_area(mvp_widget_t *widget, int x, int y, int w, int h,
			  unsigned long *pixels);

/**
 * Create a container widget
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t *mvpw_create_container(mvp_widget_t *parent,
					   int x, int y, int w, int h,
					   uint32_t bg, uint32_t border_color,
					   int border_size);

/*
 * text widget
 */
#define MVPW_TEXT_LEFT		0	/**< left justified */
#define MVPW_TEXT_RIGHT		1	/**< right justified */
#define MVPW_TEXT_CENTER	2	/**< center justified */

/**
 * text attributes
 */
typedef struct {
	bool	 	wrap;		/**< auto-wrap text */
	bool	 	justify;	/**< justification type */
	bool		pack;
	int	 	margin;		/**< margin in pixels */
	int	 	font;		/**< font id */
	uint32_t 	fg;		/**< foreground color */
	uint32_t 	bg;		/**< background color */
	uint32_t 	border;		/**< border color */
	bool	 	rounded;	/**< rounded or square hilite */
	int	 	border_size;	/**< border size in pixels */
	bool	 	utf8;		/**< utf8 encoding */
} mvpw_text_attr_t;

/**
 * Create a text widget
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t *mvpw_create_text(mvp_widget_t *parent,
				      int x, int y, int w, int h,
				      uint32_t bg, uint32_t border_color,
				      int border_size);

/**
 * Set the text string in a text widget.
 * \param widget widget handle
 * \param str string to display (NULL for no string)
 */
extern void mvpw_set_text_str(mvp_widget_t *widget, char *str);

/**
 * Retrieve the current string for a text widget.
 * \param widget widget handle
 * \return text string
 */
extern char *mvpw_get_text_str(mvp_widget_t *widget);

/**
 * Set the widget attributes for a text widget.
 * \param widget widget handle
 * \param attr text attributes
 */
extern void mvpw_set_text_attr(mvp_widget_t *widget, mvpw_text_attr_t *attr);

/**
 * Get the widget attributes for a text widget.
 * \param widget widget handle
 * \param[out] attr text attributes
 */
extern void mvpw_get_text_attr(mvp_widget_t *widget, mvpw_text_attr_t *attr);

/**
 * Set the foreground color of a text widget.
 * \param widget widget handle
 * \param fg color
 */
extern void mvpw_set_text_fg(mvp_widget_t *widget, uint32_t fg);

/**
 * Get the foreground color of a text widget.
 * \param widget widget handle
 */
extern uint32_t  mvpw_get_text_fg(mvp_widget_t *widget);

/*
 * array widget
 */

#define MVPW_ARRAY_LEFT 0
#define MVPW_ARRAY_RIGHT 1
#define MVPW_ARRAY_UP 2
#define MVPW_ARRAY_DOWN 3
#define MVPW_ARRAY_PAGE_UP 4
#define MVPW_ARRAY_PAGE_DOWN 5
#define MVPW_ARRAY_HOLD 6

typedef struct {
	uint32_t cell_fg;
	uint32_t cell_bg;
	uint32_t hilite_fg;
	uint32_t hilite_bg;
} mvpw_array_cell_theme;

typedef struct {
	int rows;
	int cols;
	int col_label_height;
	int row_label_width;
	uint32_t array_border;
	int border_size;
	/* Default attributes for cells and headers */
	uint32_t row_label_fg;
	uint32_t row_label_bg;
	uint32_t col_label_fg;
	uint32_t col_label_bg;
	uint32_t cell_fg;
	uint32_t cell_bg;
	uint32_t hilite_fg;
	uint32_t hilite_bg;
	int cell_rounded;
} mvpw_array_attr_t;

extern mvp_widget_t *mvpw_create_array(mvp_widget_t *parent,
						int x, int y, int w, int h,
						uint32_t bg, uint32_t border_color,
						int border_size);
extern void mvpw_set_array_attr(mvp_widget_t *widget, mvpw_array_attr_t *attr);
extern void mvpw_get_array_attr(mvp_widget_t *widget, mvpw_array_attr_t *attr);
extern void mvpw_set_array_row(mvp_widget_t *widget, int which, char * string,
  mvpw_text_attr_t * attr);
extern void mvpw_set_array_row_bg(mvp_widget_t *widget, int which,
																	uint32_t bg_col);
extern void mvpw_set_array_col(mvp_widget_t *widget, int which, char * string,
  mvpw_text_attr_t * attr);
extern void mvpw_set_array_cell(mvp_widget_t *widget, int x, int y,
	char * string, mvpw_text_attr_t * attr);
extern void mvpw_hilite_array_cell(mvp_widget_t *widget, int x, int y, int hlt);
extern void mvpw_move_array_selection(mvp_widget_t *widget, int direction);
extern void mvpw_reset_array_selection(mvp_widget_t *widget);
extern void mvpw_set_array_scroll(mvp_widget_t *widget,
	 void (*scroll_callback)(mvp_widget_t *widget, int direction));
extern void mvpw_set_array_cell_data(mvp_widget_t *widget, int x, int y,
																		 void * data);
extern void mvpw_set_array_cell_fg(mvp_widget_t *widget, int x, int y,
																		 uint32_t fg);
extern void *
mvpw_get_array_cell_data(mvp_widget_t *widget, int x, int y);

extern void * mvpw_get_array_cur_cell_data(mvp_widget_t *widget);
extern void mvpw_set_array_cell_span(mvp_widget_t *widget,
																			int x, int y, int span);
extern void mvpw_reset_array_cells(mvp_widget_t *widget);
extern void mvpw_array_clear_dirty(mvp_widget_t *widget);
extern void mvpw_set_array_cell_theme(mvp_widget_t *widget, int x, int y,
																			mvpw_array_cell_theme *theme);

/**
 * menu attributes
 */
typedef struct {
	int	 	font;		/**< font id */
	uint32_t 	fg;		/**< foreground color */
	uint32_t 	bg;		/**< background color */
	uint32_t 	hilite_fg;	/**< hilite foreground color */
	uint32_t 	hilite_bg;	/**< hilite background color */
	uint32_t 	title_fg;	/**< title foreground color */
	uint32_t 	title_bg;	/**< title background color */
	uint32_t 	border;		/**< border color */
	uint32_t 	checkbox_fg;	/**< checkbox color */
	int	 	title_justify;	/**< title justification */
	bool	 	checkboxes;	/**< display checkboxes */
	bool	 	rounded;	/**< rounded or square hilite */
	int	 	border_size;	/**< border size in pixels */
	int	 	margin;		/**< margin size in pixels */
	bool	 	utf8;		/**< utf8 text encoding */
} mvpw_menu_attr_t;

/**
 * menu item attributes
 */
typedef struct {
	bool	 	  selectable;	/**< is item selectable? */
	uint32_t 	  fg;		/**< foreground color */
	uint32_t 	  bg;		/**< background color */
	uint32_t 	  checkbox_fg;	/**< checkbox color */
	/** item destroy callback */
	void 		(*destroy)(mvp_widget_t*, char*, void*);
	/** item select callback */
	void 		(*select)(mvp_widget_t*, char*, void*);
	/** item hilite callback */
	void 		(*hilite)(mvp_widget_t*, char*, void*, bool);
} mvpw_menu_item_attr_t;

/**
 * Create a menu widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t *mvpw_create_menu(mvp_widget_t *parent,
				      int x, int y, int w, int h,
				      uint32_t bg, uint32_t border_color,
				      int border_size);

/**
 * Select a menu item based on a text string.
 * \param widget widget handle
 * \param text initial text string to match
 */
extern void mvpw_select_via_text(mvp_widget_t *widget, char text[]);

/**
 * Set the menu attributes.
 * \param widget widget handle
 * \param attr menu attributes
 */	
extern void mvpw_set_menu_attr(mvp_widget_t *widget, mvpw_menu_attr_t *attr);

/**
 * Get the menu attributes.
 * \param widget widget handle
 * \param[out] attr menu attributes
 */	
extern void mvpw_get_menu_attr(mvp_widget_t *widget, mvpw_menu_attr_t *attr);

/**
 * Set the menu title.
 * \param widget widget handle
 * \param title title string
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_menu_title(mvp_widget_t *widget, char *title);

/**
 * Add an entry to a menu.
 * \param widget widget handle
 * \param label string to display in menu
 * \param key menu item key (should be unique)
 * \param item_attr menu item attributes
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_add_menu_item(mvp_widget_t *widget, char *label, void *key,
			      mvpw_menu_item_attr_t *item_attr);

/**
 * Clear the entire menu.
 * \param widget widget handle
 */
extern void mvpw_clear_menu(mvp_widget_t *widget);

/**
 * Delete all menu items with a certain key.
 * \param widget widget handle
 * \param key key to the item that should be deleted
 * \return number of items deleted
 */
extern int mvpw_delete_menu_item(mvp_widget_t *widget, void *key);

/**
 * Get the menu label for a specific key.
 * \param widget widget handle
 * \param key menu item key
 * \return menu item label
 */
extern char* mvpw_get_menu_item(mvp_widget_t *widget, void *key);

/**
 * Check or uncheck a menu item.
 * \param widget widget handle
 * \param key menu item key
 * \param checked 0 to uncheck, 1 to check
 */
extern void mvpw_check_menu_item(mvp_widget_t *widget, void *key, bool checked);

/**
 * Hilite a specific menu item.
 * \param widget widget handle
 * \param key menu item key
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_menu_hilite_item(mvp_widget_t *widget, void *key);

/**
 * Return the item attributes for a menu item.
 * \param widget widget handle
 * \param key menu item key
 * \param[out] item_attr item attributes
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_menu_get_item_attr(mvp_widget_t *widget, void *key,
				   mvpw_menu_item_attr_t *item_attr);

/**
 * Set the item attributes for a menu item.
 * \param widget widget handle
 * \param key menu item key
 * \param item_attr item attributes
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_menu_set_item_attr(mvp_widget_t *widget, void *key,
				   mvpw_menu_item_attr_t *item_attr);

/**
 * Change the label for a menu item.
 * \param widget widget handle
 * \param key menu item key
 * \param label new label text
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_menu_change_item(mvp_widget_t *widget, void *key, char *label);

/**
 * Return the menu item key of the currently hilited item.
 * \param widget widget handle
 * \return menu item key
 */
extern void* mvpw_menu_get_hilite(mvp_widget_t *widget);

/**
 * image information
 */
typedef struct {
	int 		width;		/**< image width */
	int 		height;		/**< image height */
} mvpw_image_info_t;

/**
 * Create an image widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t* mvpw_create_image(mvp_widget_t *parent,
				       int x, int y, int w, int h,
				       uint32_t bg, uint32_t border_color,
				       int border_size);

/**
 * Draw an image from a file into an image widget.
 * \param widget widget handle
 * \param file filename of image
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_image(mvp_widget_t *widget, char *file);

/**
 * Get image info from an image file.
 * \param file image filename
 * \param[out] data image information
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_get_image_info(char *file, mvpw_image_info_t *data);

/**
 * Destroy the image contents of an image widget.
 * \param widget widget handle
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_image_destroy(mvp_widget_t *widget);

/**
 * Load a jpeg image from a file.
 * \param widget widget handle
 * \param file filename of image
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_load_image_jpeg(mvp_widget_t *widget, char *file);

/**
 * Show a loaded jpeg image.
 * \param widget widget handle
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_show_image_jpeg(mvp_widget_t *widget);

/**
 * graph widget attributes
 */
typedef struct {
	int 		min;		/**< minimum graph value */
	int 		max;		/**< maximum graph value */
	uint32_t 	fg;		/**< foreground color */
	uint32_t 	bg;		/**< background color */
	uint32_t 	border;		/**< border color */
	int 		border_size;	/**< border size in pixels */
	bool 		gradient;	/**< use gradient? */
	uint32_t 	left;		/**< left gradient color */
	uint32_t 	right;		/**< right gradient color */
} mvpw_graph_attr_t;

/**
 * Create a graph widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t* mvpw_create_graph(mvp_widget_t *parent,
				       int x, int y, int w, int h,
				       uint32_t bg, uint32_t border_color,
				       int border_size);

/**
 * Set graph attributes.
 * \param widget widget handle
 * \param attr graph attributes
 */
extern void mvpw_set_graph_attr(mvp_widget_t *widget, mvpw_graph_attr_t *attr);

/**
 * Set current graph value
 * \param widget widget handle
 * \param value widget value (between min and max)
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_graph_current(mvp_widget_t *widget, int value);

/**
 * Increment the graph value.
 * \param widget widget handle
 * \param value amount to increment by
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_graph_incr(mvp_widget_t *widget, int value);

/**
 * Create a checkbox widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t* mvpw_create_checkbox(mvp_widget_t *parent,
					  int x, int y, int w, int h,
					  uint32_t bg, uint32_t border_color,
					  int border_size);

/**
 * Set the checkbox color.
 * \param widget widget handle
 * \param fg color
 */
extern void mvpw_set_checkbox_fg(mvp_widget_t *widget, uint32_t fg);

/**
 * Check or uncheck the checkbox.
 * \param widget widget handle
 * \param checked 0 to uncheck, 1 to check
 */
extern void mvpw_set_checkbox(mvp_widget_t *widget, bool checked);

/**
 * bitmap attributes
 */
typedef struct {
	char 		*image;		/**< bitmap image */
} mvpw_bitmap_attr_t;

/**
 * Create a bitmap widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t* mvpw_create_bitmap(mvp_widget_t *parent,
					int x, int y, int w, int h,
					uint32_t bg, uint32_t border_color,
					int border_size);

/**
 * Set the bitmap image.
 * \param widget handle
 * \param bitmap image bitmap
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_bitmap(mvp_widget_t *widget, mvpw_bitmap_attr_t *bitmap);

/**
 * dialog attributes
 */
typedef struct {
	uint32_t 	fg;		/**< foreground color */
	uint32_t 	bg;		/**< background color */
	uint32_t 	title_fg;	/**< title foreground color */
	uint32_t 	title_bg;	/**< title background color */
	uint32_t 	border;		/**< border color */
	int 		border_size;	/**< border size in pixels */
	char 		*image;		/**< image filename */
	bool 		modal;		/**< modal or not */
	int	 	font;		/**< font id */
	int	 	margin;		/**< margin in pixels */
	bool	 	justify_title;	/**< title justification */
	bool	 	justify_body;	/**< body justification */
	bool	 	utf8;		/**< utf8 character encoding */
} mvpw_dialog_attr_t;

/**
 * Create a dialog widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \return widget handle
 */
extern mvp_widget_t* mvpw_create_dialog(mvp_widget_t *parent,
					int x, int y, int w, int h,
					uint32_t bg, uint32_t border_color,
					int border_size);

/**
 * Set the dialog attributes.
 * \param widget widget handle
 * \param attr dialog attributes
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_dialog_attr(mvp_widget_t *widget,
				mvpw_dialog_attr_t *attr);

/**
 * Set the dialog title text.
 * \param widget widget handle
 * \param title title string
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_dialog_title(mvp_widget_t *widget, char *title);

/**
 * Set the dialog body text.
 * \param widget widget handle
 * \param text text string
 * \retval 0 success
 * \retval -1 error
 */
extern int mvpw_set_dialog_text(mvp_widget_t *widget, char *text);

/*
 * surface widget
 */
typedef struct {
	int 		pixtype;
	int		wid;
	uint32_t	foreground;
	int 		fd;
} mvpw_surface_attr_t;

/**
 * Create a surface widget.
 * \param parent parent widget (NULL for root window)
 * \param x horizontal coordinate
 * \param y vertical coordinate
 * \param w width
 * \param h height
 * \param bg background color
 * \param border_color border color
 * \param border_size border width in pixels
 * \param new_pixmap create new pixmap?
 * \return widget handle
 */
extern mvp_widget_t* mvpw_create_surface(mvp_widget_t *parent,
					int x, int y, int w, int h,
					uint32_t bg, uint32_t border_color,
					int border_size, int new_pixmap);

extern int mvpw_get_surface_attr(mvp_widget_t *widget, mvpw_surface_attr_t *surface);
extern int mvpw_set_surface_attr(mvp_widget_t *widget, mvpw_surface_attr_t *surface);
extern int mvpw_set_surface(mvp_widget_t *widget, char *image, int x, int y, int width, int height);
extern int mvpw_copy_area(mvp_widget_t *widget, int x, int y, int srcwid, int srcx, int srcy, int width, int height);
extern int mvpw_fill_rect(mvp_widget_t *widget, int x, int y, int w, int h, uint32_t* color);

/*
 * common colors
 */
#define MVPW_TRANSPARENT	MVPW_RGBA(0,0,0,0)
#define MVPW_RED		MVPW_RGBA(255,0,0,255)
#define MVPW_DARK_RED MVPW_RGBA(170, 15, 15, 255)
#define MVPW_GREEN		MVPW_RGBA(0,255,0,255)
#define MVPW_BLUE		MVPW_RGBA(0,0,255,255)
#define MVPW_MIDNIGHTBLUE	MVPW_RGBA(25,25,112,255)
#define MVPW_CYAN		MVPW_RGBA(0,255,255,255)
#define MVPW_YELLOW		MVPW_RGBA(255,255,0,255)
#define MVPW_WHITE		MVPW_RGBA(255,255,255,255)
#define MVPW_BLACK		MVPW_RGBA(0,0,0,255)
#define MVPW_ORANGE		MVPW_RGBA(255,110,0,255)
#define MVPW_DARK_ORANGE	MVPW_RGBA(255,190,0,255)
#define MVPW_PURPLE		MVPW_RGBA(255,0,255,255)
#define MVPW_LIGHTGREY		MVPW_RGBA(128,128,128,255)
#define MVPW_ALMOSTWHITEGREY   MVPW_RGBA(220,220,220,255)
#define MVPW_DARKGREY		MVPW_RGBA(96,96,96,255)
#define MVPW_DARKGREY2		MVPW_RGBA(64,64,64,255)

#define MVPW_RGBA(r,g,b,a)	((a<<24) | (b<<16) | (g<<8) | r)

/**
 * Convert a color from its 4 parts to a single value.
 * \param r red
 * \param g green
 * \param b blue
 * \param a alpha channel
 * \return color
 */
static inline unsigned long
mvpw_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return (a<<24) | (b<<16) | (g<<8) | r;
}

/**
 * Convert a color from a single value to its 4 parts.
 * \param c color
 * \param[out] r red
 * \param[out] g green
 * \param[out] b blue
 * \param[out] a alpha channel
 */
static inline void
mvpw_get_rgba(unsigned long c, unsigned char *r, unsigned char *g,
	      unsigned char *b, unsigned char *a)
{
	*a = (c >> 24) & 0xff;
	*b = (c >> 16) & 0xff;
	*g = (c >> 8) & 0xff;
	*r = (c >> 0) & 0xff;
}

/**
 * Change the alpha channel for a color.
 * \param c color
 * \param a alpha channel
 * \return new color
 */
#define mvpw_color_alpha(c,a)	((a << 24) | (c & 0x00ffffff))

#ifdef MVPMC_HOST
#define MVPW_KEY_UP	2
#define MVPW_KEY_DOWN	3
#define MVPW_KEY_LEFT	0
#define MVPW_KEY_RIGHT	1
#define MVPW_KEY_OK	'\r'
#define MVPW_KEY_ZERO	'0'
#define MVPW_KEY_ONE	'1'
#define MVPW_KEY_TWO	'2'
#define MVPW_KEY_THREE	'3'
#define MVPW_KEY_FOUR	'4'
#define MVPW_KEY_FIVE	'5'
#define MVPW_KEY_SIX	'6'
#define MVPW_KEY_SEVEN	'7'
#define MVPW_KEY_EIGHT	'8'
#define MVPW_KEY_NINE	'9'
#define MVPW_KEY_POWER	'P'
#define MVPW_KEY_GO	'G'
#define MVPW_KEY_EXIT	27
#define MVPW_KEY_MENU	'M'
#define MVPW_KEY_CHAN_UP	8
#define MVPW_KEY_CHAN_DOWN	9
#define MVPW_KEY_VOL_UP	'^'
#define MVPW_KEY_VOL_DOWN	'v'
#define MVPW_KEY_BLUE	'b'
#define MVPW_KEY_GREEN	'g'
#define MVPW_KEY_YELLOW 'y'
#define MVPW_KEY_RED    'r'
#define MVPW_KEY_MUTE	'Q'
#define MVPW_KEY_BLANK	' '
#define MVPW_KEY_FULL	'L'
#define MVPW_KEY_PLAY	'#'
#define MVPW_KEY_RECORD	'*'
#define MVPW_KEY_PAUSE	','
#define MVPW_KEY_STOP	'.'
#define MVPW_KEY_REWIND	'{'
#define MVPW_KEY_FFWD	'}'
#define MVPW_KEY_REPLAY	'('
#define MVPW_KEY_SKIP	')'
#define	MVPW_KEY_PREV_CHAN	'C'
#define	MVPW_KEY_GUIDE		'U'
#define	MVPW_KEY_TV		'T'
#else
#define	MVPW_KEY_ZERO		 0
#define	MVPW_KEY_ONE		 1
#define	MVPW_KEY_TWO		 2
#define	MVPW_KEY_THREE		 3
#define	MVPW_KEY_FOUR		 4
#define	MVPW_KEY_FIVE		 5
#define	MVPW_KEY_SIX		 6
#define	MVPW_KEY_SEVEN		 7
#define	MVPW_KEY_EIGHT		 8
#define	MVPW_KEY_NINE		 9
#define	MVPW_KEY_RED		11
#define	MVPW_KEY_BLANK		12
#define	MVPW_KEY_MENU		13
#define	MVPW_KEY_MUTE		15
#define	MVPW_KEY_RIGHT		16
#define	MVPW_KEY_LEFT		17
#define	MVPW_KEY_VIDEOS		24
#define	MVPW_KEY_MUSIC		25
#define	MVPW_KEY_PICTURES	26
#define	MVPW_KEY_GUIDE		27
#define	MVPW_KEY_TV		28
#define	MVPW_KEY_SKIP		30
#define	MVPW_KEY_EXIT		31
#define	MVPW_KEY_REPLAY		36
#define	MVPW_KEY_OK		37
#define	MVPW_KEY_BLUE		41
#define	MVPW_KEY_GREEN		46
#define	MVPW_KEY_PAUSE		48
#define	MVPW_KEY_REWIND		50
#define	MVPW_KEY_FFWD		52
#define	MVPW_KEY_PLAY		53
#define	MVPW_KEY_STOP		54
#define	MVPW_KEY_RECORD		55
#define	MVPW_KEY_YELLOW		56
#define	MVPW_KEY_GO		59
#define	MVPW_KEY_FULL		60
#define	MVPW_KEY_POWER		61

#define	MVPW_KEY_UP		32
#define	MVPW_KEY_DOWN		33
#define	MVPW_KEY_CHAN_UP	20
#define	MVPW_KEY_CHAN_DOWN	21
#define	MVPW_KEY_VOL_UP		23
#define	MVPW_KEY_VOL_DOWN	22

#define	MVPW_KEY_ASTERISK	10
#define	MVPW_KEY_POUND		14
#define	MVPW_KEY_PREV_CHAN	18

#define	MVPW_KEY_RADIO		29
#endif /* MVPMC_HOST */

#endif /* MVP_WIDGET_H */
