#ifndef REPLAYTV_H
#define REPLAYTV_H

/*
 *  $Id$
 *
 *  Copyright (C) 2004, John Honeycutt
 *  http://mvpmc.sourceforge.net/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

extern volatile int running_replaytv;

extern int  replaytv_init(char *init_str); 
extern int  replay_gui_init(void);
extern int  replaytv_hide_device_menu(void);
extern int  replaytv_device_update(void);
extern int  replaytv_show_device_menu(void);
extern void replaytv_back_from_video(void);
extern void replaytv_osd_proginfo_update(mvp_widget_t *widget);

#endif /* REPLAYTV_H */
