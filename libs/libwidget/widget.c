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
#include "utf8.h"

#define HASH_BINS	31

static struct widget_list {
	struct widget_list *next;
	mvp_widget_t *widget;
} *hash[HASH_BINS];

static volatile int widget_count = 0;

static mvp_widget_t *root;

static volatile mvp_widget_t *modal_focus = NULL;
static volatile mvp_widget_t *screensaver_widget = NULL;
static void (*screensaver_callback)(mvp_widget_t*, int) = NULL;

static void (*idle)(void);
static void (*keystroke_callback)(char);
static void (*fdinput_callback)(void);

static mvp_widget_t*
find_widget(GR_WINDOW_ID wid)
{
	struct widget_list *ptr;
	int h = wid % HASH_BINS;

	ptr = hash[h];

	while (ptr && ptr->widget && (ptr->widget->wid != wid))
		ptr = ptr->next;

	if (ptr && ptr->widget && (ptr->widget->wid == wid))
		return ptr->widget;

	return NULL;
}

static mvp_widget_t*
find_widget_fd(GR_WINDOW_ID wid, int fd)
{
	struct widget_list *ptr;
	int h = wid % HASH_BINS;

	ptr = hash[h];

	while (ptr && ptr->widget && (ptr->widget->type != MVPW_SURFACE))
		ptr = ptr->next;
	
	if (ptr && ptr->widget && (ptr->widget->data.surface.fd == fd))
		return ptr->widget;

	return NULL;
}

static int
add_widget(mvp_widget_t *widget)
{
	struct widget_list *ptr, *item;
	int h = widget->wid % HASH_BINS;

	ptr = hash[h];

	while (ptr && ptr->next)
		ptr = ptr->next;

	if ((item=(struct widget_list*)malloc(sizeof(*item))) == NULL)
		return -1;

	item->next = NULL;
	item->widget = widget;

	if (ptr)
		ptr->next = item;
	else
		hash[h] = item;

	widget_count++;

	return 0;
}

static int
remove_widget(mvp_widget_t *widget)
{
	struct widget_list *ptr, *prev;
	int h = widget->wid % HASH_BINS;

	ptr = hash[h];

	prev = NULL;
	while (ptr) {
		if (ptr->widget == widget) {
			if (prev)
				prev->next = ptr->next;
			else
				hash[h] = ptr->next;
			free(ptr);
			widget_count--;
			return 0;
		}
		prev = ptr;
		ptr = ptr->next;
	}

	return -1;
}

static void
raise_widget(mvp_widget_t *widget, mvp_widget_t *top)
{
	int wid;

	if (widget) {
		if (top == NULL) {
			wid = GrGetFocus();
			top = find_widget(wid);
		}

		if (top == widget)
			return;

		if (widget->below)
			widget->below->above = widget->above;
		if (widget->above)
			widget->above->below = widget->below;
		widget->below = top;
		widget->above = NULL;

		if (top) {
			top->above = widget;
		}

		GrSetFocus(widget->wid);
	}
}

static void
lower_widget(mvp_widget_t *widget)
{
	if (widget && (widget != modal_focus)) {
		if (widget->above)
			widget->above->below = widget->below;
		if (widget->below)
			widget->below->above = widget->above;

		widget->below = NULL;
		widget->above = NULL;
	}
}

mvp_widget_t*
mvpw_create(mvp_widget_t *parent,
	    GR_COORD x, GR_COORD y,
	    unsigned int width, unsigned int height,
	    GR_COLOR bg, GR_COLOR border_color, int border_size)
{
	mvp_widget_t *widget;
	GR_WINDOW_ID wid, pwid;

	if (parent)
		pwid = parent->wid;
	else
		pwid = GR_ROOT_WINDOW_ID;

	wid = GrNewWindow(pwid, x, y, width, height,
			  border_size, bg, border_color);

	if (wid == 0)
		return NULL;

	if ((widget=(mvp_widget_t*)malloc(sizeof(*widget))) == NULL)
		return NULL;
	memset(widget, 0, sizeof(*widget));

	widget->wid = wid;
	widget->parent = parent;
	widget->x = x;
	widget->y = y;
	widget->width = width;
	widget->height = height;
	widget->bg = bg;
	widget->border_color = border_color;
	widget->border_size = border_size;

	if (add_widget(widget) < 0)
		goto err;

	widget->event_mask = GR_EVENT_MASK_EXPOSURE;

	GrSelectEvents(wid, widget->event_mask);

	return widget;

 err:
	if (widget)
		mvpw_destroy(widget);

	return NULL;
}

void
mvpw_destroy(mvp_widget_t *widget)
{
	mvpw_hide(widget);

	if (widget->callback_destroy)
		widget->callback_destroy(widget);
	if (widget->destroy)
		widget->destroy(widget);

	remove_widget(widget);

	GrDestroyWindow(widget->wid);

	free(widget);
}

void
mvpw_focus(mvp_widget_t *widget)
{
	if ((mvpw_get_focus() == widget) &&
	    !((widget->type == MVPW_DIALOG) &&
	      (widget->data.dialog.modal == 1)))
		return;

	if (widget) {
		if ((widget->type == MVPW_DIALOG) &&
		    (widget->data.dialog.modal == 1)) {
			raise_widget(widget, (mvp_widget_t*)modal_focus);
			modal_focus = widget;
		} else {
			raise_widget(widget, NULL);
			if ((widget != screensaver_widget) && (modal_focus)) {
				raise_widget((mvp_widget_t*)modal_focus, widget);
			}
		}
	}
}

void
mvpw_show(mvp_widget_t *widget)
{
	mvp_widget_t *top;

	if (widget) {
		if ((widget->type == MVPW_DIALOG) &&
		    (widget->data.dialog.modal == 1)) {
			top = mvpw_get_focus();
			GrMapWindow(widget->wid);
			mvpw_focus(widget);
			raise_widget(widget, top);
		} else {
			GrMapWindow(widget->wid);
			if(widget->show) (*widget->show)(widget, 1);
		}
	}
}

void
mvpw_hide(mvp_widget_t *widget)
{
	mvp_widget_t *top;

	if (widget) {
		if (widget == modal_focus) {
			if ((widget->below->type == MVPW_DIALOG) &&
			    (widget->below->data.dialog.modal == 1)) {
				modal_focus = widget->below;
			} else {
				modal_focus = NULL;
			}
		}

		GrUnmapWindow(widget->wid);
		if(widget->show) (*widget->show)(widget, 0);
		if ((top=widget->below))
			widget->below->above = widget->above;
		if (widget->above) {
			top = widget->above;
			widget->above->below = widget->below;
		}

		if (modal_focus) {
			GrSetFocus(modal_focus->wid);
		} else if ((widget->above == NULL) && top) {
			GrSetFocus(top->wid);
		}

		widget->below = NULL;
		widget->above = NULL;
	}
}

void
mvpw_expose(const mvp_widget_t *widget)
{
	if (widget) {
		if (widget->type == MVPW_SURFACE) {
			if(widget->wid != widget->data.surface.wid) {
				GrClearArea(widget->wid, 0, 0, 0, 0, 1);
			}
		} else {
			GrClearArea(widget->wid, 0, 0, 0, 0, 1);
		}
	}
}

int
mvpw_load_font(char *file)
{
	return GrCreateFont((unsigned char*)file, 0, NULL);
}

void
mvpw_resize(const mvp_widget_t *widget, int w, int h)
{
	GrResizeWindow(widget->wid, w, h);
}

static mvp_widget_t**
attach_list(mvp_widget_t *widget, mvp_widget_t **list, int *size, int *count)
{
	int i;

	if (widget == NULL)
		return list;

	for (i=0; i<*count; i++)
		if (list[i] == widget)
			return list;

	if (*size == *count) {
		if ((list=realloc(list, sizeof(*list)*(*size+128))) == NULL)
			return NULL;
		*size += 128;
	}

	list[(*count)++] = widget;

	for (i=0; i<4; i++) {
		list = attach_list(widget->attach[i], list, size, count);

		if (list == NULL)
			return NULL;
	}

	return list;
}

int
mvpw_attach(mvp_widget_t *w1, mvp_widget_t *w2, int direction)
{
	int ret = 0, i, size, count;
	GR_COORD x = 0, y = 0;
	mvp_widget_t **list, **l;

	switch (direction) {
	case MVPW_DIR_UP:
	case MVPW_DIR_DOWN:
	case MVPW_DIR_LEFT:
	case MVPW_DIR_RIGHT:
		break;
	default:
		return -1;
		break;
	}

	if ((list=malloc(sizeof(*list)*128)) == NULL)
		return -1;

	size = 128;
	count = 0;
	l = list;
	if ((list=attach_list(w2, list, &size, &count)) == NULL) {
		free(l);
		return -1;
	}

	for (i=0; i<count; i++)
		if (list[i] == w1) {
			free(list);
			return -1;
		}

	switch (direction) {
	case MVPW_DIR_UP:
		x = w1->x;
		y = w1->y - w2->height;
		break;
	case MVPW_DIR_DOWN:
		x = w1->x;
		y = w1->y + w1->height;
		break;
	case MVPW_DIR_LEFT:
		x = w1->x - w1->width;
		y = w1->y;
		break;
	case MVPW_DIR_RIGHT:
		x = w1->x + w1->width;
		y = w1->y;
		break;
	}

	x = x - w2->x;
	y = y - w2->y;
	for (i=0; i<count; i++) {
		list[i]->x += x;
		list[i]->y += y;
		GrMoveWindow(list[i]->wid, list[i]->x, list[i]->y);
	}

	w1->attach[direction] = w2;
	switch (direction) {
	case MVPW_DIR_UP:
		w2->attach[MVPW_DIR_DOWN] = w1;
		break;
	case MVPW_DIR_DOWN:
		w2->attach[MVPW_DIR_UP] = w1;
		break;
	case MVPW_DIR_LEFT:
		w2->attach[MVPW_DIR_RIGHT] = w1;
		break;
	case MVPW_DIR_RIGHT:
		w2->attach[MVPW_DIR_LEFT] = w1;
		break;
	}

	free(list);

	return ret;
}

void
mvpw_unattach(mvp_widget_t *widget, int dir)
{
	switch (dir) {
	case MVPW_DIR_UP:
		if (widget->attach[dir])
			widget->attach[dir]->attach[MVPW_DIR_DOWN] = NULL;
		break;
	case MVPW_DIR_DOWN:
		if (widget->attach[dir])
			widget->attach[dir]->attach[MVPW_DIR_UP] = NULL;
		break;
	case MVPW_DIR_LEFT:
		if (widget->attach[dir])
			widget->attach[dir]->attach[MVPW_DIR_RIGHT] = NULL;
		break;
	case MVPW_DIR_RIGHT:
		if (widget->attach[dir])
			widget->attach[dir]->attach[MVPW_DIR_LEFT] = NULL;
		break;
	default:
		return;
		break;
	}

	widget->attach[dir] = NULL;
}

void
mvpw_moveto(mvp_widget_t *widget, int x, int y)
{
	int dx, dy;

	dx = x - widget->x;
	dy = y - widget->y;

	mvpw_move(widget, dx, dy);
}

void
mvpw_move(mvp_widget_t *widget, int x, int y)
{
	int i, size, count;
	mvp_widget_t **list;

	if ((list=malloc(sizeof(*list)*128)) == NULL)
		return;

	size = 128;
	count = 0;
	if ((list=attach_list(widget, list, &size, &count)) == NULL)
		return;

	for (i=0; i<count; i++) {
		list[i]->x += x;
		list[i]->y += y;
		GrMoveWindow(list[i]->wid, list[i]->x, list[i]->y);
#if 1
		mvpw_expose(list[i]);
#endif
	}

	if (count > 0)
		mvpw_expose(root);

	free(list);
}

int
mvpw_font_height(int font, int utf8)
{
	GR_FONT_INFO finfo;

	GrGetFontInfo(font, &finfo);

	return finfo.height;
}

static void accumulate_width(void *closure, void *closure2, int c) {
	int *w = (int *)closure2;

	GR_FONT_INFO *finfo = (GR_FONT_INFO *)closure;

	if (c < 256)
		*w += finfo->widths[c];
	else
		*w += finfo->maxwidth;
}

int
mvpw_font_width(int font, char *str, int utf8)
{
	GR_FONT_INFO finfo;
	int i, w = 0;

	GrGetFontInfo(font, &finfo);

	if (utf8) {
		utf8_for_each2(str, &accumulate_width,
			       (void *)&finfo, (void *)&w);
	} else {
		for (i=0; i<strlen(str); i++)
			w += finfo.widths[(int)str[i]];
	}

	return w;
}

void
mvpw_set_expose_callback(mvp_widget_t *widget, void (*callback)(mvp_widget_t*))
{
	if (widget == NULL)
		return;

	widget->callback_expose = callback;
}

static void
exposure(GR_EVENT_EXPOSURE *exposure)
{
	mvp_widget_t *widget;

	if ((widget=find_widget(exposure->wid)) == NULL) {
		printf("expose on unknown wid %d\n", exposure->wid);
		return;
	}

	if (widget->callback_expose)
		widget->callback_expose(widget);
	if (widget->expose)
		widget->expose(widget);
}

static void
keystroke(GR_EVENT_KEYSTROKE *key)
{
	mvp_widget_t *widget;

	if (keystroke_callback)
		keystroke_callback(key->ch);

	if ((widget=find_widget(key->wid)) == NULL)
		return;

	if (widget->callback_key)
		widget->callback_key(widget, key->ch);
	if (widget->key)
		widget->key(widget, key->ch);
}

static void
fdinput(GR_EVENT_FDINPUT *fdinput)
{
	mvp_widget_t *widget;
	GR_WINDOW_ID wid;	

	if (fdinput_callback)
		fdinput_callback();

	wid = GrGetFocus();
	if ((widget=find_widget_fd(wid, fdinput->fd)) == NULL)
		return;

	if (widget->callback_fdinput)
		widget->callback_fdinput(widget, fdinput->fd);
	if (widget->fdinput)
		widget->fdinput(widget, fdinput->fd);
}

static void
timer(GR_EVENT_TIMER *timer)
{
	mvp_widget_t *widget;

	if ((widget=find_widget(timer->wid)) == NULL)
		return;

	if (widget->tid != timer->tid)
		return;

	if (widget->callback_timer)
		widget->callback_timer(widget);
	if (widget->timer)
		widget->timer(widget);
}

static void
screensaver(GR_EVENT_SCREENSAVER *event)
{
	if (screensaver_callback) {
		screensaver_callback((mvp_widget_t*)screensaver_widget,
				     event->activate);
	}
}

void
mvpw_set_bg(mvp_widget_t *widget, uint32_t bg)
{
	widget->bg = bg;

	GrSetWindowBackgroundColor(widget->wid, bg);
}

uint32_t
mvpw_get_bg(const mvp_widget_t *widget)
{
	return widget->bg;
}

void
mvpw_set_timer(mvp_widget_t *widget, void (*callback)(mvp_widget_t*),
	       uint32_t timeout)
{
	if (widget->callback_timer) {
		widget->event_mask &= ~GR_EVENT_MASK_TIMER;
		GrSelectEvents(widget->wid, widget->event_mask);
		GrDestroyTimer(widget->tid);
	}

	widget->callback_timer = callback;

	if (callback) {
		widget->event_mask |= GR_EVENT_MASK_TIMER;
		GrSelectEvents(widget->wid, widget->event_mask);
		widget->tid = GrCreateTimer(widget->wid, timeout);
	}

}

int
mvpw_event_flush(void)
{
	GR_EVENT event;

	if (widget_count == 0)
		return -1;

	while (widget_count > 0) {
		GrCheckNextEvent(&event);
		switch (event.type) {
		case GR_EVENT_TYPE_EXPOSURE:
			exposure(&event.exposure);
			break;
		case GR_EVENT_TYPE_KEY_DOWN:
			keystroke(&event.keystroke);
			break;
		case GR_EVENT_TYPE_TIMER:
			timer(&event.timer);
			break;
		case GR_EVENT_TYPE_SCREENSAVER:
			screensaver(&event.screensaver);
			break;
		case GR_EVENT_TYPE_FDINPUT:
			fdinput(&event.fdinput);
			break;
		case GR_EVENT_TYPE_NONE:
			return 0;
			break;
		}
	}

	return 0;
}

int
mvpw_event_loop(void)
{
	GR_EVENT event;

	if (widget_count == 0)
		return -1;

	while (widget_count > 0) {
		GrCheckNextEvent(&event);
		switch (event.type) {
		case GR_EVENT_TYPE_EXPOSURE:
			exposure(&event.exposure);
			break;
		case GR_EVENT_TYPE_KEY_DOWN:
			keystroke(&event.keystroke);
			break;
		case GR_EVENT_TYPE_TIMER:
			timer(&event.timer);
			break;
		case GR_EVENT_TYPE_SCREENSAVER:
			screensaver(&event.screensaver);
			break;
		case GR_EVENT_TYPE_FDINPUT:
			fdinput(&event.fdinput);
			break;
		case GR_EVENT_TYPE_NONE:
			if (idle)
				idle();
			else
				usleep(1000);
			break;
		}
	}

	return 0;
}

int
mvpw_init(void)
{
	GR_SCREEN_INFO si;

	if (GrOpen() < 0)
		return -1;

	GrGetScreenInfo(&si);

	root = malloc(sizeof(*root));
	memset(root, 0, sizeof(*root));

	root->type = MVPW_ROOT;
	root->wid = GR_ROOT_WINDOW_ID;
	root->width = si.cols;
	root->height = si.rows;
	add_widget(root);

	GrSetWindowBackgroundColor(GR_ROOT_WINDOW_ID, 0);

	root->event_mask = GR_EVENT_MASK_KEY_DOWN | GR_EVENT_MASK_TIMER |
		GR_EVENT_MASK_SCREENSAVER | GR_EVENT_MASK_FDINPUT;
	GrSelectEvents(GR_ROOT_WINDOW_ID, root->event_mask);

	return 0;
}

void
mvpw_get_widget_info(mvp_widget_t *widget, mvpw_widget_info_t *info)
{
	info->x = widget->x;
	info->y = widget->y;
	info->w = widget->width;
	info->h = widget->height;
}

void
mvpw_get_screen_info(mvpw_screen_info_t *info)
{
	GR_SCREEN_INFO si;

	GrGetScreenInfo(&si);

	info->cols = si.cols;
	info->rows = si.rows;
	info->bpp  = si.bpp;
	info->pixtype = si.pixtype;
}

mvp_widget_t*
mvpw_get_root(void)
{
	return root;
}

void
mvpw_set_key(mvp_widget_t *widget, void (*callback)(mvp_widget_t*, char))
{
	widget->event_mask |= GR_EVENT_MASK_KEY_DOWN;
	GrSelectEvents(widget->wid, widget->event_mask);

	widget->callback_key = callback;
}

void
mvpw_set_idle(void (*callback)(void))
{
	idle = callback;
}

void
mvpw_raise(mvp_widget_t *widget)
{
	if (widget) {
		if (modal_focus) {
			if ((widget == modal_focus) ||
			    (widget == screensaver_widget)) {
				GrRaiseWindow(widget->wid);
			}
		} else {
			GrRaiseWindow(widget->wid);
		}
	}
}

void
mvpw_lower(mvp_widget_t *widget)
{
	if (widget) {
		lower_widget(widget);
		GrLowerWindow(widget->wid);
	}
}

int
mvpw_visible(const mvp_widget_t *widget)
{
	GR_WINDOW_INFO info;

	if (widget == NULL)
		return 0;

	GrGetWindowInfo(widget->wid, &info);

	if (info.realized)
		return 1;
	else
		return 0;
}

int
mvpw_keystroke_callback(void (*callback)(char))
{
	keystroke_callback = callback;

	return 0;
}

mvp_widget_t*
mvpw_get_focus(void)
{
	int wid;

	wid = GrGetFocus();

	return find_widget(wid);
}

int
mvpw_set_screensaver(mvp_widget_t *widget, int seconds,
	void (*callback)(mvp_widget_t*, int))
{
	if (widget) {
		GrSetScreenSaverTimeout(seconds);
	} else {
		GrSetScreenSaverTimeout(0);
	}

	screensaver_widget = widget;
	screensaver_callback = callback;

	return 0;
}

void
mvpw_set_fdinput(mvp_widget_t *widget, void (*callback)(mvp_widget_t*, int))
{
	widget->event_mask |= GR_EVENT_MASK_FDINPUT;
	GrSelectEvents(widget->wid, widget->event_mask);

	widget->callback_fdinput = callback;
}	

int
mvpw_fdinput_callback(void (*callback)(void))
{
	fdinput_callback = callback;

	return 0;
}

void
mvpw_reparent(mvp_widget_t *child, mvp_widget_t *parent)
{
	if (parent == NULL)
		parent = root;

	GrReparentWindow(child->wid, parent->wid, 0, 0);
}

int
mvpw_read_area(mvp_widget_t *widget, int x, int y, int w, int h,
	       unsigned long *pixels)
{
	if ((widget == NULL) || (pixels == NULL))
		return -1;
	if ((x < 0) || (x >= widget->width))
		return -1;
	if ((y < 0) || (y >= widget->height))
		return -1;
	if ((w < 0) || (w > widget->width))
		return -1;
	if ((h < 0) || (h > widget->height))
		return -1;

	GrReadArea(widget->wid, x, y, w, h, pixels);

	return 0;
}
