#ifndef MCLIENT_H
#define MCLIENT_H

/*
 *  $Id$
 *
 *  Copyright (C) 2005, Rick Stuart
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


extern int music_client(void);
extern void mclient_idle_callback(mvp_widget_t*);
extern unsigned long curses2ir(int);
extern void mclient_exit(void);
extern void mclient_local_init(void);
extern int mclient_server_connect(void);

extern mvp_widget_t *mclient;
extern char *mclient_server;

/*
 * Track which music server is being used (none, slim, UPNP,...)
 */
extern int mclient_type;

#define MCLIENT_DISABLE		0
#define MCLIENT			1
#define MCLIENT_OTHER		2  /* Maybe someday UPNP? */


#endif /* MCLIENT_H */
