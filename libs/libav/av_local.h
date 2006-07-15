#ifndef AV_LOCAL_H
#define AV_LOCAL_H

/*
 *  $Id$
 *
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

#define init_mtd1	__init_mtd1
#define vid_mode	__vid_mode
#define aspect		__aspect
#define set_output_method		__set_output_method
#define paused		__paused

extern int fd_video;
extern int fd_audio;
extern int paused;
extern int muted;
extern int ffwd;
extern av_mode_t vid_mode;
extern av_tv_aspect_t tv_aspect;

extern int init_mtd1(void);
extern int set_output_method(void);

#define AV_DEBUG(x) printf("%s: %s\n", __FUNCTION__,(x));fflush(stdout);

#endif /* AV_LOCAL_H */
