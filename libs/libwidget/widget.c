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

#include "mvp_widget.h"
#include "widget.h"

#define HASH_BINS	31

static struct widget_list {
	struct widget_list *next;
	mvp_widget_t *widget;
} *hash[HASH_BINS];

static volatile int widget_count = 0;

static mvp_widget_t *root;

static void (*idle)(void);

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
	if (widget->callback_destroy)
		widget->callback_destroy(widget);
	if (widget->destroy)
		widget->destroy(widget);

	remove_widget(widget);

	free(widget);
}

void
mvpw_focus(mvp_widget_t *widget)
{
	GrSetFocus(widget->wid);
}

void
mvpw_show(mvp_widget_t *widget)
{
	GrMapWindow(widget->wid);
}

void
mvpw_hide(mvp_widget_t *widget)
{
	GrUnmapWindow(widget->wid);
}

void
mvpw_expose(mvp_widget_t *widget)
{
	GrClearArea(widget->wid, 0, 0, 0, 0, 1);
}

int
mvpw_load_font(char *file)
{
	return GrCreateFont(file, 0, NULL);
}

void
mvpw_resize(mvp_widget_t *widget, int w, int h)
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
		if ((list=realloc(list, sizeof(*list)*(*size+128))) == NULL) {
			if (list)
				free(list);
			return NULL;
		}
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
	mvp_widget_t **list;

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
	if ((list=attach_list(w2, list, &size, &count)) == NULL)
		return -1;

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
		y = w2->y;
		break;
	case MVPW_DIR_RIGHT:
		x = w1->x + w1->width;
		y = w2->y;
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
mvpw_font_height(int font)
{
	GR_FONT_INFO finfo;

	GrGetFontInfo(font, &finfo);

	return finfo.height;
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

	if ((widget=find_widget(key->wid)) == NULL)
		return;

	if (widget->callback_key)
		widget->callback_key(widget, key->ch);
	if (widget->key)
		widget->key(widget, key->ch);
}

static void
timer(GR_EVENT_TIMER *timer)
{
	mvp_widget_t *widget;

	if ((widget=find_widget(timer->wid)) == NULL)
		return;

	if (widget->callback_timer)
		widget->callback_timer(widget);
	if (widget->timer)
		widget->timer(widget);
}

void
mvpw_set_bg(mvp_widget_t *widget, uint32_t bg)
{
	widget->bg = bg;

	GrSetWindowBackgroundColor(widget->wid, bg);
}

uint32_t
mvpw_get_bg(mvp_widget_t *widget)
{
	return widget->bg;
}

void
mvpw_set_timer(mvp_widget_t *widget, void (*callback)(mvp_widget_t*),
	       uint32_t timeout)
{
	widget->callback_timer = callback;

	widget->event_mask |= GR_EVENT_MASK_TIMER;
	GrSelectEvents(widget->wid, widget->event_mask);

	GrCreateTimer(widget->wid, timeout);
}

int
mvpw_event_loop(void)
{
	GR_EVENT event;

	if (widget_count == 0)
		return -1;

	while (widget_count > 0) {
		if (idle)
			GrCheckNextEvent(&event);
		else
			GrGetNextEvent(&event);
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
		case GR_EVENT_TYPE_NONE:
			if (idle)
				idle();
			break;
		}
	}

	return 0;
}

int
mvpw_init(void)
{
	if (GrOpen() < 0)
		return -1;

	root = malloc(sizeof(*root));
	memset(root, 0, sizeof(*root));

	root->type = MVPW_ROOT;
	root->wid = GR_ROOT_WINDOW_ID;
	add_widget(root);

	GrSetWindowBackgroundColor(GR_ROOT_WINDOW_ID, 0);
	GrSelectEvents(GR_ROOT_WINDOW_ID, GR_EVENT_MASK_KEY_DOWN);

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
}

mvp_widget_t*
mvpw_get_root(void)
{
	return root;
}

void
mvpw_set_key(mvp_widget_t *widget, void (*callback)(mvp_widget_t*, char))
{
	widget->key = callback;
}

void
mvpw_set_idle(void (*callback)(void))
{
	idle = callback;
}
